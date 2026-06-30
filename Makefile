# Serial port for flash/monitor. Machine-local override: put the device path in a
# gitignored `.PORT` file (e.g. /dev/tty.usbmodemXXXX on macOS). Falls back to the
# Linux default. Override on the CLI with `make … PORT=/dev/…` as usual.
PORT ?= $(shell cat .PORT 2>/dev/null || echo /dev/ttyACM0)

IDF_PATH ?= $(shell cat .IDF_PATH 2>/dev/null || echo `pwd`/esp-idf)
IDF_TOOLS_PATH ?= $(shell cat .IDF_TOOLS_PATH 2>/dev/null || echo `pwd`/esp-idf-tools)
IDF_BRANCH ?= v5.5.1
#IDF_COMMIT ?= aaebc374676621980878789c49d239232ea714c5
IDF_EXPORT_QUIET ?= 1
IDF_GITHUB_ASSETS ?= dl.espressif.com/github_assets
MAKEFLAGS += --silent

SHELL := /usr/bin/env bash

DEVICE ?= tanmatsu
# Stage 0.5: BENCH=1 compiles the CPU-profiling harness (engine/bench.c, -DSYNTH_BENCH)
# into the image. It builds into a separate dir and define so it never overwrites the
# shipping build, its cmake cache, or the normal app slot in AppFS.
ifeq ($(BENCH),1)
BUILD ?= build/$(DEVICE)-bench
BENCH_DEFINE := -DBENCH=1
else
BUILD ?= build/$(DEVICE)
BENCH_DEFINE :=
endif
# Stage 5b-i: USBHOST_DEBUG=1 injects -DSYNTH_USB_HOST_DEBUG, which swaps
# platform_init() to call midi_usb_host_init() (USB-A host spike) instead of
# midi_usb_device_init() (Stage 5d USB-C device). This keeps the USB-C Serial/JTAG
# console alive so enumeration and packet logs are visible via `make sniff`.
# Builds into a separate dir so it never overwrites the normal build cache.
ifeq ($(USBHOST_DEBUG),1)
override BUILD := build/$(DEVICE)-usbhost
USBHOST_DEFINE := -DSYNTH_USB_HOST_DEBUG=1
else
USBHOST_DEFINE :=
endif
# Device crackle diagnostics:
#   PROFILE=1          — audio-block cycle profiler + 1 s console readout.
#   FREEZE_DISPLAY=1   — freeze the display after the first frame (isolates
#                        display-blit contention from audio compute).
# Both flags are no-ops in the shipping image (all code is under #ifdef).
# NOTE: unlike BENCH/USBHOST_DEBUG, these two share the default build/$(DEVICE)
# dir with the shipping build, so they MUST be passed with an explicit 0/1 every
# time — omitting the -D leaves the previous value stale in CMakeCache.txt and
# the flag stays "on" until `make clean`. CMake's if(VAR) treats "0" as false.
PROFILE_DEFINE      := -DPROFILE=$(if $(filter 1,$(PROFILE)),1,0)
FREEZE_DEFINE       := -DFREEZE_DISPLAY=$(if $(filter 1,$(FREEZE_DISPLAY)),1,0)
FAT ?= 0
SDKCONFIG_DEFAULTS ?= sdkconfigs/general;sdkconfigs/$(DEVICE)
SDKCONFIG ?= sdkconfig_$(DEVICE)

####

# Set IDF_TARGET based on device name

ifeq ($(DEVICE), tanmatsu)
IDF_TARGET ?= esp32p4
else ifeq ($(DEVICE), konsool)
IDF_TARGET ?= esp32p4
else ifeq ($(DEVICE), esp32-p4-function-ev-board)
IDF_TARGET ?= esp32p4
else ifeq ($(DEVICE), mch2022)
IDF_TARGET ?= esp32
else ifeq ($(DEVICE), kami)
IDF_TARGET ?= esp32
else ifeq ($(DEVICE), hackerhotel-2024)
IDF_TARGET ?= esp32c6
else ifeq ($(DEVICE), heltecv3)
IDF_TARGET ?= esp32s3
else
$(warning "Unknown device, defaulting to ESP32 $(DEVICE)")
IDF_TARGET ?= esp32
endif

