# Hardware Reference — Tanmatsu (what matters for the synth)

Source: <https://docs.tanmatsu.cloud/hardware/specifications/>, Hackaday/CNX coverage,
and the `tanmatsu-template` BSP. Verify codec/USB details against the BSP headers once
the build env is up (the build-env agent is reporting these).

## Compute
- **ESP32-P4NRW32**: dual-core 32-bit RISC-V @ up to 400 MHz, **single-precision FPU**,
  128-bit SIMD/DSP ("PIE") vector extension — usable for audio DSP.
- **32 MB PSRAM** (octal, ~200 MHz) + **768 KB internal SRAM** + 16 MB flash.
  - Internal SRAM is fast and scarce → audio hot path lives here.
  - PSRAM is large but higher-latency → wavetables/samples/UI buffers.
- Coprocessor **CH32V203** (power, LEDs, keyboard scan). Radio **ESP32-C6** (WiFi/BT/
  802.15.4) over SDIO — not needed for audio, can stay off to save power/RAM.

## Audio (output only)
- **Codec: Everest ES8156** — **stereo audio DAC**, I2C control (incl. hardware volume).
- **Amp: FM8002A** mono speaker amp (switchable via coprocessor) → built-in 8Ω speaker.
- **3.5 mm headphone jack** with insertion detection (stereo, the good output).
- **I2S** drives the codec. **Confirmed from `badge-bsp` (`bsp/audio.h` +
  `targets/tanmatsu/badge_bsp_audio.c`):** I2S standard mode, **16-bit, stereo,
  interleaved L/R**, MSB slot. Default rate **44100 Hz**; change with
  `bsp_audio_set_rate(48000)` (note: it tears down + re-creates the I2S channel).
  - Output path: `bsp_audio_get_i2s_handle(&h)` → write blocks with `i2s_channel_write()`.
    Our float DSP converts to `int16` interleaved at the sink.
  - Volume: `bsp_audio_set_volume(0..100)` (hardware, via ES8156 over I2C).
  - Speaker amp: `bsp_audio_set_amplifier(bool)` / `..._force(bool)` (auto-mutes on
    headphone insert unless forced). Codec component: `nicolaielectronics__es8156`.
- ⚠️ **No audio input / no audio ADC.** There is no line/mic-in codec path. The
  general-purpose ADC is for things like pots/CV on expansion headers, **not** for an
  analog audio signal chain.

### What this means for "analog modeling"
This is a **fully digital virtual-analog (VA) + hybrid** instrument. There is no analog
signal path and nothing to sample from the outside world. "Analog modeling" = DSP that
*emulates* analog oscillators/filters. The hybrid is **wavetable + VA filter models +
FM**, all in software, out through the stereo I2S DAC. (See `specs/02` and the open
question about whether external analog/CV via expansion is in scope.)

## USB (two ports, opposite roles — important for MIDI)
- **USB-C: device mode.** We can enumerate as a **USB-MIDI device** to a computer/DAW
  (and as USB-serial/JTAG for flashing). TinyUSB via `esp_tinyusb`.
- **USB-A: host mode**, 480 Mbit/s, 1 A out. We can be a **USB-MIDI host** for a plugged-in
  hardware MIDI keyboard/controller. Needs a USB-host MIDI class driver (verify what the
  BSP/IDF provides; may need a small driver).
- `CONFIG_USB_HOST_HUBS_SUPPORTED=y` is already set in the Tanmatsu sdkconfig.

## Input & Display (the live-tweak UI surface)
- **QWERTY keyboard** + navigation/action keys + scancodes, delivered as
  `bsp_input_event_t` on a FreeRTOS queue (see `main/main.c`). Keyboard can double as a
  musical keyboard and as a parameter-entry surface.
- **800×480 DSI display**, ST7701, drawn via **PAX graphics** (`pax_*`), blitted with
  `bsp_display_blit`. Backlight brightness controllable. Big enough for real param pages
  and meters.
- **6 addressable RGB LEDs** (via coprocessor) — usable for activity/voice/clip feedback.

## Storage & Expansion
- **microSD** (SDIO, FATFS already configured, 3 volumes) — wavetable/sample/preset
  storage. Keeps flash free.
- Expansion: **PMOD/SAO (CATT)**, **Qwiic/StemmaQT I2C**, **36-pin personality module**
  (14 GPIO, I2C, UART, **I2S**, I3C). Path for future physical knobs/encoders/CV.

## Power
- Battery-powered handheld → DSP cost has a power/heat budget, not just a CPU budget.
  `PM_ENABLE` + DFS are on; pinning CPU to 400 MHz for audio may fight DFS — revisit.

## Budget implications (rough, refine by profiling)
- ~400 MHz × 2 cores. At 48 kHz that's ~8333 cycles/sample/core, or ~533k cycles per
  64-sample block per core. Generous for a handful of VA voices; polyphony target and
  per-voice cost are the design's central tradeoff (`specs/02`).
- Plan: **one core for audio**, one for UI/MIDI/SD, to keep the audio deadline clean.
