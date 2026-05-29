#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace iar {

bool IsAllowedBitrate(int bps) {
	switch (bps) {
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		return true;
	default:
		return false;
	}
}

bool IsAllowedSampleRate(int hz) {
	switch (hz) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 48000:
		return true;
	default:
		return false;
	}
}

static bool valid_stream_id(const std::string &id) {
	if (id.empty() || id.size() > 64)
		return false;
	auto ok_char = [](char c, bool first) {
		if (std::isdigit((unsigned char)c) || (c >= 'a' && c <= 'z'))
			return true;
		if (!first && (c == '-' || c == '_'))
			return true;
		return false;
	};
	for (std::size_t i = 0; i < id.size(); ++i)
		if (!ok_char(id[i], i == 0))
			return false;
	return true;
}

static bool starts_with(const std::string &s, const char *p) {
	return s.rfind(p, 0) == 0;
}

ValidationResult Validate(const Settings &s) {
	ValidationResult r;

	if (!starts_with(s.relay_url, "wss://") && !starts_with(s.relay_url, "ws://"))
		r.errors.push_back("Relay URL must start with wss:// (or ws:// for local testing).");
	else if (starts_with(s.relay_url, "ws://") && s.relay_url.find("://127.0.0.1") == std::string::npos &&
	         s.relay_url.find("://localhost") == std::string::npos)
		r.warnings.push_back("Using ws:// to a non-local host sends your token unencrypted. Use wss://.");

	if (!valid_stream_id(s.stream_id))
		r.errors.push_back("Stream ID must be 1-64 chars: lowercase letters, digits, '-' or '_'.");

	if (s.broadcaster_token.size() < 8)
		r.errors.push_back("Stream key/token is missing or too short.");

	if (s.track < 1 || s.track > 6)
		r.errors.push_back("Track must be between 1 and 6.");

	if (!IsAllowedBitrate(s.bitrate_bps))
		r.errors.push_back("Bitrate must be one of 24, 32, 48, 64, 96 kbps.");

	if (s.channels != 1 && s.channels != 2)
		r.errors.push_back("Channels must be Mono (1) or Stereo (2).");

	if (s.frame_ms != 10 && s.frame_ms != 20)
		r.errors.push_back("Frame size must be 20 ms or 10 ms.");

	if (!IsAllowedSampleRate(s.sample_rate))
		r.errors.push_back("Sample rate must be one of 8000/12000/16000/24000/48000 Hz.");

	// Non-blocking guidance.
	if (s.track == 1)
		r.warnings.push_back(
			"Track 1 usually carries your full public stream audio. Sending it to "
			"the return bus may echo your entire mix to your earpiece. Use a "
			"dedicated track (e.g. Track 6) for private return audio.");
	if (s.channels == 2 && s.bitrate_bps < 48000)
		r.warnings.push_back("Stereo below 48 kbps may sound poor; 64 kbps is recommended for stereo.");

	r.ok = r.errors.empty();
	return r;
}

std::string MaskSecret(const std::string &secret) {
	if (secret.empty())
		return "";
	if (secret.size() <= 6)
		return std::string(secret.size(), '*');
	std::string out;
	out += secret.substr(0, 2);
	out += std::string(secret.size() - 4, '*');
	out += secret.substr(secret.size() - 2);
	return out;
}