IDF_PARAMS := -B $(BUILD) build -DDEVICE=$(DEVICE) -DSDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" -DSDKCONFIG=$(SDKCONFIG) -DIDF_TARGET=$(IDF_TARGET) -DFAT=$(FAT) $(BENCH_DEFINE) $(USBHOST_DEFINE) $(PROFILE_DEFINE) $(FREEZE_DEFINE)

#####

export IDF_TOOLS_PATH
export IDF_GITHUB_ASSETS

# General targets

.PHONY: all
all: build

# Badgelink — upload + launch an app over USB into the launcher's AppFS partition.
# This is the fast dev loop: it does NOT replace the launcher firmware (cf. `make flash`).
# Prereqs: run `make badgelink` once (clones the tool), and have the device sitting in
# the launcher. Docs: https://docs.tanmatsu.cloud/software/badgelink/ and /software/appfs/.
.PHONY: badgelink
badgelink:
	rm -rf badgelink
	git clone https://github.com/badgeteam/esp32-component-badgelink.git badgelink
	# Upstream install.sh runs `python -m venv`, but macOS only ships `python3` — it dies
	# with "python: command not found". Create the venv ourselves with python3; badgelink.sh
	# then sees .venv and skips install.sh, and its own `python` call works from inside the
	# activated venv. Upstream candidate logged in specs/07-upstream-contributions.md.
	cd badgelink/tools; python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt

# AppFS app identity. Override APP_SLUG/APP_TITLE to install side-by-side variants
# (e.g. the CPU bench below installs under its own slug so the synth app stays put).
APP_SLUG  ?= neiro
APP_TITLE ?= Neiro Virtual Analog Synthesizer
APP_VER   ?= 0

.PHONY: install
install: build
	cd badgelink/tools; ./badgelink.sh appfs upload $(APP_SLUG) "$(APP_TITLE)" $(APP_VER) ../../$(BUILD)/application.bin

.PHONY: run
run:
	cd badgelink/tools; ./badgelink.sh start $(APP_SLUG)

# Stage 0.5: build the CPU-profiling image, push it into AppFS, and launch it — all
# without touching the launcher firmware. badgelink can't capture the console, so the
# bench's printf table comes out over UART: run `make monitor BENCH=1` in another
# terminal (started before `bench-run`, it reconnects across the launch-reboot).
BENCH_SLUG  ?= synthbench
BENCH_TITLE ?= Synth CPU bench

.PHONY: bench-upload
bench-upload:
	$(MAKE) install BENCH=1 APP_SLUG=$(BENCH_SLUG) APP_TITLE="$(BENCH_TITLE)"

.PHONY: bench-run
bench-run:
	$(MAKE) run APP_SLUG=$(BENCH_SLUG)

.PHONY: bench-device
bench-device: bench-upload bench-run
	@echo ">>> Bench launched via AppFS — launcher firmware untouched."
	@echo ">>> Capture the table with:  make sniff   (reads ALL usbmodem ports)"

# Sniff every /dev/cu.usbmodem* at once — the Tanmatsu exposes multiple serial
# interfaces (P4 host + C6 radio) whose numbers shift across the launch-reboot,
# so this avoids guessing the port. Tees to build/<dev>-bench/console.log.
# Run this in one terminal, then `make bench-device` in another.
.PHONY: sniff
sniff:
	mkdir -p $(BUILD)
	python3 tools/sniff-console.py --log $(BUILD)/console.log

# Preparation

.PHONY: prepare
prepare: submodules sdk

.PHONY: submodules
submodules: 
	if [ ! -f .submodules_update_done ]; then \
		echo "Updating submodules"; \
		git submodule update --init --recursive; \
		touch .submodules_update_done; \
	fi

.PHONY: sdk
sdk:
	if test -d "$(IDF_PATH)"; then echo -e "ESP-IDF target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	if test -d "$(IDF_TOOLS_PATH)"; then echo -e "ESP-IDF tools target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	git clone --recursive --branch "$(IDF_BRANCH)" https://github.com/espressif/esp-idf.git "$(IDF_PATH)" --depth=1 --shallow-submodules
