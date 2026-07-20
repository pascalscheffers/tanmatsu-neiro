# Ultramaster KR-106 DSP subset

These files are copied verbatim from
[Ultramaster KR-106](https://github.com/kr-la/ultramaster_kr106), tag v2.5.13,
at commit `bc15caee5843ab238a25d0969e68d57db2b1615f`:

- `KR106Oscillators.h` from `Source/DSP/KR106Oscillators.h`
- `KR106VCF_OPTIMIZED.h` from `Source/DSP/KR106VCF_OPTIMIZED.h`
- `KR106ADSR.h` from `Source/DSP/KR106ADSR.h`
- `KR106VCA.h` from `Source/DSP/KR106VCA.h`
- `KR106Noise.h` from `Source/DSP/KR106Noise.h`
- `BBDFilter.h` from `Source/DSP/BBDFilter.h`
- `KR106AnalogNoise.h` from `Source/DSP/KR106AnalogNoise.h`

All vendored headers are unmodified (the tracked analog-noise copy adds only a
conventional final newline). `dsp/kr106_chorus.h` is an attributed adaptation
of `Source/DSP/KR106Chorus.h` at the same pin. It replaces each allocating
`std::vector<float>` delay with a fixed 1024-float array and a 1023 mask, fixes
the rate at 48 kHz, omits unreachable mode I+II and plugin variance controls,
and adds embedded finite/denormal hygiene. Hermite interpolation, BBD filters
and saturation, leakage/click/floor/ripple models, I/II calibrations, and the
5 ms mode-change fade are retained. Other target-specific adaptation lives in
`engine/juno_voice.{h,cpp}`. Ultramaster KR-106 and this combined distribution
are licensed under GPL-3.0-only; the exact upstream license text is at the
repository root in `LICENSE`.

Upstream authors are Karl LaRocca and David Mansfield; the KR-106 rewrite is
credited to Karl LaRocca.
