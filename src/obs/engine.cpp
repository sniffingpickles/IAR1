#define _USE_MATH_DEFINES // MSVC: expose M_PI from <cmath>
#include "engine.hpp"

#include "../core/iar_packet.hpp"
#include "../core/iar_version.h"

#include <obs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace iar {

using clock = std::chrono::steady_clock;

const char *ToString(ConnState s) {
	switch (s) {
	case ConnState::Disconnected:
		return "Disconnected";
	case ConnState::Connecting:
		return "Connecting";
	case ConnState::Connected:
		return "Connected";
	case ConnState::Reconnecting:
		return "Reconnecting";
	case ConnState::AuthFailed:
		return "Auth failed";
	case ConnState::RelayUnreachable:
		return "Relay unreachable";
	}
	return "Unknown";
}

Engine::Engine() = default;

Engine::~Engine() {
	Stop();
}

void Engine::SetState(ConnState s) {
	state_.store(static_cast<int>(s));
}

void Engine::SetError(const std::string &err) {
	std::lock_guard<std::mutex> lk(err_mu_);
	last_error_ = err;
}

bool Engine::Start(const Settings &settings, std::string &out_error) {
	if (running_.load()) {
		out_error = "already running";
		return false;
	}

	auto v = Validate(settings);
	if (!v.ok) {
		out_error = v.errors.empty() ? "invalid settings" : v.errors.front();
		return false;
	}

	settings_ = settings;
	channels_ = settings_.channels;
	frame_samples_ = static_cast<int>(settings_.frame_samples());
	seq_ = 0;
	timestamp_ = 0;
	fatal_stop_.store(false);
	hello_acked_.store(false);
	frames_sent_.store(0);
	bytes_sent_.store(0);
	dropped_frames_.store(0);
	kbps_out_.store(0.0);
	listeners_.store(0);
	last_audio_ns_.store(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			clock::now().time_since_epoch())
			.count());
	audio_present_.store(false);
	SetError("");

	if (!encoder_.Init(settings_.sample_rate, settings_.channels,
	                   settings_.bitrate_bps, settings_.opus_voip)) {
		out_error = "Opus encoder init failed: " + encoder_.last_error();
		return false;
	}

	// ~1 second of audio of headroom in the ring.
	ring_ = std::make_unique<SampleRing>(
		static_cast<std::size_t>(settings_.sample_rate) * channels_);

	ws_ = std::make_unique<WsClient>();
	ws_->set_on_open([this]() {
		SetState(ConnState::Connecting);
		hello_acked_.store(false);
		SendHello();
	});
	ws_->set_on_text([this](const std::string &t) { OnText(t); });
	ws_->set_on_close([this](uint16_t, const std::string &reason) {
		hello_acked_.store(false);
		if (running_.load() && !fatal_stop_.load()) {
			SetState(settings_.reconnect ? ConnState::Reconnecting
			                             : ConnState::Disconnected);
			if (!reason.empty())
				SetError(reason);
		}
	});
	ws_->set_on_error([this](const std::string &reason) {
		hello_acked_.store(false);
		if (running_.load() && !fatal_stop_.load()) {
			SetState(ConnState::RelayUnreachable);
			SetError(reason);
		}
	});

	running_.store(true);

	// Register the OBS raw-audio tap for ONLY the selected mix index. OBS
	// resamples/downmixes to the requested rate/layout for us.
	struct audio_convert_info conv = {};
	conv.samples_per_sec = static_cast<uint32_t>(settings_.sample_rate);
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = (channels_ == 1) ? SPEAKERS_MONO : SPEAKERS_STEREO;
	obs_add_raw_audio_callback(static_cast<size_t>(settings_.mix_index()), &conv,
	                           &Engine::AudioCallback, this);

	worker_ = std::thread(&Engine::WorkerLoop, this);

	SetState(ConnState::Connecting);
	ws_->Connect(settings_.relay_url, settings_.reconnect);

	blog(LOG_INFO,
	     "[irl-audio-return] started: track %d (mix %d), %s %d Hz, %d kbps, %d ms",
	     settings_.track, settings_.mix_index(),
	     channels_ == 1 ? "mono" : "stereo", settings_.sample_rate,
	     settings_.bitrate_bps / 1000, settings_.frame_ms);
	return true;
}

