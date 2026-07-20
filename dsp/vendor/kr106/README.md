# Ultramaster KR-106 DSP subset

These files are copied verbatim from
[Ultramaster KR-106](https://github.com/kr-la/ultramaster_kr106), tag v2.5.13,
at commit `bc15caee5843ab238a25d0969e68d57db2b1615f`:

- `KR106Oscillators.h` from `Source/DSP/KR106Oscillators.h`
- `KR106VCF_OPTIMIZED.h` from `Source/DSP/KR106VCF_OPTIMIZED.h`
- `KR106ADSR.h` from `Source/DSP/KR106ADSR.h`
- `KR106VCA.h` from `Source/DSP/KR106VCA.h`
- `KR106Noise.h` from `Source/DSP/KR106Noise.h`

All vendored headers are unmodified. Target-specific adaptation lives in
`engine/juno_voice.{h,cpp}`. Ultramaster KR-106 and this combined distribution
are licensed under GPL-3.0-only; the exact upstream license text is at the
repository root in `LICENSE`.

Upstream authors are Karl LaRocca and David Mansfield; the KR-106 rewrite is
credited to Karl LaRocca.
