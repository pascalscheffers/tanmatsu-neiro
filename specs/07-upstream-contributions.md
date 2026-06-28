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

- **[now / Stage 0] PAX host build.** Spec 04 assumes PAX compiles off-ESP for the
  simulator. If it needs a CMake/host target or small portability fixes (no IDF includes
  in the core renderer, settable pixel buffer), that's the ideal first upstream PR — and
  the author is family. De-risks the whole host-first strategy. *Confirm during Stage 0.*
  **Don't panic** about it: a PAX portability tweak is normal and expected, not a fire.
  Surface the specific need to Pascal calmly, keep Stage 0 moving (lean on the spec-04
  software-present fallback if needed to stay unblocked), and let the upstream fix land at
  its own pace. No thrashing, no panic-fork.
- **[soon] `bsp_audio` ergonomics.** `bsp_audio_set_rate` tears down + recreates the I2S
  channel; default is 44.1 k. A `bsp_audio_get_format()` accessor and/or a documented
  init-at-rate path would be cleaner than re-deriving it. Minor; raise if it annoys us.
- **[Stage 5] USB-host MIDI.** ADR 0005's real unknown. If neither IDF nor BSP provides a
  USB-host MIDI class driver, a reusable component (contributed to badge.team) is better
  than a private one. Discuss scope with Renze before building.
- **[watch, Stage 2+] PAX performance.** Full-frame 800×480×24bpp blits every redraw are
  heavy for a live-tweak UI. If profiling shows it, dirty-rect / partial-blit support in
  PAX is a high-value perf PR — *with numbers*.
- **[maybe] launcher integration.** Icon/metadata/appfs niceties so the synth launches
  cleanly. Low priority.

## Workflow
When any session hits one of these, **don't quietly work around it** — append it here with
a one-line need + where it bit, and flag Pascal in the response. Pascal owns the upstream
relationship and decides what to raise. Track filed PRs back in this list.
