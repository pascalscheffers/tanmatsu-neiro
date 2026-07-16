# tests/host/CMakeLists.txt — host DSP test suite.
#
# Compiles dsp/ wrappers, engine/juno_voice, and runs all host DSP tests.
# Configure into build-test/ (see the Makefile `test` target).
# FTZ-off: no -ffast-math so denormals behave as on the device (ADR 0012).
cmake_minimum_required(VERSION 3.16)
project(tanmatsu_tests C CXX)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DAISYSP "${REPO_ROOT}/dsp/vendor/daisysp")

add_executable(tanmatsu-tests
    main.cpp
    test_midi_parse.cpp
    test_osc.cpp
    test_osc_waveform.cpp
    test_voice.cpp
    test_alloc.cpp
    test_command_queue.cpp
    test_record_ring.cpp
    test_note_cap.cpp
    test_param_store.cpp
    test_saturate.cpp
    test_dcblock.cpp
    test_limiter.cpp
    test_preset.cpp
    test_mod_sources.cpp
    test_mod_matrix.cpp
    test_params.cpp
    test_clock.cpp
    test_scheduler.cpp
    test_arp.cpp
    test_arp_clock.cpp
    test_sustain.cpp
    # mono+unison voice-cap regression tests.
    test_alloc_unison_mono.cpp
    # WO-3: preset browser state-machine tests (no PAX dependency).
    test_ui_presets.cpp
    "${REPO_ROOT}/ui/ui_presets_state.cpp"
    # Launchkey follow: CC-driven page/row focus (no PAX dependency).
    test_ui_focus.cpp
    "${REPO_ROOT}/ui/ui_focus.cpp"
    "${REPO_ROOT}/ui/ui_page_table.cpp"
    "${REPO_ROOT}/control/sustain.c"
    "${DAISYSP}/Source/Synthesis/oscillator.cpp"
    "${DAISYSP}/Source/Filters/svf.cpp"
    "${DAISYSP}/Source/Control/adsr.cpp"
    "${DAISYSP}/Source/Effects/chorus.cpp"
    "${DAISYSP}/Source/Utility/dcblock.cpp"
    "${REPO_ROOT}/engine/juno_voice.cpp"
    "${REPO_ROOT}/engine/mod_matrix.cpp"
    "${REPO_ROOT}/engine/juno_model.cpp"
    "${REPO_ROOT}/engine/voice_alloc.cpp"
    "${REPO_ROOT}/engine/param_desc.cpp"
    "${REPO_ROOT}/engine/param_store.cpp"
    "${REPO_ROOT}/engine/preset.cpp"
    "${REPO_ROOT}/engine/record_ring.cpp"
    "${REPO_ROOT}/control/midi_in.c"
)

target_include_directories(tanmatsu-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # Repo root: resolves "dsp/osc.h", "engine/juno_voice.h" etc.
    "${REPO_ROOT}"
    # Engine layer (juno_voice.h includes voice.h by short name).
    "${REPO_ROOT}/engine"
    # Platform layer (platform.h, used by ui_presets_state.h).
    "${REPO_ROOT}/platform"
    # PAX headers (types only; pax_buf_t used in ui.h).
    "${REPO_ROOT}/managed_components/robotman2412__pax-gfx/core/include"
    # DaisySP Source root: resolves "Synthesis/oscillator.h",
    # "Filters/svf.h", "Utility/dsp.h", "Noise/whitenoise.h" etc.
    "${DAISYSP}/Source"
    # DaisySP .cpp files use flat #include "dsp.h" (without "Utility/" prefix).
    "${DAISYSP}/Source/Utility"
)

# Explicitly exclude -ffast-math to keep FTZ disabled (ADR 0012).
# -O2 matches the device shipping build (Stage 0.5 -O2 confirmation).
target_compile_options(tanmatsu-tests PRIVATE -O2 -fno-fast-math)
