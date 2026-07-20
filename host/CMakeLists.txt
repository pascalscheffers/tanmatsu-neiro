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
set(DAISYSP "${REPO_ROOT}/dsp/vendor/daisysp")

# WO-13-neiro-bank: host/test builds embed the Neiro factory JSON bank by
# wrapping the single source-of-truth file (engine/banks/neiro_factory.json)
# in a generated C++ raw-string-literal source (engine/factory_bank_embed.cpp.in).
# The device build instead uses ESP-IDF's EMBED_TXTFILES on the same .json
# (main/CMakeLists.txt + main/factory_bank_embed.cpp) — see engine/factory_bank.h.
# configure_file() tracks the .json as a configure-time dependency, so editing
# the bank automatically regenerates this file on the next build.
file(READ "${REPO_ROOT}/engine/banks/neiro_factory.json" NEIRO_FACTORY_JSON_CONTENT)
configure_file(
    "${REPO_ROOT}/engine/factory_bank_embed.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/factory_bank_embed.cpp"
    @ONLY)

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

# --- RtMidi (host-only; device uses a no-op stub in platform/device/) ---
pkg_check_modules(RTMIDI REQUIRED rtmidi)

# Stage 0.5: bench mode. Pass -DSYNTH_BENCH=ON to cmake (done by `make bench`).
option(SYNTH_BENCH "Build the Stage 0.5 CPU profiling harness" OFF)

# Device crackle diagnostics. Pass -DSYNTH_PROFILE=ON to enable audio-block
# profiling + 1 s console readout. -DSYNTH_FREEZE_DISPLAY=ON freezes the display
# after the first frame (less useful on host, but wired for consistency).
option(SYNTH_PROFILE "Enable audio-block cycle profiler + 1 s console readout" OFF)
option(SYNTH_FREEZE_DISPLAY "Freeze display after first frame (display-blit isolation)" OFF)

set(HOST_SRCS
    # Stage 1c: allocator + JunoModel + chorus added; synth.cpp drives the pool.
    "${REPO_ROOT}/engine/synth.cpp"
    "${REPO_ROOT}/engine/record_ring.cpp"
    "${REPO_ROOT}/engine/juno_voice.cpp"
    "${REPO_ROOT}/dsp/juno106_hpf.cpp"
    "${REPO_ROOT}/engine/mod_matrix.cpp"
    "${REPO_ROOT}/engine/juno_model.cpp"
    "${REPO_ROOT}/engine/voice_alloc.cpp"
    # Stage 2a: parameter table + store.
    "${REPO_ROOT}/engine/param_desc.cpp"
    "${REPO_ROOT}/engine/param_store.cpp"
    # Stage 2d: preset serialisation + factory bank.
    "${REPO_ROOT}/engine/preset.cpp"
    "${REPO_ROOT}/engine/user_bank.cpp"
    # WO-13-fmt: PresetPatch value object + JSON bank codec (ADR 0027).
    "${REPO_ROOT}/engine/bank_json.cpp"
    "${REPO_ROOT}/dsp/vendor/cjson/cJSON.c"
    # WO-13-neiro-bank: generated host embed of engine/banks/neiro_factory.json.
    "${CMAKE_CURRENT_BINARY_DIR}/generated/factory_bank_embed.cpp"
    # DaisySP modules (header-only: whitenoise, dsp wrappers).
    "${DAISYSP}/Source/Synthesis/oscillator.cpp"
    "${DAISYSP}/Source/Filters/svf.cpp"
    "${DAISYSP}/Source/Control/adsr.cpp"
    "${DAISYSP}/Source/Effects/chorus.cpp"
    "${DAISYSP}/Source/Utility/dcblock.cpp"
    # Stage 1d: musical-typing input.
    "${REPO_ROOT}/control/keyboard.c"
    "${REPO_ROOT}/control/wav_recorder.cpp"
    "${REPO_ROOT}/ui/ui.cpp"
    "${REPO_ROOT}/app/app.c"
    "${REPO_ROOT}/platform/audio_volume.c"
    "${REPO_ROOT}/platform/host/platform_host.c"
    "${REPO_ROOT}/platform/host/miniaudio_impl.c"
    "${REPO_ROOT}/platform/host/midi_host.cpp"
    "${REPO_ROOT}/control/midi_in.c"
    "${REPO_ROOT}/control/midi_router.c"
    "${REPO_ROOT}/control/sustain.c"
    "${REPO_ROOT}/ui/ui_presets.cpp"
    "${REPO_ROOT}/ui/ui_presets_state.cpp"
    "${REPO_ROOT}/ui/ui_overlay.cpp"
    "${REPO_ROOT}/ui/ui_icons.cpp"
    "${REPO_ROOT}/ui/ui_page_table.cpp"
    "${REPO_ROOT}/ui/ui_focus.cpp"
    "${REPO_ROOT}/ui/ui_dirty.cpp")

if(SYNTH_BENCH)
    list(APPEND HOST_SRCS "${REPO_ROOT}/engine/bench.c")
    list(APPEND HOST_SRCS "${REPO_ROOT}/engine/bench_blocks.cpp")
endif()

add_executable(tanmatsu-synth-host ${HOST_SRCS})

if(SYNTH_BENCH)
    target_compile_definitions(tanmatsu-synth-host PRIVATE SYNTH_BENCH=1)
endif()

if(SYNTH_PROFILE)
    target_compile_definitions(tanmatsu-synth-host PRIVATE SYNTH_PROFILE=1)
endif()

if(SYNTH_FREEZE_DISPLAY)
    target_compile_definitions(tanmatsu-synth-host PRIVATE SYNTH_FREEZE_DISPLAY=1)
endif()

target_include_directories(tanmatsu-synth-host PRIVATE
    # Repo root: resolves "dsp/osc.h", "engine/voice.h" etc.
    "${REPO_ROOT}"
    "${REPO_ROOT}/platform"
    "${REPO_ROOT}/platform/host"
    "${REPO_ROOT}/engine"
    "${REPO_ROOT}/ui"
    "${REPO_ROOT}/app"
    # WO-13-fmt: vendored cJSON header (host/test builds only; device uses the
    # ESP-IDF `json` component's copy — see dsp/vendor/cjson/cJSON.h).
    "${REPO_ROOT}/dsp/vendor/cjson"
    # DaisySP Source root: resolves "Synthesis/oscillator.h", "Utility/dsp.h",
    # "Noise/whitenoise.h" etc. from our dsp/ wrappers and juno_voice.
    "${DAISYSP}/Source"
    # DaisySP .cpp files use flat #include "dsp.h" (without "Utility/" prefix).
    "${DAISYSP}/Source/Utility"
    ${SDL2_INCLUDE_DIRS}
    ${RTMIDI_INCLUDE_DIRS})

target_link_directories(tanmatsu-synth-host PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${RTMIDI_LIBRARY_DIRS})

target_link_libraries(tanmatsu-synth-host PRIVATE
    pax_gfx
    ${SDL2_LIBRARIES}
    ${RTMIDI_LIBRARIES})

# miniaudio's backends: Core Audio on macOS, ALSA/pulse via dl on Linux.
if(APPLE)
    target_link_libraries(tanmatsu-synth-host PRIVATE
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreMIDI")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(tanmatsu-synth-host PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
