// Standalone unit tests for the OBS plugin's OBS-independent core: IAR1 packet
// encoding (incl. cross-language wire-format parity), the lock-free ring buffer,
// settings validation, and the Opus encoder. These build and run without OBS or
// Qt - only libopus is required - so they run on every developer machine and in
// CI on all platforms.

#define _USE_MATH_DEFINES // MSVC: expose M_PI from <cmath>
#include "../src/core/iar_packet.hpp"
#include "../src/core/opus_encoder.hpp"
#include "../src/core/ring_buffer.hpp"
#include "../src/core/settings.hpp"

#include <opus.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++g_checks;                                                            \
		if (!(cond)) {                                                         \
			++g_failures;                                                      \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
		}                                                                      \
	} while (0)

static void test_packet_roundtrip() {
	std::printf("test_packet_roundtrip\n");
	const std::uint8_t payload[] = {0xfc, 0x01, 0x02, 0x03, 0x04};
	std::vector<std::uint8_t> buf;
	std::size_t n = iar::EncodeMediaFrame(iar::kFlagKey | iar::kFlagMarker, 42,
	                                       960 * 7, 960, payload, sizeof(payload),
	                                       buf);
	CHECK(n == iar::kHeaderLen + sizeof(payload));

	iar::MediaFrame f;
	CHECK(iar::DecodeMediaFrame(buf.data(), buf.size(), 48000, f));
	CHECK(f.seq == 42);
	CHECK(f.timestamp == 960 * 7);
	CHECK(f.duration_samples == 960);
	CHECK(f.payload_len == sizeof(payload));
	CHECK((f.flags & iar::kFlagKey) != 0);
	CHECK((f.flags & iar::kFlagMarker) != 0);
	CHECK(std::memcmp(f.payload, payload, sizeof(payload)) == 0);
}

static void test_packet_wire_parity() {
	std::printf("test_packet_wire_parity\n");
	// Lock the exact big-endian byte layout shared with the Go relay.
	const std::uint8_t payload[] = {0xAA, 0xBB};
	std::vector<std::uint8_t> buf;
	iar::EncodeMediaFrame(iar::kFlagKey, /*seq*/ 0x0102030405060708ULL,
	                      /*ts*/ 0x1112131415161718ULL, /*dur*/ 0x000003C0u, // 960
	                      payload, sizeof(payload), buf);
	const std::uint8_t expected[] = {
	    'I',  'A',  'R',  '1',             // magic
	    0x01,                              // version
	    0x01,                              // flags = KEY
	    0x00, 0x1E,                        // header_len = 30
	    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // seq
	    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // timestamp
	    0x00, 0x00, 0x03, 0xC0,            // duration_samples = 960
	    0x00, 0x02,                        // payload_size = 2
	    0xAA, 0xBB,                        // payload
	};
	CHECK(buf.size() == sizeof(expected));
	CHECK(std::memcmp(buf.data(), expected, sizeof(expected)) == 0);
}

static void test_packet_malformed() {
	std::printf("test_packet_malformed\n");
	const std::uint8_t payload[] = {1, 2, 3};
	std::vector<std::uint8_t> buf;
	iar::EncodeMediaFrame(0, 1, 1, 960, payload, sizeof(payload), buf);
	iar::MediaFrame f;

	CHECK(!iar::DecodeMediaFrame(buf.data(), iar::kHeaderLen - 1, 48000, f)); // short
	auto bad = buf;
	bad[0] = 'X';
	CHECK(!iar::DecodeMediaFrame(bad.data(), bad.size(), 48000, f)); // magic
	bad = buf;
	bad[4] = 9;
	CHECK(!iar::DecodeMediaFrame(bad.data(), bad.size(), 48000, f)); // version
	bad = buf;
	bad[28] = 0x00;
	bad[29] = 0x63; // payload_size = 99 != 3
	CHECK(!iar::DecodeMediaFrame(bad.data(), bad.size(), 48000, f));
	bad = buf;
	bad[24] = bad[25] = bad[26] = bad[27] = 0; // duration 0
	CHECK(!iar::DecodeMediaFrame(bad.data(), bad.size(), 48000, f));
	// Oversized payload claim is rejected by EncodeMediaFrame.
	std::vector<std::uint8_t> big(iar::kMaxPayload + 1, 0);
	std::vector<std::uint8_t> out;
	CHECK(iar::EncodeMediaFrame(0, 0, 0, 960, big.data(), big.size(), out) == 0);
}

