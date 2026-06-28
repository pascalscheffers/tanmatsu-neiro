# Upstream Contributions — strategy

We have direct PR access and personal relationships to the core Tanmatsu software. So our
**reuse-first** principle extends one step: when we hit a gap in the platform, the default
is **fix it upstream**, not fork it or paper over it locally.

## Why this is different here
- **Renze** (Nicolai Electronics) — the hardware/firmware author — is a friend.
- **PAX graphics** was written by Pascal's eldest child (`robotman2412`).
- So the feedback loop is short and trusted: we can raise a need, agree on shape, and land
  a clean PR fast. A fix we contribute helps every Tanmatsu app, not just ours.

This does **not** lower the bar — it raises it. PRs to these projects should be clean,
minimal, and upstream-quality, because they reflect on us and benefit the whole badge.team
ecosystem. Move fast, but ship work we'd be proud to have our name (and our kid's project)
attached to.

## Decision ladder when we hit a platform gap
1. **Bug in core/PAX/BSP/launcher** → fix upstream + PR. Always worth it. If we need to
   stay unblocked meanwhile, a *minimal, clearly-marked* local workaround with a `TODO`
   linking the PR — removed when it merges. Don't let workarounds calcify.
2. **Small missing feature we need** (a BSP accessor, a PAX call, a launcher hook) →
   raise the need with the author, agree on the API, PR it. Prefer upstream over carrying
   a local patch.
3. **Performance** → **profile first** (CLAUDE.md rule), bring numbers, then PR. No
   speculative "this looks slow" changes to someone else's code.
4. **Large / invasive / API-shaping** → discuss with the author *before* building. A
   30-second message beats a rejected 300-line PR.

**Never silently vendor-fork** a maintained dependency. If we must carry a local patch
temporarily, track it (what, why, upstream PR link) so it stays tractable and gets retired.

## How we carry patches (the mechanism)
Local fixes are **tracked patch files**, not ad-hoc shims, so they're versioned, reviewable,
and PR-ready (others can iterate once we push to GitHub):

- Patches live in `upstream-patches/<component>/NNNN-*.patch`, each with a prose header
  explaining *why it exists* + the upstream target — the file **is** the future PR.
- Dependency sources are in gitignored `managed_components/`; `tools/apply-upstream-patches.sh`
  (via `make patches`, and auto-run by `make host` / `make build`) re-applies them
  idempotently. `.component_hash` is left intact so the IDF component manager doesn't revert.
- Submit upstream with `git am` into a clone of the dep; **delete the patch on merge**.
- Full details: `upstream-patches/README.md`.

## Contribution targets
| Project | Repo / owner | What it gives us |
|---|---|---|
| **PAX graphics** | `robotman2412/pax-graphics` (Pascal's kid) | all UI drawing; **host portability** for the sim |
| **badge-bsp** | `badgeteam/badge-bsp` | audio/I2S, input, display, power, LEDs |
| **tanmatsu-launcher** | `Nicolai-Electronics/tanmatsu-launcher` | app discovery/launch, metadata, icons |
| **esp-hosted-tanmatsu / tanmatsu-wifi** | Nicolai-Electronics | radio (we may not need it) |
| **hardware / docs** | Nicolai Electronics (Renze) | spec clarifications, errata |

Licensing: follow each project's own license for contributions (template is CC0; our code
MIT — ADR 0004). Verify PAX/BSP licenses before the first PR.

## Live candidate list (append as we find them; surface to Pascal)
Tiered by when it bites. Nothing here is committed-to yet — these are *needs to discuss*.

- **[now / Stage 0] PAX host build — CONFIRMED (Stage 0, 2026-06-28).** PAX core *does*
  build off-ESP via its non-ESP CMake path (`pax_gfx` target); ESP includes are properly
  `#ifdef ESP_PLATFORM`-guarded. Two small macOS portability gaps remain:
  1. `core/src/{pax_gfx.c,pax_shapes.c}` include `<malloc.h>` and
     `core/src/{pax_setters.c,renderer/*}` include `<endian.h>` — both glibc-isms absent on
     Darwin (use `<stdlib.h>` / `<machine/endian.h>`).
  2. The **gui** sources (`gui/src/elem/*.c`, `pax_gui.c`) have *unguarded* `esp_*` includes,
     so the `pax_gui` target won't compile off-ESP (we `EXCLUDE_FROM_ALL` it for now).
  **Patch landed locally → `upstream-patches/pax-graphics/0001-macos-bsd-host-portable-includes.patch`**
  (swaps `<malloc.h>`→`<stdlib.h>`, selects `<machine/endian.h>` on Apple via `#if`/`#else`;
  ESP/Linux unchanged). Build-verified on host + device. The earlier compat-shim workaround
  was removed in favor of the patch. **Ready to PR** to `robotman2412/pax-graphics` — surface
  to Pascal; retire the patch on merge. *Still open:* the gui esp-include guards (we
  `EXCLUDE_FROM_ALL` gui for now; not needed until we use PAX gui) — separate patch later.
- **[soon] `bsp_audio` ergonomics — clarified (Stage 0).** Correction: `bsp_audio_set_rate`
  does **not** tear down/recreate — it calls `i2s_channel_reconfig_std_clock`, which requires
  the channel **disabled** first (the BSP enables it at init). So changing rate means
  disable→set_rate→enable (what `platform/device/` now does). A `bsp_audio_get_format()`
  accessor (sample rate + bit width + channels) would let us stop hardcoding 16-bit stereo,
  and a `bsp_audio_set_rate` that handles the disable/enable internally would be cleaner.
  Minor; raise if it annoys us.
- **[Stage 5] USB-host MIDI.** ADR 0005's real unknown. If neither IDF nor BSP provides a
  USB-host MIDI class driver, a reusable component (contributed to badge.team) is better
  than a private one. Discuss scope with Renze before building.
- **[watch, Stage 2+] PAX performance.** Full-frame 800×480×24bpp blits every redraw are
  heavy for a live-tweak UI. If profiling shows it, dirty-rect / partial-blit support in
  PAX is a high-value perf PR — *with numbers*.
- **[maybe] launcher integration.** Icon/metadata/appfs niceties so the synth launches
  cleanly. Low priority.
- **[trivial] badgelink `install.sh` macOS fix (2026-06-28).** `tools/install.sh` runs
  `python -m venv .venv`, but macOS ships only `python3` → `python: command not found`.
  One-line fix upstream (`python3 -m venv`, or detect). Repo `badgeteam/esp32-component-badgelink`.
  We work around it in our Makefile (`make badgelink` builds the venv with `python3`
  directly, so badgelink.sh skips install.sh). Worth a tiny PR; surface to Pascal. *No
  local patch* — badgelink/ is a gitignored tool clone, not a managed_component.

## Workflow
When any session hits one of these, **don't quietly work around it** — append it here with
a one-line need + where it bit, and flag Pascal in the response. Pascal owns the upstream
relationship and decides what to raise. Track filed PRs back in this list.
