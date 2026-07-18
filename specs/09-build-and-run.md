# Build, Flash, Run — full command catalog

The everyday commands live in `CLAUDE.md`; this is the complete reference (device flashing,
the AppFS fast loop, serial capture, bench). `DEVICE=tanmatsu` is the default target. Run
from the repo root.

## Full firmware flash (overwrites the launcher)

- `make prepare` — one-time: clone ESP-IDF v5.5.1 + toolchain into `./esp-idf(-tools)`.
- `make build` — build the app.
- `make flash PORT=/dev/tty.usbmodemXXXX` — full firmware flash over USB (overwrites the
  launcher; use for a clean image or when AppFS is unavailable).
- `make flashmonitor PORT=…` — flash + serial monitor.
- `make menuconfig` — sdkconfig editor.

## Fast dev loop — AppFS, no firmware replace (prefer this)

The shipped launcher keeps a dedicated AppFS partition;
[badgelink](https://docs.tanmatsu.cloud/software/badgelink/) uploads an app binary into it over
USB and launches it, leaving the launcher firmware untouched (you drop back into the launcher
when the app exits). Much faster than a full flash — the default for iterating. One-time:
`make badgelink` (clones the tool; on Linux also install its udev rules). The device must be in
**USB mode** for badgelink to find it (launcher home screen → press the purple diamond; a USB
icon appears top-right). Then:

- `make install` — build + upload the synth into AppFS (slug `synth`).
- `make run` — launch it.
- Override `APP_SLUG`/`APP_TITLE` to install variants side-by-side without clobbering a slot.

## Serial capture (console is USB-Serial-JTAG)

badgelink does **not** capture the console — output (e.g. the bench table) comes over USB
serial. The Tanmatsu exposes **two** serial interfaces (P4 host + C6 radio) whose device
numbers shift across reboots, and the console is **USB-Serial-JTAG** (block-buffers stdout when
not a TTY — `printf` needs `setvbuf`/`fflush` or it stays invisible while `ESP_LOG` shows). Use
`make sniff` (reads all `/dev/cu.usbmodem*` at once, labeled) rather than guessing a port.
AppFS partition details: https://docs.tanmatsu.cloud/software/appfs/.

## Bench, size, format

- `make bench-device` — Stage 0.5: build `BENCH=1`, upload under the `synthbench` slug, and
  launch the CPU bench via AppFS (synth slot untouched). Capture with `make sniff`.
- `make size` / `make size-components` — track flash/RAM budget (do this often).
- `make format` — clang-format the tree.
- **6-voice PROFILE baseline (Stage 13-baseline, ADR 0027):** before dropping
  `kNumVoices` from 8 to 6, build `PROFILE=1` and capture the worst-block render cost and
  `sizeof(JunoVoice)` at the *current* 8-voice count, with the display quiescent (a live
  display blit otherwise masks the number — see `[[ipc-collapse-rt-spikes]]`). Record both
  in `specs/MEMORY.md` as the split-if reference for the Stage 13 fidelity work (13c/13e),
  then set `kNumVoices = 6`.

## Host-side DSP tests

The `dsp/` layer is pure, so it compiles and runs on the Mac. `make host` builds/runs the host
target; `make test` runs the host DSP unit tests (`tests/host/`, stood up in Stage 1a). Host
tests run **FTZ-off** to match the device (ADR 0012).
