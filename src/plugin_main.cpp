#include "core/iar_version.h"
#include "obs/engine.hpp"
#include "ui/iar_dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("irl-audio-return", "en-US")

namespace {
// The engine is a process-lifetime singleton. It is intentionally never freed:
// the dock (owned by the OBS frontend) may be destroyed after the module is
// unloaded, and it references the engine from its status timer. Stopping the
// engine on unload releases the OBS audio callback and network socket, which is
// the resource cleanup that actually matters.
iar::Engine *g_engine = nullptr;
iar::IarDock *g_dock = nullptr;

// Auto-start once the OBS frontend has finished loading scenes and the audio
// subsystem is ready. This is what lets the plugin "just stream" on launch.
void on_frontend_event(enum obs_frontend_event event, void *) {
	if (!g_dock)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		g_dock->AutoStartIfEnabled();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		g_dock->OnStreamingStarted();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		g_dock->OnStreamingStopped();
		break;
	default:
		break;
	}
}

void on_tools_menu_clicked(void *) {
	if (g_dock)
		g_dock->OpenSettings();
}
} // namespace

const char *obs_module_name(void) {
	return "IRL Audio Return";
}

const char *obs_module_description(void) {
	return obs_module_text("Description");
}

bool obs_module_load(void) {
	g_engine = new iar::Engine();
	blog(LOG_INFO, "[irl-audio-return] loaded v%s (protocol v%d)",
	     IAR_PLUGIN_VERSION, IAR_PROTOCOL_VERSION);
	return true;
}

void obs_module_post_load(void) {
	// The dock must be created after the Qt frontend is ready. OBS takes
	// ownership of the widget and wraps it in a managed QDockWidget.
	g_dock = new iar::IarDock(g_engine);
	obs_frontend_add_dock_by_id("irl-audio-return", obs_module_text("Title"), g_dock);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_frontend_add_tools_menu_item(obs_module_text("Menu.Settings"), on_tools_menu_clicked, nullptr);
}

void obs_module_unload(void) {
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (g_engine)
		g_engine->Stop();
	blog(LOG_INFO, "[irl-audio-return] unloaded");
}
