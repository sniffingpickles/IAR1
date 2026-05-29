# Standalone build for the OBS-independent core unit tests. This project does
# NOT depend on libobs or Qt, only on libopus, so it can be built and run on any
# developer machine and in CI:
#
#   cmake -S obs-plugin/test -B build_coretest
#   cmake --build build_coretest
#   ctest --test-dir build_coretest --output-on-failure
#
# The main plugin CMakeLists also adds these tests when building with OBS deps.
cmake_minimum_required(VERSION 3.20)
project(iar_core_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate Opus via its CMake config (vcpkg/Windows) first, then pkg-config
# (Homebrew/apt). Either path yields an importable include path + link target.
find_package(Opus CONFIG QUIET)

add_executable(iar_core_tests
  core_tests.cpp
  ../src/core/iar_packet.cpp
  ../src/core/settings.cpp
  ../src/core/opus_encoder.cpp
)

if(TARGET Opus::opus)
  target_link_libraries(iar_core_tests PRIVATE Opus::opus)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(OPUS REQUIRED opus)
  target_include_directories(iar_core_tests PRIVATE ${OPUS_INCLUDE_DIRS})
  target_link_directories(iar_core_tests PRIVATE ${OPUS_LIBRARY_DIRS})
  target_link_libraries(iar_core_tests PRIVATE ${OPUS_LIBRARIES})
endif()

if(NOT MSVC)
  target_compile_options(iar_core_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME core_tests COMMAND iar_core_tests)