void Engine::Stop() {
	if (!running_.exchange(false)) {
		// Ensure callback is removed even if never fully started.
		return;
	}

	obs_remove_raw_audio_callback(static_cast<size_t>(settings_.mix_index()),
	                              &Engine::AudioCallback, this);

	if (worker_.joinable())
		worker_.join();

	if (ws_) {
		ws_->Stop();
		ws_.reset();
	}
	ring_.reset();
	SetState(ConnState::Disconnected);
	blog(LOG_INFO, "[irl-audio-return] stopped");
}

void Engine::AudioCallback(void *param, std::size_t mix_idx, struct audio_data *data) {
	static_cast<Engine *>(param)->OnAudio(mix_idx, data);
}

void Engine::OnAudio(std::size_t /*mix_idx*/, struct audio_data *data) {
	if (!running_.load() || !ring_)
		return;
	const int ch = channels_;
	const uint32_t frames = data->frames;
	if (frames == 0)
		return;

	// Interleave planar float into a thread-local scratch buffer (no allocation
	// after warm-up). This runs on the OBS audio thread, so it only writes the
	// lock-free ring and never touches locks or the network.
	thread_local std::vector<float> il;
	il.resize(static_cast<std::size_t>(frames) * ch);
	float peak = 0.0f;
	for (int c = 0; c < ch; ++c) {
		const float *src = reinterpret_cast<const float *>(data->data[c]);
		if (!src)
			continue;
		for (uint32_t i = 0; i < frames; ++i) {
			float s = src[i];
			il[static_cast<std::size_t>(i) * ch + c] = s;
			float a = std::fabs(s);
			if (a > peak)
				peak = a;
		}
	}

	ring_->Write(il.data(), il.size());

	if (peak > 1.0e-4f) {
		audio_present_.store(true);
		last_audio_ns_.store(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				clock::now().time_since_epoch())
				.count());
	}
}

void Engine::InjectTestTone() {
	// Handled by the worker so we never introduce a second ring producer.
	tone_remaining_.store(settings_.sample_rate); // ~1 second
}

void Engine::WorkerLoop() {
	const int ch = channels_;
	const int fpc = frame_samples_;
	const std::size_t frame_total = static_cast<std::size_t>(fpc) * ch;
	std::vector<float> frame(frame_total);
	std::vector<std::uint8_t> opus;
	std::deque<std::vector<std::uint8_t>> outbound;

	const std::size_t max_outbound =
		static_cast<std::size_t>(2000 / std::max(1, settings_.frame_ms)); // ~2s
	const std::size_t kCongestionBytes = 256 * 1024;

	auto last_rate = clock::now();
	std::uint64_t last_bytes = 0;
	double tone_phase = 0.0;
	std::int64_t tone_left = 0;

	auto queue_packet = [&](const std::vector<std::uint8_t> &enc) {
		std::vector<std::uint8_t> pkt;
		EncodeMediaFrame(kFlagKey, seq_, timestamp_, static_cast<std::uint32_t>(fpc),
		                 enc.data(), enc.size(), pkt);
		++seq_;
		timestamp_ += static_cast<std::uint64_t>(fpc);
		if (outbound.size() >= max_outbound) {
			outbound.pop_front();
			dropped_frames_.fetch_add(1);
		}
		outbound.push_back(std::move(pkt));
	};

	while (running_.load()) {
		bool did_work = false;

		// Test tone generation (worker is the sole producer in this path).
		if (tone_left <= 0) {
			std::int64_t t = tone_remaining_.exchange(0);
			if (t > 0)
				tone_left = t;
		}
		while (tone_left >= fpc) {
			for (int i = 0; i < fpc; ++i) {
				float s = 0.2f * static_cast<float>(std::sin(tone_phase));
				tone_phase += 2.0 * M_PI * 440.0 / settings_.sample_rate;
				if (tone_phase > 2.0 * M_PI)
					tone_phase -= 2.0 * M_PI;
				for (int c = 0; c < ch; ++c)
					frame[static_cast<std::size_t>(i) * ch + c] = s;
			}
			if (encoder_.Encode(frame.data(), fpc, opus) > 0)
				queue_packet(opus);
			tone_left -= fpc;
			did_work = true;
		}

		// Drain captured audio: encode whole frames.
		while (ring_ && ring_->available() >= frame_total) {
			ring_->Read(frame.data(), frame_total);
			if (encoder_.Encode(frame.data(), fpc, opus) > 0)
				queue_packet(opus);
			did_work = true;
		}
		if (ring_)
			ring_fill_.store(ring_->fill_ratio());

		// Flush to the network only when not congested; otherwise let the
		// outbound queue cap itself (dropping the oldest packets).
		if (ws_ && ws_->IsOpen() && hello_acked_.load()) {
			while (!outbound.empty() && ws_->BufferedAmount() < kCongestionBytes) {
				const auto &f = outbound.front();
				if (!ws_->SendBinary(f.data(), f.size()))
					break;
				frames_sent_.fetch_add(1);
				bytes_sent_.fetch_add(f.size());
				outbound.pop_front();
				did_work = true;
			}
		}
		while (outbound.size() > max_outbound) {
			outbound.pop_front();
			dropped_frames_.fetch_add(1);
		}

		auto now = clock::now();
		double elapsed = std::chrono::duration<double>(now - last_rate).count();
		if (elapsed >= 1.0) {
			std::uint64_t b = bytes_sent_.load();
			kbps_out_.store(static_cast<double>(b - last_bytes) * 8.0 / 1000.0 / elapsed);
			last_bytes = b;
			last_rate = now;
		}

		if (fatal_stop_.load()) {
			if (ws_)
				ws_->Stop();
			break;
		}
		if (!did_work)
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
}

void Engine::SendHello() {
	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "t", "hello");
	obs_data_set_int(d, "v", IAR_PROTOCOL_VERSION);
	obs_data_set_string(d, "stream_id", settings_.stream_id.c_str());
	obs_data_set_string(d, "token", settings_.broadcaster_token.c_str());
	obs_data_set_string(d, "codec", "opus");
	obs_data_set_int(d, "sample_rate", settings_.sample_rate);
	obs_data_set_int(d, "channels", settings_.channels);
	obs_data_set_int(d, "frame_ms", settings_.frame_ms);
	obs_data_set_int(d, "bitrate", settings_.bitrate_bps);
	std::string client = std::string(IAR_CLIENT_NAME) + "/" + IAR_PLUGIN_VERSION + " (obs)";
	obs_data_set_string(d, "client", client.c_str());

	const char *json = obs_data_get_json(d);
	if (json && ws_)
		ws_->SendText(json);
	obs_data_release(d);
	// Never log the token.
	blog(LOG_INFO, "[irl-audio-return] sent hello for stream '%s'",
	     settings_.stream_id.c_str());
}

