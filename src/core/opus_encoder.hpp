#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iar {

// OpusFrameEncoder is a thin RAII wrapper over libopus for fixed-frame audio.
// It is OBS-independent and unit-testable (encode real frames, decode-verify).
class OpusFrameEncoder {
public:
	OpusFrameEncoder() = default;
	~OpusFrameEncoder();

	OpusFrameEncoder(const OpusFrameEncoder &) = delete;
	OpusFrameEncoder &operator=(const OpusFrameEncoder &) = delete;

	// Init (re)creates the encoder. application_voip selects OPUS_APPLICATION_VOIP
	// (better for speech/alerts at very low bitrate); otherwise AUDIO.
	// Returns true on success; on failure last_error() is set.
	bool Init(int sample_rate, int channels, int bitrate_bps, bool application_voip);

	// Encode one frame of interleaved float PCM (frame_samples per channel).
	// Writes the Opus packet into `out`. Returns the packet size (>0) on success,
	// 0 on a recoverable empty result, or -1 on error.
	int Encode(const float *pcm, int frame_samples, std::vector<std::uint8_t> &out);

	bool valid() const { return enc_ != nullptr; }
	int channels() const { return channels_; }
	int sample_rate() const { return sample_rate_; }
	const std::string &last_error() const { return last_error_; }

private:
	void destroy();

	void *enc_ = nullptr; // OpusEncoder* (opaque to avoid leaking opus.h here)
	int sample_rate_ = 0;
	int channels_ = 0;
	std::string last_error_;
};

} // namespace iar
