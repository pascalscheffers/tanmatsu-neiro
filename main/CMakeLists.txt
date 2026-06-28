idf_build_get_property(idf_target IDF_TARGET)
if("${idf_target}" STREQUAL "esp32p4")
list(APPEND extra_requires esp-hosted-tanmatsu)
endif()


# Portable upper layers (shared verbatim with the host build) plus the device
# HAL backend. Sources live outside main/ so the host CMake build compiles the
# exact same files — one source tree, two build descriptions.
# Stage 0.5: pass BENCH=1 on the cmake command line (done by `make build BENCH=1`)
# to compile the profiling harness into the device image.
if(BENCH)
    set(BENCH_SRCS "../engine/bench.c")
    # INTERACTIVE: on device the console can't be attached at AppFS launch (USB-C
    # is muxed for badgelink), so the bench waits for a keypress and exits to the
    # launcher when done. The host bench (host/CMakeLists.txt) sets only
    # SYNTH_BENCH so `make bench` still runs unattended.
    #
    # -O2: the project default is -Og (IDF debug); the bench measures DSP cost, so
    # it builds at -O2 to be representative of the shipping synth. Appended after
    # the global -Og in COMPILE_OPTIONS, so the last -O wins. Bench build only;
    # the normal image's optimization is unchanged (sdkconfig still DEBUG).
    set(BENCH_FLAGS "-DSYNTH_BENCH=1" "-DSYNTH_BENCH_INTERACTIVE=1" "-O2")
else()
    set(BENCH_SRCS "")
    set(BENCH_FLAGS "")
endif()

set(DAISYSP "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/vendor/daisysp")

idf_component_register(
	SRCS
		"main.c"
		"../app/app.c"
		# Stage 1c: allocator + JunoModel + chorus; synth.cpp drives the pool.
		"../engine/synth.cpp"
		"../engine/juno_voice.cpp"
		"../engine/juno_model.cpp"
		"../engine/voice_alloc.cpp"
		# Stage 2a: parameter table + store.
		"../engine/param_desc.cpp"
		"../engine/param_store.cpp"
		# DaisySP modules.
		"${DAISYSP}/Source/Synthesis/oscillator.cpp"
		"${DAISYSP}/Source/Filters/svf.cpp"
		"${DAISYSP}/Source/Control/adsr.cpp"
		"${DAISYSP}/Source/Effects/chorus.cpp"
		# Stage 1d: musical-typing input.
		"../control/keyboard.c"
		"../ui/ui.c"
		"../platform/device/platform_device.c"
		${BENCH_SRCS}
	PRIV_REQUIRES
		esp_lcd
		esp_driver_i2s
		esp_timer
		fatfs
		nvs_flash
		bt
		badge-bsp
		tanmatsu-wifi
		${extra_requires}
	INCLUDE_DIRS
		"."
		"../platform"
		"../engine"
		"../ui"
		"../app"
		# Repo root: resolves "dsp/osc.h", "engine/voice.h" etc.
		".."
		# DaisySP Source root: resolves "Synthesis/oscillator.h",
		# "Utility/dsp.h", "Noise/whitenoise.h" etc.
		"${DAISYSP}/Source"
		# DaisySP .cpp files use flat #include "dsp.h" (without "Utility/" prefix).
		"${DAISYSP}/Source/Utility"
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-variable" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-const-variable" APPEND)

if(BENCH)
    foreach(flag ${BENCH_FLAGS})
        idf_build_set_property(COMPILE_OPTIONS "${flag}" APPEND)
    endforeach()
endif()