void Engine::OnText(const std::string &text) {
	obs_data_t *d = obs_data_create_from_json(text.c_str());
	if (!d)
		return;
	const char *t = obs_data_get_string(d, "t");
	if (!t) {
		obs_data_release(d);
		return;
	}
	if (std::strcmp(t, "welcome") == 0) {
		hello_acked_.store(true);
		SetState(ConnState::Connected);
		SetError("");
		blog(LOG_INFO, "[irl-audio-return] relay welcomed stream '%s'",
		     settings_.stream_id.c_str());
	} else if (std::strcmp(t, "status") == 0) {
		listeners_.store(static_cast<int>(obs_data_get_int(d, "listeners")));
	} else if (std::strcmp(t, "error") == 0) {
		const char *code = obs_data_get_string(d, "code");
		const char *msg = obs_data_get_string(d, "message");
		SetError(msg && *msg ? msg : (code ? code : "relay error"));
		if (code && (std::strcmp(code, "auth_failed") == 0 ||
		             std::strcmp(code, "unsupported") == 0 ||
		             std::strcmp(code, "bad_request") == 0)) {
			SetState(ConnState::AuthFailed);
			fatal_stop_.store(true); // worker will close the socket; no reconnect storm
		}
		blog(LOG_WARNING, "[irl-audio-return] relay error: %s",
		     code ? code : "unknown");
	}
	obs_data_release(d);
}

StatsSnapshot Engine::Snapshot() const {
	StatsSnapshot s;
	s.state = static_cast<ConnState>(state_.load());
	s.listeners = listeners_.load();
	s.kbps_out = kbps_out_.load();
	s.ring_fill = ring_fill_.load();
	s.dropped_frames = dropped_frames_.load();
	s.frames_sent = frames_sent_.load();
	s.bytes_sent = bytes_sent_.load();

	std::int64_t last = last_audio_ns_.load();
	std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
		                   clock::now().time_since_epoch())
		                   .count();
	s.seconds_since_audio = static_cast<double>(now - last) / 1.0e9;
	s.audio_present = s.seconds_since_audio < 1.5;
	{
		std::lock_guard<std::mutex> lk(err_mu_);
		s.last_error = last_error_;
	}
	return s;
}

} // namespace iar
