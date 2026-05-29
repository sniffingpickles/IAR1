cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# This plugin needs the OBS frontend API (to add a dock) and Qt (for the dock UI).
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  if(APPLE)
    # Recent macOS SDKs removed the legacy AGL framework that Qt6's WrapOpenGL
    # module falls back to when find_library(AGL) fails, which breaks linking.
    # Redirect the lookup to OpenGL; this plugin is audio-only and uses neither.
    find_library(_IAR_OPENGL_FRAMEWORK NAMES OpenGL)
    if(_IAR_OPENGL_FRAMEWORK)
      set(WrapOpenGL_AGL "${_IAR_OPENGL_FRAMEWORK}" CACHE FILEPATH "AGL shim -> OpenGL" FORCE)
    endif()
  endif()
  find_package(Qt6 COMPONENTS Widgets Core Gui REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# --- Third-party dependencies fetched and built from source for portability ---
include(FetchContent)

# libopus (the audio codec). Build a static library and link it privately.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  opus
  GIT_REPOSITORY https://github.com/xiph/opus.git
  GIT_TAG v1.5.2
  GIT_SHALLOW TRUE
)

# IXWebSocket (the WSS client). TLS backend is platform-native to avoid extra
# dependencies: SecureTransport on macOS, OpenSSL elsewhere (provided by obs-deps
# on Windows, by the system on Linux).
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(USE_SECURE_TRANSPORT ON CACHE BOOL "" FORCE)
else()
  set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(opus ixwebsocket)

# The plugin build (from the OBS template) compiles with -Werror and strict
# warnings. Third-party fetched sources are not our code and must not fail the
# build on their own warnings, so silence warnings for those targets only.
foreach(_thirdparty opus ixwebsocket)
  if(TARGET ${_thirdparty})
    if(MSVC)
      target_compile_options(${_thirdparty} PRIVATE /w)
    else()
      target_compile_options(${_thirdparty} PRIVATE -w -Wno-error)
    endif()
    set_target_properties(
      ${_thirdparty}
      PROPERTIES
        XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO
        XCODE_ATTRIBUTE_CLANG_WARN_DOCUMENTATION_COMMENTS NO
        COMPILE_WARNING_AS_ERROR OFF
    )
  endif()
endforeach()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Opus::opus ixwebsocket)

# --- Plugin sources ---
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin_main.cpp
    src/core/iar_packet.cpp
    src/core/settings.cpp
    src/core/opus_encoder.cpp
    src/net/ws_client.cpp
    src/obs/engine.cpp
    src/obs/settings_store.cpp
    src/ui/iar_dock.cpp
    src/ui/iar_settings_dialog.cpp
    src/third_party/qrcodegen.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

# Treat fetched third-party headers as system headers so their warnings do not
# trip our -Werror, and silence the few that still leak through include paths.
if(ixwebsocket_SOURCE_DIR)
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${ixwebsocket_SOURCE_DIR}")
endif()
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party")
if(MSVC)
  set_source_files_properties(src/third_party/qrcodegen.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(src/third_party/qrcodegen.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(NOT MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wno-newline-eof)
endif()

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE IAR_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