static void test_ring_buffer() {
	std::printf("test_ring_buffer\n");
	iar::SampleRing ring(1024);
	float in[100];
	for (int i = 0; i < 100; ++i)
		in[i] = static_cast<float>(i);

	CHECK(ring.Write(in, 100) == 100);
	CHECK(ring.available() == 100);

	float out[100] = {0};
	CHECK(ring.Read(out, 100) == 100);
	for (int i = 0; i < 100; ++i)
		CHECK(out[i] == static_cast<float>(i));
	CHECK(ring.available() == 0);

	// Wrap-around correctness: push/pull repeatedly across the boundary.
	iar::SampleRing small(8); // usable capacity 15 (next pow2 of 9 = 16)
	float a[5] = {1, 2, 3, 4, 5};
	float b[5] = {0};
	for (int iter = 0; iter < 50; ++iter) {
		CHECK(small.Write(a, 5) == 5);
		CHECK(small.Read(b, 5) == 5);
		for (int i = 0; i < 5; ++i)
			CHECK(b[i] == a[i]);
	}

	// Overflow drops the incoming write and counts samples, never blocks.
	iar::SampleRing tiny(4);
	float big[10] = {0};
	CHECK(tiny.Write(big, 4) == 4);   // fills usable capacity (mask = 7? -> see note)
	std::uint64_t dropped_before = tiny.dropped_samples();
	CHECK(tiny.Write(big, 10) == 0);  // cannot fit -> rejected
	CHECK(tiny.dropped_samples() == dropped_before + 10);
}

static void test_settings_validation() {
	std::printf("test_settings_validation\n");
	iar::Settings s;
	s.relay_url = "wss://relay.example.com/contribute";
	s.stream_id = "alerts-main";
	s.broadcaster_token = "a-very-secret-token";
	s.track = 6;
	s.bitrate_bps = 32000;
	s.channels = 1;
	s.frame_ms = 20;
	s.sample_rate = 48000;

	auto r = iar::Validate(s);
	CHECK(r.ok);
	CHECK(r.warnings.empty());
	CHECK(s.mix_index() == 5);
	CHECK(s.frame_samples() == 960);

	// Track 1 is allowed but warns.
	s.track = 1;
	r = iar::Validate(s);
	CHECK(r.ok);
	CHECK(!r.warnings.empty());

	// Invalid combinations produce errors.
	iar::Settings bad;
	bad.relay_url = "http://nope";
	bad.stream_id = "Bad ID";
	bad.broadcaster_token = "x";
	bad.track = 9;
	bad.bitrate_bps = 12345;
	bad.channels = 3;
	bad.frame_ms = 15;
	r = iar::Validate(bad);
	CHECK(!r.ok);
	CHECK(r.errors.size() >= 5);
}

static void test_mask_secret() {
	std::printf("test_mask_secret\n");
	CHECK(iar::MaskSecret("") == "");
	CHECK(iar::MaskSecret("abc") == "***");
	CHECK(iar::MaskSecret("abcdefghij") == "ab******ij");
	// Never reveals the middle.
	std::string m = iar::MaskSecret("supersecrettoken");
	CHECK(m.find("persecret") == std::string::npos);
}

static void test_listener_hint() {
	std::printf("test_listener_hint\n");
	CHECK(iar::BuildListenerHint("wss://r.example.com/contribute", "tok123") ==
	      "https://r.example.com/l/tok123");
	CHECK(iar::BuildListenerHint("ws://127.0.0.1:8080/contribute", "tok") ==
	      "http://127.0.0.1:8080/l/tok");
	CHECK(iar::BuildListenerHint("wss://r.example.com", "") == "");
}

