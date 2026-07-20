# Juno-106 control curves — byte → physical value (WO-13h)

Offline-mapping reference for `tools/build_juno106_bank.py`. Every function here is
named, documented, and reused verbatim by the generator — no third-party (KR-106 or
otherwise) curve table was read or consulted (clean-room, per CLAUDE.md/stage-13).

## Continuous bytes (16 of the 18): the declared-curve fallback

None of these have been measured against real Juno-106 hardware yet
(`juno106-parameter-set.md` §B item 1 — open). Rather than invent a bespoke taper per
byte, or borrow one, every continuous byte reuses the **curve already declared for its
destination `ParamId`** in `engine/param_desc.cpp` (the single parameter table —
CLAUDE.md Prime Directive 2: one definition, not a second one here):

```
norm = byte / 127.0                     # byte is 0-127 (7-bit tape field)
CURVE_LIN:  v = lo + norm * (hi - lo)
CURVE_EXP:  v = lo * (hi / lo) ** norm  # lo must be > 0
```

This is the simplest monotonic mapping available: it assumes the panel pot's physical
travel matches the taper already chosen for the on-screen control (which is itself
tuned for a musically-useful UI sweep). It is almost certainly *not* the real Juno
hardware taper for every control (Roland's own pots are unlikely to match our UI
curve exactly, and envelope/cutoff times are known to be non-linear in ways we
haven't measured — see §B item 1). **Every row below is calibration-pending** until a
hardware A/B pass (WO-13k) confirms or replaces it.

| Byte | Neiro param | Curve | lo | hi |
|---:|---|---|---:|---:|
| 0 | LFO1 Rate | EXP | 0.01 | 20.0 |
| 1 | LFO1 Delay | LIN | 0.0 | 5.0 |
| 2 | DCO LFO Depth | LIN | 0.0 | 1.0 |
| 3 | OSC PWM | LIN | 0.0 | 1.0 |
| 4 | Noise Level | LIN | 0.0 | 1.0 |
| 5 | Filter Cutoff | EXP | 20.0 | 20000.0 |
| 6 | Filter Res | LIN | 0.0 | 1.0 |
| 7 | VCF Env Depth | LIN | 0.0 | 1.0 |
| 8 | VCF LFO Depth | LIN | 0.0 | 1.0 |
| 9 | VCF Key Track | LIN | 0.0 | 1.0 |
| 10 | VCA Level | LIN | 0.0 | 1.0 |
| 11 | Attack + Env2 Attack (duplicated) | EXP | 0.001 | 5.0 |
| 12 | Decay + Env2 Decay (duplicated) | EXP | 0.001 | 5.0 |
| 13 | Sustain + Env2 Sustain (duplicated) | LIN | 0.0 | 1.0 |
| 14 | Release + Env2 Release (duplicated) | EXP | 0.001 | 5.0 |
| 15 | Sub Level | LIN | 0.0 | 1.0 |

Bytes 11-14 (the one real Juno ADSR) are decoded once, then the same physical value is
written into **both** Neiro envelopes (`ENV_*` driving amp, `ENV2_*` driving the
filter) per the Source record contract — a real Juno-106 has one EG feeding both
destinations.

Five of the 128 committed records (the tape-decode-residue slots already flagged
`"uncertain": true` — A86, A88, B65, B74, B88) carry a stray high bit in byte 16 and/or
17 from unresolved tape noise (`specs/notes/juno106-tape-format.md` "Resolution"). The
generator masks byte 16 to its 7 meaningful bits and byte 17 to its 5 before decoding,
rather than hard-failing — those slots are already visibly marked `(uncertain)` to the
user, so a masked best-effort decode is preferable to refusing to build the bank.

## Switch byte 16 (bits 0-6; bit 7 always 0 per tape-format legality check)

| Bits | Field | Decode |
|---|---|---|
| 0-2 | Range | see below |
| 3 | Pulse on/off | direct: `bit3 != 0` |
| 4 | Saw on/off | direct: `bit4 != 0` |
| 5 | Chorus off (inverted) | `bit5 != 0` means the chorus switch is physically **off** |
| 6 | Chorus I/II | only consulted when bit 5 says chorus is on: `bit6 == 0` → Chorus I, `bit6 == 1` → Chorus II |

**Range (bits 0-2, `OSC Range`).** The Source record contract states the three legal
switch positions map `16' = -12 semitones, 8' = 0, 4' = +12`. Across the committed
128-record bank the raw 3-bit field actually takes all 8 possible values (not just
0/1/2) — the real bit-position-to-octave encoding used by the original hardware dump
is **not yet confirmed** (a genuine open item, alongside §B item 6/7). To stay
deterministic and never emit an out-of-range value: compute `pos = min(field, 2)`
then look up `{0: -12, 1: 0, 2: +12}`. Values 3-7 collapse to `+12` (the same as `2`).
This is documented explicitly as **calibration-pending** — a hardware pass (WO-13k)
should confirm which raw codes correspond to which real switch position, and this
function should be revisited then.

**Chorus off/I/II.** Byte 16 packs three chorus states (Off, I, II) into two bits:
an "off" flag and an "I/II" flag. The off flag is *inverted* per the Source record
contract; the convention adopted here (raw bit 1 = off) is an internally-consistent,
documented choice, not a measured fact — also calibration-pending; a listening pass
against real Juno-106 chorus settings (WO-13k) should confirm or flip it.

## Switch byte 17 (bits 0-4; bits 5-7 always 0)

| Bit | Field | Decode |
|---|---|---|
| 0 | PWM source | `bit0 != 0` → Manual (`PWM Mode` = 1), else LFO (`PWM Mode` = 0) |
| 1 | VCF Env polarity | `bit1 != 0` → inverted (`VCF Env Polarity` = 1), else positive (0) |
| 2 | VCA gate/env | `bit2 != 0` → gate (`VCA Gate Mode` = 1), else envelope-driven (0) |
| 3-4 | HPF position | `pos = 3 - ((byte17 >> 3) & 3)` (Source record contract formula, exact) |

These four are direct bit reads with no ambiguity in the Source record contract text,
so none are flagged calibration-pending (unlike range/chorus above).

## Neiro-only extensions (no Juno-panel equivalent)

The real Juno-106 has no per-patch oscillator-level knob, no filter-mode switch (VCF
is LP-only), and no per-patch output-trim knob distinct from `VCA Level`. Every
imported original loads these Neiro extensions at their **neutral/unity** value so the
imported patch sounds like "the real Juno control set, nothing added":

| Neiro param | Value | Why |
|---|---|---|
| Osc Level | 1.0 (unity) | real DCO has no level knob — full level, balanced by Sub/Noise |
| Filter Mode | 0.0 (LP) | real VCF is 4-pole LP only |
| Master Gain | 1.0 (unity) | no per-patch output-trim on the real panel |

All other Neiro-only rows (LFO2, arp, unison, portamento, free chorus rate/depth/delay,
clock, etc.) are simply absent from each generated patch's `params` object — per
`engine/bank_json.h`, absent keys are not an error; WO-13i / the engine's own defaults
own filling them in, not this offline mapper.
# KR-106 factory-bank conversion (WO-14g)

The generated factory bank now consumes the committed J106 entries 128–255
from Ultramaster KR-106 v2.5.13 at commit
`bc15caee5843ab238a25d0969e68d57db2b1615f`. The raw artifact preserves all
44 controller integers and descriptive names; the builder maps only KR
indices 3–19 and 23–35.

Direct semantics are preserved for HPF, pulse, saw, PWM mode, VCF envelope
polarity, VCA gate mode, octave (`0/1/2` → `-12/0/+12` semitones), and chorus
(I → 1, II → 2, neither → 0). Sub level is zero when KR's sub switch is off.
Normalized depth and level controls retain `raw / 127`. The one Juno ADSR is
duplicated into ENV1 and ENV2 because Neiro's filter modulation reads ENV2.
KR's derived chorus-off field and LFO-mode field have no stored Neiro
counterpart and are omitted, as are Neiro's legacy Filter Mode and chorus
rate/depth/delay no-ops. Osc Level and Master Gain use neutral unity values.

This is a preset-data conversion, not a claim of bit-identical audio. Neiro's
physical-value seam needs bank-only approximations for LFO rate
(`0.01 * 2000^(raw/127)` Hz), LFO delay (`5 * raw/127` seconds), and cutoff
(`20 * 1000^(raw/127)` Hz). Attack, decay, and release use the exact timing
tables already generated from the pinned KR-106 port, but values above the
public 5-second envelope ceiling clamp to 5 seconds. Those long raw decay and
release indices therefore collapse and do not select their original KR timing
index at runtime.

Exact sonic parity would require separate runtime/public-seam work. Current
DCO-LFO and PWM depth laws, additive Hz-domain VCF modulation, linear LFO
delay fade, and VCA level/gate behavior differ from KR-106's runtime laws.
Those differences are explicit approximations here; WO-14g does not alter DSP.
