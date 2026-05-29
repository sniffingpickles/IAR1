#pragma once

#include "../core/opus_encoder.hpp"
#include "../core/ring_buffer.hpp"
#include "../core/settings.hpp"
#include "../net/ws_client.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct audio_data; // libobs forward decl

namespace iar {

enum class ConnState {
	Disconnected,
	Connecting,
	Connected,
	Reconnecting,
	AuthFailed,
	RelayUnreachable,
};

const char *ToString(ConnState s);

// StatsSnapshot is a consistent copy of live engine status for the UI.
struct StatsSnapshot {
	ConnState state = ConnState::Disconnected;
	int listeners = 0;
	double kbps_out = 0.0;
	double ring_fill = 0.0;
	std::uint64_t dropped_frames = 0;
	std::uint64_t frames_sent = 0;
	std::uint64_t bytes_sent = 0;
	bool audio_present = false;
	double seconds_since_audio = 0.0;
	std::string last_error;
};

// Engine is the runtime: it taps the selected OBS mixer track, encodes Opus on a
// worker thread, and contributes IAR1 frames to the relay over WSS. The OBS
// audio callback only ever touches the lock-free ring buffer. Network back-
// pressure is absorbed by dropping the OLDEST queued Opus packets, so OBS audio
// never stutters.
class Engine {
public:
	Engine();
	~Engine();

	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	// Start validates settings, registers the OBS raw-audio callback, and opens
	// the relay connection. Returns false (with error in `out_error`) if the
	// settings are invalid or the encoder cannot be created.
	bool Start(const Settings &settings, std::string &out_error);

	// Stop tears everything down and is safe to call when not running.
	void Stop();

	bool running() const { return running_.load(); }

	StatsSnapshot Snapshot() const;

	// InjectTestTone pushes ~1s of a 440 Hz tone into the ring for the Test Tone
	// button (useful to verify the path end-to-end without an OBS source).
	void InjectTestTone();

	// Static OBS audio callback trampoline.
	static void AudioCallback(void *param, std::size_t mix_idx, struct audio_data *data);

private:
	void OnAudio(std::size_t mix_idx, struct audio_data *data);
	void WorkerLoop();
	void SendHello();
	void OnText(const std::string &text);
	void SetState(ConnState s);
	void SetError(const std::string &err);

	Settings settings_;
	std::unique_ptr<SampleRing> ring_;
	OpusFrameEncoder encoder_;
	std::unique_ptr<WsClient> ws_;

	std::thread worker_;
	std::atomic<bool> running_{false};
	std::atomic<bool> fatal_stop_{false};
	std::atomic<bool> hello_acked_{false};

	// Stats (atomics; last_error guarded by mutex).
	std::atomic<int> state_{static_cast<int>(ConnState::Disconnected)};
	std::atomic<int> listeners_{0};
	std::atomic<std::uint64_t> frames_sent_{0};
	std::atomic<std::uint64_t> bytes_sent_{0};
	std::atomic<std::uint64_t> dropped_frames_{0};
	std::atomic<double> kbps_out_{0.0};
	std::atomic<double> ring_fill_{0.0};
	std::atomic<std::int64_t> last_audio_ns_{0};
	std::atomic<bool> audio_present_{false};
	std::atomic<std::int64_t> tone_remaining_{0}; // test-tone samples to emit

	mutable std::mutex err_mu_;
	std::string last_error_;

	// Audio-thread -> worker handoff state.
	std::uint64_t seq_ = 0;
	std::uint64_t timestamp_ = 0;
	int channels_ = 1;
	int frame_samples_ = 960;
};

} // namespace iar
