#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// IAR1 binary media frame encode/decode. This mirrors protocol/IAR1.md and the
// Go reference parser in relay/internal/contrib/packet.go. It has no OBS, Qt or
// network dependencies so it can be unit tested standalone.
//
// Header layout (big-endian / network byte order), 30 bytes fixed in v1:
//   0 ..4   magic "IAR1"
//   4       version (uint8)
//   5       flags   (uint8)
//   6 ..8   header_len (uint16) = 30
//   8 ..16  seq (uint64)
//   16..24  timestamp (uint64, codec-sample units)
//   24..28  duration_samples (uint32)
//   28..30  payload_size (uint16)
//   30..    payload (Opus packet)
namespace iar {

inline constexpr std::size_t kHeaderLen = 30;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kMaxPayload = 8192;

inline constexpr std::uint8_t kFlagKey = 0x01;    // independently decodable
inline constexpr std::uint8_t kFlagMarker = 0x02; // start of talkspurt

struct MediaFrame {
	std::uint8_t version = kVersion;
	std::uint8_t flags = 0;
	std::uint64_t seq = 0;
	std::uint64_t timestamp = 0;
	std::uint32_t duration_samples = 0;
	const std::uint8_t *payload = nullptr;
	std::size_t payload_len = 0;
};

// EncodeMediaFrame serializes header + payload into `out` (resized as needed).
// Returns the total number of bytes written, or 0 on invalid input
// (payload too large / null payload with non-zero length).
std::size_t EncodeMediaFrame(std::uint8_t flags, std::uint64_t seq,
                             std::uint64_t timestamp,
                             std::uint32_t duration_samples,
                             const std::uint8_t *payload,
                             std::size_t payload_len,
                             std::vector<std::uint8_t> &out);

// DecodeMediaFrame validates and parses a frame. `frame.payload` aliases `data`.
// Returns true on success. Mirrors the relay's validation rules exactly.
// max_duration_samples is a sanity ceiling (0 to skip).
bool DecodeMediaFrame(const std::uint8_t *data, std::size_t len,
                      std::uint32_t max_duration_samples, MediaFrame &frame);

} // namespace iar
