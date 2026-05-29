#include "settings_store.hpp"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>

#include <string>

namespace iar {
namespace {

std::string config_file(const char *name) {
	char *p = obs_module_config_path(name);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

} // namespace

bool LoadSettings(Settings &out) {
	std::string path = config_file("settings.json");
	obs_data_t *d = obs_data_create_from_json_file(path.c_str());
	if (!d)
		return false;

	// Defaults (in case a key is missing) before reading.
	obs_data_set_default_int(d, "track", 6);
	obs_data_set_default_int(d, "bitrate", 32000);
	obs_data_set_default_int(d, "channels", 1);
	obs_data_set_default_int(d, "frame_ms", 20);
	obs_data_set_default_bool(d, "reconnect", true);

	out.relay_url = obs_data_get_string(d, "relay_url");
	out.stream_id = obs_data_get_string(d, "stream_id");
	out.broadcaster_token = obs_data_get_string(d, "token");
	out.listener_token = obs_data_get_string(d, "listener_token");
	out.track = static_cast<int>(obs_data_get_int(d, "track"));
	out.bitrate_bps = static_cast<int>(obs_data_get_int(d, "bitrate"));
	out.channels = static_cast<int>(obs_data_get_int(d, "channels"));
	out.frame_ms = static_cast<int>(obs_data_get_int(d, "frame_ms"));
	out.sample_rate = 48000;
	out.reconnect = obs_data_get_bool(d, "reconnect");
	out.auto_start = obs_data_get_bool(d, "auto_start");
	out.start_with_stream = obs_data_get_bool(d, "start_with_stream");

	obs_data_release(d);
	return true;
}

void SaveSettings(const Settings &s) {
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "relay_url", s.relay_url.c_str());
	obs_data_set_string(d, "stream_id", s.stream_id.c_str());
	obs_data_set_string(d, "token", s.broadcaster_token.c_str());
	obs_data_set_string(d, "listener_token", s.listener_token.c_str());
	obs_data_set_int(d, "track", s.track);
	obs_data_set_int(d, "bitrate", s.bitrate_bps);
	obs_data_set_int(d, "channels", s.channels);
	obs_data_set_int(d, "frame_ms", s.frame_ms);
	obs_data_set_bool(d, "reconnect", s.reconnect);
	obs_data_set_bool(d, "auto_start", s.auto_start);
	obs_data_set_bool(d, "start_with_stream", s.start_with_stream);

	std::string path = config_file("settings.json");
	obs_data_save_json_safe(d, path.c_str(), "tmp", "bak");
	obs_data_release(d);
}

} // namespace iar
