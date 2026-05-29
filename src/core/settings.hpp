#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iar {

// Settings is the user-configurable plugin state. It is OBS-independent so it
// (and its validation) can be unit tested without launching OBS. The OBS layer
// loads/stores it via obs_data_t and the Qt UI edits it.
struct Settings {
	bool enabled = false;
	std::string relay_url;        // wss://host[:port]/contribute
	std::string stream_id;        // [a-z0-9][a-z0-9_-]{0,63}
	std::string broadcaster_token; // secret; never logged in plaintext
	std::string listener_token;    // optional; only to display/copy listener URL
	int track = 6;                // OBS Track 1..6 (UI is 1-based)
	int bitrate_bps = 32000;      // 24/32/48/64/96 kbps
	int channels = 1;             // 1 = mono, 2 = stereo
	int frame_ms = 20;            // 20 (default) or 10
	int sample_rate = 48000;      // Opus clock; OBS is commonly 48 kHz
	bool reconnect = true;
	bool opus_voip = false;       // false = OPUS_APPLICATION_AUDIO
	bool auto_start = false;       // start automatically when OBS launches
	bool start_with_stream = false; // start/stop together with the OBS stream

	// Convenience: the OBS mixer index for the selected track (Track N -> N-1).
	int mix_index() const { return track - 1; }

	// Samples per Opus frame, per channel.
	std::uint32_t frame_samples() const {
		return static_cast<std::uint32_t>(sample_rate / 1000 * frame_ms);
	}
};

struct ValidationResult {
	bool ok = false;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

// Validate checks the settings. Errors block starting; warnings are surfaced in
// the UI but do not block (e.g. selecting Track 1).
ValidationResult Validate(const Settings &s);

// Allowed discrete option sets (shared with the UI).
bool IsAllowedBitrate(int bps);
bool IsAllowedSampleRate(int hz);

// MaskSecret returns a display-safe rendering of a secret: never reveals the
// middle. Short secrets are fully masked.
std::string MaskSecret(const std::string &secret);

// BuildListenerHint converts a wss contribute URL + listener token into a guess
// at the public listener URL for display only (the relay is authoritative).
std::string BuildListenerHint(const std::string &relay_url,
                              const std::string &listener_token);

// ParsePairingCode decodes a one-paste setup code produced by the control panel
// and fills the relevant fields of `out` (relay_url, stream_id, tokens, channels,
// bitrate). This is the "idiot-proof" path: the streamer pastes a single code
// instead of filling six fields. Returns true on success.
//
// Wire format: base64url( "IAR1|<relay>|<stream>|<btoken>|<ltoken>|<channels>|<bitrate_bps>" )
// An optional "IAR1:" prefix on the code is accepted and ignored.
bool ParsePairingCode(const std::string &code, Settings &out);

// BuildPairingCode is the inverse of ParsePairingCode (used by tests and tooling).
std::string BuildPairingCode(const Settings &s);

// BuildMoblinDeepLink builds a `moblin://` settings deep link that points
// Moblin's built-in Web browser at the given listener URL. Mirrors the relay's
// provision.MoblinDeepLink so the plugin can render the same QR offline.
// Returns "" if listener_url is empty.
std::string BuildMoblinDeepLink(const std::string &listener_url);

} // namespace iar
