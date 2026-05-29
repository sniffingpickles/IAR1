#include "opus_encoder.hpp"

#include <opus.h>

namespace iar {

OpusFrameEncoder::~OpusFrameEncoder() {
	destroy();
}

void OpusFrameEncoder::destroy() {
	if (enc_) {
		opus_encoder_destroy(static_cast<OpusEncoder *>(enc_));
		enc_ = nullptr;
	}
}

bool OpusFrameEncoder::Init(int sample_rate, int channels, int bitrate_bps,
                            bool application_voip) {
	destroy();
	last_error_.clear();

	if (channels != 1 && channels != 2) {
		last_error_ = "unsupported channel count";
		return false;
	}

	int err = OPUS_OK;
	int application = application_voip ? OPUS_APPLICATION_VOIP
	                                   : OPUS_APPLICATION_AUDIO;
	OpusEncoder *enc = opus_encoder_create(sample_rate, channels, application, &err);
	if (err != OPUS_OK || !enc) {
		last_error_ = opus_strerror(err);
		return false;
	}

	opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));
	opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(10));
	opus_encoder_ctl(enc, OPUS_SET_SIGNAL(application_voip ? OPUS_SIGNAL_VOICE
	                                                        : OPUS_AUTO));
	// Favour low, predictable latency for return audio.
	opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(8));

	enc_ = enc;
	sample_rate_ = sample_rate;
	channels_ = channels;
	return true;
}

int OpusFrameEncoder::Encode(const float *pcm, int frame_samples,
                             std::vector<std::uint8_t> &out) {
	if (!enc_) {
		last_error_ = "encoder not initialized";
		return -1;
	}
	// Worst-case Opus packet is well under 4 KB for our bitrates/frame sizes.
	out.resize(4000);
	opus_int32 n = opus_encode_float(static_cast<OpusEncoder *>(enc_), pcm,
	                                 frame_samples, out.data(),
	                                 static_cast<opus_int32>(out.size()));
	if (n < 0) {
		last_error_ = opus_strerror(n);
		return -1;
	}
	out.resize(static_cast<std::size_t>(n));
	return n;
}

} // namespace iar
