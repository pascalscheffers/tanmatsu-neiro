// bench.h — CPU profiling harness (Stage 0.5).
//
// Compiled only when SYNTH_BENCH is defined (the normal shipping build never
// includes this file). bench_run() prints a kernel cost table then runs the
// audio-deadline load ramp via the platform audio API.
#pragma once
#ifdef SYNTH_BENCH

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Run the full Stage 0.5 benchmark: kernel table + load ramp.
// Never returns (loops until the deadline is exceeded, then hangs).
// Called from app_run() in SYNTH_BENCH mode before the normal UI loop.
void bench_run(uint32_t sample_rate, uint32_t block_size);

// Section 5: per-block DSP micro-bench (implemented in bench_blocks.cpp).
// Times each real dsp:: / DaisySP building block in isolation.
// block_period: cycles per block; cps: cycles per second; n: block size in samples.
void bench_blocks_run(uint32_t block_period, uint32_t cps, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif  // SYNTH_BENCH
