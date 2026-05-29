#include "iar_packet.hpp"

#include <cstring>

namespace iar {
namespace {

void put_u16(std::uint8_t *p, std::uint16_t v) {
	p[0] = static_cast<std::uint8_t>(v >> 8);
	p[1] = static_cast<std::uint8_t>(v);
}
void put_u32(std::uint8_t *p, std::uint32_t v) {
	p[0] = static_cast<std::uint8_t>(v >> 24);
	p[1] = static_cast<std::uint8_t>(v >> 16);
	p[2] = static_cast<std::uint8_t>(v >> 8);
	p[3] = static_cast<std::uint8_t>(v);
}
void put_u64(std::uint8_t *p, std::uint64_t v) {
	for (int i = 0; i < 8; ++i)
		p[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
}
std::uint16_t get_u16(const std::uint8_t *p) {
	return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}
std::uint32_t get_u32(const std::uint8_t *p) {
	return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
	       (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::uint64_t get_u64(const std::uint8_t *p) {
	std::uint64_t v = 0;
	for (int i = 0; i < 8; ++i)
		v = (v << 8) | p[i];
	return v;
}

} // namespace

std::size_t EncodeMediaFrame(std::uint8_t flags, std::uint64_t seq,
                             std::uint64_t timestamp,
                             std::uint32_t duration_samples,
                             const std::uint8_t *payload,
                             std::size_t payload_len,
                             std::vector<std::uint8_t> &out) {
	if (payload_len > kMaxPayload)
		return 0;
	if (payload_len > 0 && payload == nullptr)
		return 0;

	out.resize(kHeaderLen + payload_len);
	std::uint8_t *p = out.data();
	p[0] = 'I';
	p[1] = 'A';
	p[2] = 'R';
	p[3] = '1';
	p[4] = kVersion;
	p[5] = flags;
	put_u16(p + 6, static_cast<std::uint16_t>(kHeaderLen));
	put_u64(p + 8, seq);
	put_u64(p + 16, timestamp);
	put_u32(p + 24, duration_samples);
	put_u16(p + 28, static_cast<std::uint16_t>(payload_len));
	if (payload_len > 0)
		std::memcpy(p + kHeaderLen, payload, payload_len);
	return out.size();
}

bool DecodeMediaFrame(const std::uint8_t *data, std::size_t len,
                      std::uint32_t max_duration_samples, MediaFrame &frame) {
	if (len < kHeaderLen)
		return false;
	if (data[0] != 'I' || data[1] != 'A' || data[2] != 'R' || data[3] != '1')
		return false;
	if (data[4] != kVersion)
		return false;

	std::size_t header_len = get_u16(data + 6);
	if (header_len < kHeaderLen || header_len > len)
		return false;

	frame.version = data[4];
	frame.flags = data[5];
	frame.seq = get_u64(data + 8);
	frame.timestamp = get_u64(data + 16);
	frame.duration_samples = get_u32(data + 24);
	std::size_t payload_size = get_u16(data + 28);

	std::size_t actual_payload = len - header_len;
	if (payload_size != actual_payload)
		return false;
	if (payload_size == 0 || payload_size > kMaxPayload)
		return false;
	if (frame.duration_samples == 0)
		return false;
	if (max_duration_samples != 0 && frame.duration_samples > max_duration_samples)
		return false;

	frame.payload = data + header_len;
	frame.payload_len = payload_size;
	return true;
}

} // namespace iar
