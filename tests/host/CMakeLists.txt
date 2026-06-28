# tests/host/CMakeLists.txt — host DSP test suite.
#
# Compiles a subset of dsp/vendor/daisysp and runs tests on the Mac.
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
    "${DAISYSP}/Source/Synthesis/oscillator.cpp"
)

target_include_directories(tanmatsu-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DAISYSP}/Source"
    "${DAISYSP}/Source/Synthesis"
    "${DAISYSP}/Source/Utility"
)

# Explicitly exclude -ffast-math to keep FTZ disabled (ADR 0012).
# -O2 matches the device shipping build (Stage 0.5 -O2 confirmation).
target_compile_options(tanmatsu-tests PRIVATE -O2 -fno-fast-math)