static void test_moblin_deeplink() {
	std::printf("test_moblin_deeplink\n");
	CHECK(iar::BuildMoblinDeepLink("") == "");
	std::string dl = iar::BuildMoblinDeepLink("https://alerts.irlhosting.com/l/abc");
	CHECK(dl.rfind("moblin://?", 0) == 0);
	// Percent-encoded JSON must carry the webBrowser/home keys and the URL host.
	CHECK(dl.find("webBrowser") != std::string::npos);
	CHECK(dl.find("alerts.irlhosting.com") != std::string::npos);
	// Slashes/colon in the URL must be percent-encoded (no raw "://" after query).
	CHECK(dl.find("https://") == std::string::npos);
}

static void test_pairing_code() {
	std::printf("test_pairing_code\n");
	iar::Settings s;
	s.relay_url = "wss://alerts.irlhosting.com/contribute";
	s.stream_id = "alerts-main";
	s.broadcaster_token = "broadcaster-secret-token-123";
	s.listener_token = "listener-secret-token-456";
	s.channels = 2;
	s.bitrate_bps = 64000;

	std::string code = iar::BuildPairingCode(s);
	CHECK(!code.empty());
	// Code must be opaque (no raw token visible).
	CHECK(code.find("broadcaster-secret") == std::string::npos);

	iar::Settings out;
	CHECK(iar::ParsePairingCode(code, out));
	CHECK(out.relay_url == s.relay_url);
	CHECK(out.stream_id == s.stream_id);
	CHECK(out.broadcaster_token == s.broadcaster_token);
	CHECK(out.listener_token == s.listener_token);
	CHECK(out.channels == 2);
	CHECK(out.bitrate_bps == 64000);

	// Accepts an optional "IAR1:" prefix and surrounding whitespace.
	iar::Settings out2;
	CHECK(iar::ParsePairingCode("  IAR1:" + code + "\n", out2));
	CHECK(out2.stream_id == "alerts-main");

	// Rejects garbage.
	iar::Settings bad;
	CHECK(!iar::ParsePairingCode("not a real code !!!", bad));
	CHECK(!iar::ParsePairingCode("", bad));
}

static void test_opus_encode_decode() {
	std::printf("test_opus_encode_decode\n");
	iar::OpusFrameEncoder enc;
	CHECK(enc.Init(48000, 1, 32000, false));
	CHECK(enc.valid());

	const int frame = 960; // 20 ms @ 48 kHz
	std::vector<float> pcm(frame);
	for (int i = 0; i < frame; ++i)
		pcm[i] = 0.25f * std::sin(2.0 * M_PI * 440.0 * i / 48000.0);

	std::vector<std::uint8_t> packet;
	int n = enc.Encode(pcm.data(), frame, packet);
	CHECK(n > 0);
	CHECK(packet.size() == static_cast<std::size_t>(n));
	CHECK(packet.size() <= iar::kMaxPayload);

	// Decode the Opus packet back and confirm the frame size round-trips.
	int derr = OPUS_OK;
	OpusDecoder *dec = opus_decoder_create(48000, 1, &derr);
	CHECK(derr == OPUS_OK && dec != nullptr);
	std::vector<float> decoded(frame);
	int got = opus_decode_float(dec, packet.data(), (opus_int32)packet.size(),
	                            decoded.data(), frame, 0);
	CHECK(got == frame);
	opus_decoder_destroy(dec);

	// Stereo init should also succeed.
	iar::OpusFrameEncoder st;
	CHECK(st.Init(48000, 2, 64000, false));
}

int main() {
	test_packet_roundtrip();
	test_packet_wire_parity();
	test_packet_malformed();
	test_ring_buffer();
	test_settings_validation();
	test_mask_secret();
	test_listener_hint();
	test_moblin_deeplink();
	test_pairing_code();
	test_opus_encode_decode();

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