#	cd "$(IDF_PATH)"; git fetch origin "$(IDF_COMMIT)" --recurse-submodules || true
#	cd "$(IDF_PATH)"; git checkout "$(IDF_COMMIT)"
	cd "$(IDF_PATH)"; git submodule update --init --recursive
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: reinstallsdk
reinstallsdk:
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: removesdk
removesdk:
	rm -rf "$(IDF_PATH)"
	rm -rf "$(IDF_TOOLS_PATH)"

.PHONY: refreshsdk
refreshsdk: removesdk sdk

.PHONY: menuconfig
menuconfig:
	source "$(IDF_PATH)/export.sh" && idf.py menuconfig -DDEVICE=$(DEVICE) -DSDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" -DSDKCONFIG=$(SDKCONFIG) -DIDF_TARGET=$(IDF_TARGET)
	
# Cleaning

.PHONY: clean
clean:
	rm -rf $(BUILD)
	rm -f .submodules_update_done

.PHONY: fullclean
fullclean: clean
	rm -f sdkconfig
	rm -f sdkconfig.old
	rm -f sdkconfig.ci
	rm -f sdkconfig.defaults

# Check if build environment is set up correctly
.PHONY: checkbuildenv
checkbuildenv:
	if [ -z "$(IDF_PATH)" ]; then echo "IDF_PATH is not set!"; exit 1; fi
	if [ -z "$(IDF_TOOLS_PATH)" ]; then echo "IDF_TOOLS_PATH is not set!"; exit 1; fi
	# Check if the IDF commit id the one we need
	#if [ -d "$(IDF_PATH)" ]; then \
	#	if [ "$(IDF_COMMIT)" != "$(shell cd $(IDF_PATH); git rev-parse HEAD)" ]; then \
	#		echo "ESP-IDF commit id does not match! Expected '$(IDF_COMMIT)' got '$(shell git rev-parse HEAD)'"; \
	#		echo "Run $ make refreshsdk"; \
	#		echo "To update the ESP-IDF to the correct commit id"; \
	#		echo "Or set the IDF_COMMIT variable in the Makefile to the correct commit id"; \
	#		exit 1; \
	#	fi; \
	#fi

# Building

.PHONY: build
build: icons checkbuildenv submodules patches
	source "$(IDF_PATH)/export.sh" >/dev/null && idf.py $(IDF_PARAMS)

# Upstream patches — fixes to dependencies (PAX, BSP, …) tracked in
# upstream-patches/ and re-applied to the gitignored managed_components/ tree.
# Idempotent; safe to run before every build. See specs/07-upstream-contributions.md.
.PHONY: patches
patches:
	./tools/apply-upstream-patches.sh

# Host (desktop simulator) build — same upper layers, SDL2 + miniaudio backend.
# See host/CMakeLists.txt; configures into build-host/.

HOST_BUILD ?= build-host

.PHONY: host
host: patches
	cmake -S host -B $(HOST_BUILD)
	cmake --build $(HOST_BUILD) -j

.PHONY: host-run
host-run: host
	./$(HOST_BUILD)/tanmatsu-synth-host

.PHONY: host-clean
host-clean:
	rm -rf $(HOST_BUILD)

# Stage 0.5 CPU benchmark — builds the host binary with SYNTH_BENCH=1 and runs it.
# Pass BENCH_BUILD=build-bench to keep it separate from the normal host build.
BENCH_BUILD ?= build-bench

.PHONY: bench
bench: patches
	cmake -S host -B $(BENCH_BUILD) -DSYNTH_BENCH=ON
	cmake --build $(BENCH_BUILD) -j
	./$(BENCH_BUILD)/tanmatsu-synth-host

.PHONY: bench-clean
bench-clean:
	rm -rf $(BENCH_BUILD)

# Host DSP tests — pure dsp/ layer, no platform, FTZ-off (ADR 0012).
# See tests/host/CMakeLists.txt.
TEST_BUILD ?= build-test

.PHONY: test
test: patches
	cmake -S tests/host -B $(TEST_BUILD)
	cmake --build $(TEST_BUILD) -j
	./$(TEST_BUILD)/tanmatsu-tests

.PHONY: test-clean
test-clean:
	rm -rf $(TEST_BUILD)

# Hardware

