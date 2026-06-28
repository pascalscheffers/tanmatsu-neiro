# host/CMakeLists.txt — desktop build of the Tanmatsu synth.
#
# Compiles the SAME portable upper layers (engine/ ui/ app/) as the device IDF
# build, plus the host HAL backend (SDL2 + miniaudio) and PAX built host-side.
# Configure into build-host/ (see the Makefile `host` target).
cmake_minimum_required(VERSION 3.16)
project(tanmatsu_synth_host C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Repo root is one level up from host/.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# --- PAX (built host-side, static so its GNU-ld link flags never reach ld64) ---
set(PAX_STATIC true)
# EXCLUDE_FROM_ALL: build only the targets we actually link (pax_gfx). The gui
# target (pax_gui) is unused in Stage 0 and pulls ESP-only deps, so we never
# build it.
add_subdirectory(
    "${REPO_ROOT}/managed_components/robotman2412__pax-gfx"
    "${CMAKE_CURRENT_BINARY_DIR}/pax-gfx"
    EXCLUDE_FROM_ALL)
# PAX's macOS/BSD host portability comes from upstream-patches/pax-graphics/
# (applied by tools/apply-upstream-patches.sh, run via `make patches`/`make host`).

# --- SDL2 (dev-host dependency; never shipped to the device) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Stage 0.5: bench mode. Pass -DSYNTH_BENCH=ON to cmake (done by `make bench`).
option(SYNTH_BENCH "Build the Stage 0.5 CPU profiling harness" OFF)

set(HOST_SRCS
    "${REPO_ROOT}/engine/synth.c"
    "${REPO_ROOT}/ui/ui.c"
    "${REPO_ROOT}/app/app.c"
    "${REPO_ROOT}/platform/host/platform_host.c"
    "${REPO_ROOT}/platform/host/miniaudio_impl.c")

if(SYNTH_BENCH)
    list(APPEND HOST_SRCS "${REPO_ROOT}/engine/bench.c")
endif()

add_executable(tanmatsu-synth-host ${HOST_SRCS})

if(SYNTH_BENCH)
    target_compile_definitions(tanmatsu-synth-host PRIVATE SYNTH_BENCH=1)
endif()

target_include_directories(tanmatsu-synth-host PRIVATE
    "${REPO_ROOT}/platform"
    "${REPO_ROOT}/platform/host"
    "${REPO_ROOT}/engine"
    "${REPO_ROOT}/ui"
    "${REPO_ROOT}/app"
    ${SDL2_INCLUDE_DIRS})

target_link_directories(tanmatsu-synth-host PRIVATE ${SDL2_LIBRARY_DIRS})

target_link_libraries(tanmatsu-synth-host PRIVATE
    pax_gfx
    ${SDL2_LIBRARIES})

# miniaudio's backends: Core Audio on macOS, ALSA/pulse via dl on Linux.
if(APPLE)
    target_link_libraries(tanmatsu-synth-host PRIVATE
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(tanmatsu-synth-host PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
