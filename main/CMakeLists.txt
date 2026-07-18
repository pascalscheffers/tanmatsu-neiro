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
    set(BENCH_SRCS "../engine/bench.c" "../engine/bench_blocks.cpp")
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
		"../engine/record_ring.cpp"
		"../engine/juno_voice.cpp"
		"../engine/mod_matrix.cpp"
		"../engine/juno_model.cpp"
		"../engine/voice_alloc.cpp"
		# Stage 2a: parameter table + store.
		"../engine/param_desc.cpp"
		"../engine/param_store.cpp"
		# Stage 2d: preset serialisation + factory bank.
		"../engine/preset.cpp"
		# WO-13-fmt: PresetPatch value object + JSON bank codec (ADR 0027).
		"../engine/bank_json.cpp"
		# DaisySP modules.
		"${DAISYSP}/Source/Synthesis/oscillator.cpp"
		"${DAISYSP}/Source/Filters/svf.cpp"
		"${DAISYSP}/Source/Control/adsr.cpp"
		"${DAISYSP}/Source/Effects/chorus.cpp"
		"${DAISYSP}/Source/Utility/dcblock.cpp"
		# Stage 1d: musical-typing input.
		"../control/keyboard.c"
		"../control/wav_recorder.cpp"
		# Stage 5a: MIDI parser + router (device uses no-op platform_midi_read stub).
		"../control/midi_in.c"
		"../control/midi_router.c"
		"../control/sustain.c"
		"../ui/ui.cpp"
		"../ui/ui_presets.cpp"
		"../ui/ui_presets_state.cpp"
		"../ui/ui_overlay.cpp"
		"../ui/ui_icons.cpp"
		"../ui/ui_page_table.cpp"
		"../ui/ui_focus.cpp"
		"../ui/ui_dirty.cpp"
		"../platform/audio_volume.c"
		"../platform/device/platform_device.c"
		"../platform/device/midi_usb_device.c"
		"../platform/device/midi_usb_host.c"
		${BENCH_SRCS}
	PRIV_REQUIRES
		esp_lcd
		esp_driver_i2s
		esp_driver_sdmmc
		esp_timer
		fatfs
		nvs_flash
		bt
		badge-bsp
		tanmatsu-wifi
		usb
		json
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
	# Stage 8: per-voice DSP (oscillator/svf/adsr) → IRAM (completes ADR 0013,
	# kills the flash-XIP I-cache stall behind the poly smash-crackle).
	LDFRAGMENTS
		"linker_audio.lf"
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-variable" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-const-variable" APPEND)

if(BENCH)
    foreach(flag ${BENCH_FLAGS})
        idf_build_set_property(COMPILE_OPTIONS "${flag}" APPEND)
    endforeach()
endif()

# Stage 5b-i: USBHOST_DEBUG=1 (make build USBHOST_DEBUG=1) — injects
# -DSYNTH_USB_HOST_DEBUG so platform_init() routes to midi_usb_host_init()
# instead of midi_usb_device_init(), keeping the USB-C console alive.
if(SYNTH_USB_HOST_DEBUG)
    idf_build_set_property(COMPILE_OPTIONS "-DSYNTH_USB_HOST_DEBUG=1" APPEND)
endif()

# Device crackle diagnostics (make build PROFILE=1 / FREEZE_DISPLAY=1):
#   PROFILE=1         — audio-block cycle profiler + 1 s console readout.
#   FREEZE_DISPLAY=1  — freeze display after first frame (isolates blit contention).
# All new runtime code is under #ifdef; shipping image is unaffected.
if(PROFILE)
    idf_build_set_property(COMPILE_OPTIONS "-DSYNTH_PROFILE=1" APPEND)
endif()
if(FREEZE_DISPLAY)
    idf_build_set_property(COMPILE_OPTIONS "-DSYNTH_FREEZE_DISPLAY=1" APPEND)
endif()
if(QUIET_DISPLAY)
    idf_build_set_property(COMPILE_OPTIONS "-DSYNTH_QUIET_DISPLAY=1" APPEND)
endif()
