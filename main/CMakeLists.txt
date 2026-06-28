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
    set(BENCH_FLAGS "-DSYNTH_BENCH=1")
else()
    set(BENCH_SRCS "")
    set(BENCH_FLAGS "")
endif()

idf_component_register(
	SRCS
		"main.c"
		"../app/app.c"
		"../engine/synth.c"
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
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-variable" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-const-variable" APPEND)

if(BENCH)
    idf_build_set_property(COMPILE_OPTIONS "${BENCH_FLAGS}" APPEND)
endif()