std::string BuildListenerHint(const std::string &relay_url,
                              const std::string &listener_token) {
	if (listener_token.empty())
		return "";
	std::string base = relay_url;
	// wss:// -> https://, ws:// -> http://
	if (starts_with(base, "wss://"))
		base = "https://" + base.substr(6);
	else if (starts_with(base, "ws://"))
		base = "http://" + base.substr(5);
	// Strip a trailing /contribute path component.
	const std::string contribute = "/contribute";
	if (base.size() >= contribute.size() &&
	    base.compare(base.size() - contribute.size(), contribute.size(), contribute) == 0)
		base = base.substr(0, base.size() - contribute.size());
	// Strip any remaining path after the host.
	auto scheme_end = base.find("://");
	if (scheme_end != std::string::npos) {
		auto slash = base.find('/', scheme_end + 3);
		if (slash != std::string::npos)
			base = base.substr(0, slash);
	}
	return base + "/l/" + listener_token;
}

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const std::string &in) {
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	std::size_t i = 0;
	while (i + 3 <= in.size()) {
		std::uint32_t n = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8) |
		                  std::uint8_t(in[i + 2]);
		out += kB64[(n >> 18) & 63];
		out += kB64[(n >> 12) & 63];
		out += kB64[(n >> 6) & 63];
		out += kB64[n & 63];
		i += 3;
	}
	if (i + 1 == in.size()) {
		std::uint32_t n = std::uint8_t(in[i]) << 16;
		out += kB64[(n >> 18) & 63];
		out += kB64[(n >> 12) & 63];
	} else if (i + 2 == in.size()) {
		std::uint32_t n = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8);
		out += kB64[(n >> 18) & 63];
		out += kB64[(n >> 12) & 63];
		out += kB64[(n >> 6) & 63];
	}
	return out;
}

int b64val(char c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '-' || c == '+')
		return 62;
	if (c == '_' || c == '/')
		return 63;
	return -1;
}

bool base64url_decode(const std::string &in, std::string &out) {
	out.clear();
	int buf = 0, bits = 0;
	for (char c : in) {
		if (c == '=' || c == '\n' || c == '\r' || c == ' ')
			continue;
		int v = b64val(c);
		if (v < 0)
			return false;
		buf = (buf << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += static_cast<char>((buf >> bits) & 0xFF);
		}
	}
	return true;
}

} // namespace

bool ParsePairingCode(const std::string &code, Settings &out) {
	std::string c = code;
	// Trim whitespace and an optional "IAR1:" / "iar1:" scheme-like prefix.
	while (!c.empty() && (c.front() == ' ' || c.front() == '\t' || c.front() == '\n' || c.front() == '\r'))
		c.erase(c.begin());
	while (!c.empty() && (c.back() == ' ' || c.back() == '\t' || c.back() == '\n' || c.back() == '\r'))
		c.pop_back();
	if (c.rfind("IAR1:", 0) == 0 || c.rfind("iar1:", 0) == 0)
		c = c.substr(5);
	if (c.empty())
		return false;

	std::string decoded;
	if (!base64url_decode(c, decoded))
		return false;

	// Split on '|'.
	std::vector<std::string> parts;
	std::string cur;
	for (char ch : decoded) {
		if (ch == '|') {
			parts.push_back(cur);
			cur.clear();
		} else {
			cur += ch;
		}
	}
	parts.push_back(cur);

	if (parts.size() < 5 || parts[0] != "IAR1")
		return false;

	out.relay_url = parts[1];
	out.stream_id = parts[2];
	out.broadcaster_token = parts[3];
	out.listener_token = parts[4];
	if (parts.size() >= 6 && !parts[5].empty()) {
		int ch = std::atoi(parts[5].c_str());
		if (ch == 1 || ch == 2)
			out.channels = ch;
	}
	if (parts.size() >= 7 && !parts[6].empty()) {
		int b = std::atoi(parts[6].c_str());
		if (IsAllowedBitrate(b))
			out.bitrate_bps = b;
	}
	return true;
}

std::string BuildPairingCode(const Settings &s) {
	std::ostringstream raw;
	raw << "IAR1|" << s.relay_url << "|" << s.stream_id << "|" << s.broadcaster_token
	    << "|" << s.listener_token << "|" << s.channels << "|" << s.bitrate_bps;
	return base64url_encode(raw.str());
}

namespace {
// Percent-encode per RFC 3986 (unreserved chars pass through). Used to embed the
// settings JSON in the moblin:// deep link query, matching Moblin's decoder.
std::string percent_encode(const std::string &in) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : in) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// Minimal JSON string escaping for the URL value.
std::string json_escape(const std::string &s) {
	std::string out;
	for (char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		default: out += c; break;
		}
	}
	return out;
}
} // namespace

std::string BuildMoblinDeepLink(const std::string &listener_url) {
	if (listener_url.empty())
		return "";
	std::string json = "{\"webBrowser\":{\"home\":\"" + json_escape(listener_url) + "\"}}";
	return "moblin://?" + percent_encode(json);
}

} // namespace iar
