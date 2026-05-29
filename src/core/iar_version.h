#pragma once

// Plugin / protocol versioning. PLUGIN_VERSION is injected by CMake; the
// fallback keeps standalone core test builds compiling.
#ifndef IAR_PLUGIN_VERSION
#define IAR_PLUGIN_VERSION "1.0.1"
#endif

#define IAR_PROTOCOL_VERSION 1
#define IAR_CLIENT_NAME "irl-audio-return-plugin"
