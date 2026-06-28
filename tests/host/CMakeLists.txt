# tests/host/CMakeLists.txt — host DSP test suite.
#
# Compiles dsp/ wrappers, engine/juno_voice, and runs all host DSP tests.
# Configure into build-test/ (see the Makefile `test` target).
# FTZ-off: no -ffast-math so denormals behave as on the device (ADR 0012).
cmake_minimum_required(VERSION 3.16)
project(tanmatsu_tests CXX)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DAISYSP "${REPO_ROOT}/dsp/vendor/daisysp")

add_executable(tanmatsu-tests
    main.cpp
    test_osc.cpp
    test_voice.cpp
    test_alloc.cpp
    test_command_queue.cpp
    test_param_store.cpp
    test_saturate.cpp
    test_preset.cpp
    "${DAISYSP}/Source/Synthesis/oscillator.cpp"
    "${DAISYSP}/Source/Filters/svf.cpp"
    "${DAISYSP}/Source/Control/adsr.cpp"
    "${DAISYSP}/Source/Effects/chorus.cpp"
    "${REPO_ROOT}/engine/juno_voice.cpp"
    "${REPO_ROOT}/engine/juno_model.cpp"
    "${REPO_ROOT}/engine/voice_alloc.cpp"
    "${REPO_ROOT}/engine/param_desc.cpp"
    "${REPO_ROOT}/engine/param_store.cpp"
    "${REPO_ROOT}/engine/preset.cpp"
)

target_include_directories(tanmatsu-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # Repo root: resolves "dsp/osc.h", "engine/juno_voice.h" etc.
    "${REPO_ROOT}"
    # Engine layer (juno_voice.h includes voice.h by short name).
    "${REPO_ROOT}/engine"
    # DaisySP Source root: resolves "Synthesis/oscillator.h",
    # "Filters/svf.h", "Utility/dsp.h", "Noise/whitenoise.h" etc.
    "${DAISYSP}/Source"
    # DaisySP .cpp files use flat #include "dsp.h" (without "Utility/" prefix).
    "${DAISYSP}/Source/Utility"
)

# Explicitly exclude -ffast-math to keep FTZ disabled (ADR 0012).
# -O2 matches the device shipping build (Stage 0.5 -O2 confirmation).
target_compile_options(tanmatsu-tests PRIVATE -O2 -fno-fast-math)