.PHONY: flash
flash: build
	source "$(IDF_PATH)/export.sh" && \
	idf.py $(IDF_PARAMS) flash -p $(PORT)

.PHONY: flashmonitor
flashmonitor: build
	source "$(IDF_PATH)/export.sh" && \
	idf.py $(IDF_PARAMS) flash -p $(PORT) monitor

.PHONY: prepappfs
prepappfs:
	source "$(IDF_PATH)/export.sh" && \
	python3 managed_components/badgeteam__appfs/tools/appfs_generate.py \
	8192000 \
	appfs.bin

.PHONY: appfs
appfs:
	source "$(IDF_PATH)/export.sh" && \
	esptool.py \
		-b 921600 --port $(PORT) \
		write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
		0x330000 appfs.bin

.PHONY: erase
erase:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) erase-flash -p $(PORT)

.PHONY: monitor
monitor:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) monitor -p $(PORT)

.PHONY: openocd
openocd:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) openocd

.PHONY: openocdftdi
openocdftdi:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) openocd --openocd-commands "-f board/esp32p4-ftdi.cfg"

.PHONY: gdb
gdb:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) gdb

.PHONY: gdbgui
gdbgui:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) gdbgui

.PHONY: gdbtui
gdbtui:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) gdbtui

# Tools

.PHONY: size
size:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) size

.PHONY: size-components
size-components:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) size-components

.PHONY: size-files
size-files:
	source "$(IDF_PATH)/export.sh" && idf.py $(IDF_PARAMS) size-files

.PHONY: efuse
efuse:
	$(IDF_PATH)/components/efuse/efuse_table_gen.py --idf_target esp32p4 $(IDF_PATH)/components/efuse/esp32p4/esp_efuse_table.csv main/esp_efuse_custom_table.csv

# Formatting
# Canonical tool: clang-format 21. Resolve from PATH, else fall back to the macOS
# toolchain (xcrun). Override with `make format CLANG_FORMAT=/path/to/clang-format`.
# Covers all hand-written source (engine/dsp/control/ui/app/platform/main/tests);
# vendored trees (dsp/vendor, platform/host/miniaudio.h) are excluded.
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || echo 'xcrun clang-format')

.PHONY: format
format:
	find main/ engine/ dsp/ control/ ui/ app/ platform/ tests/ \
		\( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) \
		-not -path 'platform/host/miniaudio.h' -not -path 'dsp/vendor/*' \
		| xargs $(CLANG_FORMAT) -i

# Re-compile protobuf files
# If you are an end user, you do not need to run this;
# the output files are already there in the repository.

.PHONY: compile-protobuf
compile-protobuf:
	protoc --pyi_out=tools --python_out=tools badgelink.proto
	python3 main/badgelink/nanopb/generator/nanopb_generator.py -D main/badgelink -f badgelink.options badgelink.proto

# Take all svg files from main/static/icons and put them in main/fat/icons as png using tools/connvert.sh
ICONS_SRC := $(wildcard main/static/icons/*.svg)
ICONS_DST := $(patsubst main/static/icons/%.svg,main/fat/icons/%.png,$(ICONS_SRC))

.PHONY: icons
icons: $(ICONS_DST)

main/fat/icons/%.png: main/static/icons/%.svg
	mkdir -p main/fat/icons
	tools/convert.sh $< $@
	
# Build all targets
.PHONY: buildall
buildall:
	$(MAKE) build DEVICE=tanmatsu
	$(MAKE) build DEVICE=konsool
	$(MAKE) build DEVICE=hackerhotel-2026
	$(MAKE) build DEVICE=esp32-p4-function-ev-board
	$(MAKE) build DEVICE=mch2022
	$(MAKE) build DEVICE=hackerhotel-2024

# Flash all: assumes Tanmatsu P4 is /dev/ttyACM0, C6 is /dev/ttyACM1 and MCH2022 badge is /dev/ttyACM2
.PHONY: flashall
flashall:
	$(MAKE) flash DEVICE=tanmatsu PORT=/dev/ttyACM0
	$(MAKE) flash DEVICE=mch2022 PORT=/dev/ttyACM2

# Vscode
.PHONY: vscode
vscode:
	rm -rf .vscode
	cp -r .vscode.template .vscode
