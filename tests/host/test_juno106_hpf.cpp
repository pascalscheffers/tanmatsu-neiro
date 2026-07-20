// Host acceptance tests for the pinned KR-106 Juno-106 HPF adaptation.
#include <math.h>
#include <stdint.h>
#include "dsp/juno106_hpf.h"
#include "runner.h"

static constexpr float kFs = 48000.0f;
static constexpr float kPi = 3.14159265358979323846f;

static float sine_gain_db(dsp::Juno106HpfPosition position, float frequency, int settle_samples = 192000,
                          int measure_samples = 48000) {
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(position);
    float phase_step = 2.0f * kPi * frequency / kFs;
    for (int i = 0; i < settle_samples; ++i) hpf.process(sinf(phase_step * i));

    double in_sin = 0.0;
    double in_cos = 0.0;
    for (int i = settle_samples; i < settle_samples + measure_samples; ++i) {
        float phase = phase_step * i;
        float out   = hpf.process(sinf(phase));
        in_sin     += static_cast<double>(out) * sinf(phase);
        in_cos     += static_cast<double>(out) * cosf(phase);
    }
    double amplitude = (2.0 / measure_samples) * sqrt(in_sin * in_sin + in_cos * in_cos);
    return 20.0f * log10f(static_cast<float>(amplitude));
}

static void test_cut_positions(void) {
    test_begin("juno106_hpf: pinned 236/754 Hz cut corners");
    float g236 = sine_gain_db(dsp::JUNO106_HPF_236HZ, 236.0f);
    float g754 = sine_gain_db(dsp::JUNO106_HPF_754HZ, 754.0f);
    printf("  response cut: 236 Hz %.3f dB, 754 Hz %.3f dB\n", g236, g754);
    TEST_ASSERT(fabsf(g236 + 3.0103f) < 0.08f, "236 Hz corner differs from -3.01 dB");
    TEST_ASSERT(fabsf(g754 + 3.0103f) < 0.08f, "754 Hz corner differs from -3.01 dB");
    test_pass();
}

static void test_common_ac_coupling(void) {
    test_begin("juno106_hpf: common 0.35 Hz AC coupling");
    float g035 = sine_gain_db(dsp::JUNO106_HPF_BYPASS, 0.35f, 960000, 960000);
    printf("  response AC: 0.35 Hz %.3f dB\n", g035);
    TEST_ASSERT(fabsf(g035 + 3.0103f) < 0.08f, "0.35 Hz blocker corner differs from -3.01 dB");

    for (int mode = 0; mode < 4; ++mode) {
        dsp::Juno106Hpf hpf;
        hpf.init(kFs);
        hpf.set_position(static_cast<dsp::Juno106HpfPosition>(mode));
        float out = 0.0f;
        for (int i = 0; i < 576000; ++i) out = hpf.process(0.25f);
        // With float state at a 0.35 Hz pole, accumulation eventually reaches
        // RV32F precision near a 0.25 DC level; require strong rejection, not
        // an unattainable double-precision zero.
        TEST_ASSERT(fabsf(out) < 2.0e-3f, "mode did not reject DC through common blocker");
    }
    test_pass();
}

static void test_bass_response(void) {
    test_begin("juno106_hpf: pinned J106 bass circuit response");
    const float g20  = sine_gain_db(dsp::JUNO106_HPF_BASS_BOOST, 20.0f);
    const float g70  = sine_gain_db(dsp::JUNO106_HPF_BASS_BOOST, 70.0f);
    const float g103 = sine_gain_db(dsp::JUNO106_HPF_BASS_BOOST, 103.0f);
    const float g5k  = sine_gain_db(dsp::JUNO106_HPF_BASS_BOOST, 5000.0f);
    printf("  response bass: 20 %.3f, 70 %.3f, 103 %.3f, 5000 %.3f dB\n", g20, g70, g103, g5k);
    TEST_ASSERT(fabsf(g20 - 10.10f) < 0.15f, "20 Hz bass response outside tolerance");
    TEST_ASSERT(fabsf(g70 - 7.42f) < 0.15f, "70 Hz bass response outside tolerance");
    TEST_ASSERT(fabsf(g103 - 5.86f) < 0.15f, "103 Hz bass response outside tolerance");
    TEST_ASSERT(fabsf(g5k - 1.41f) < 0.15f, "5 kHz bass response outside tolerance");
    TEST_ASSERT(fabsf((g20 - g5k) - 8.69f) < 0.2f, "bass low/high response difference outside tolerance");
    test_pass();
}

// Independent cut-mode oracle copied from the pinned KR-106 state-variable
// equations. It makes the crossfade sample count and rapid-switch snapshot
// behavior observable without exposing target internals.
struct CutReference {
    struct State {
        float frequency = 0.0f;
        float g         = 0.0f;
        float hp        = 0.0f;
        float dc        = 0.0f;
    } current, previous;
    float dc_g = tanf((0.35f / (kFs * 0.5f)) * kPi * 0.5f);
    int   fade = 0;

    void set_mode(int mode) {
        float frequency = mode == 2 ? 236.0f : (mode == 3 ? 754.0f : 0.0f);
        if (frequency == current.frequency) return;
        previous          = current;
        fade              = 64;
        current.frequency = frequency;
        current.g         = frequency > 0.0f ? tanf((frequency / (kFs * 0.5f)) * kPi * 0.5f) : 0.0f;
    }

    float path(float in, State& state) {
        float v       = (in - state.dc) * dc_g / (1.0f + dc_g);
        float lp      = state.dc + v;
        state.dc      = lp + v;
        float blocked = in - lp;
        if (state.frequency <= 0.0f) return blocked;
        v        = (blocked - state.hp) * state.g / (1.0f + state.g);
        lp       = state.hp + v;
        state.hp = lp + v;
        return blocked - lp;
    }

    float process(float in) {
        float out = path(in, current);
        if (fade > 0) {
            float old = path(in, previous);
            float t   = static_cast<float>(fade) / 64.0f;
            out       = out * (1.0f - t) + old * t;
            --fade;
        }
        return out;
    }
};

static void test_exact_crossfade_and_rapid_switching(void) {
    test_begin("juno106_hpf: exact 64-sample and repeated-switch crossfades");
    dsp::Juno106Hpf hpf;
    CutReference    ref;
    hpf.init(kFs);
    for (int i = 0; i < 600; ++i) {
        float in = 0.4f * sinf(2.0f * kPi * 317.0f * i / kFs) + 0.1f;
        TEST_ASSERT(fabsf(hpf.process(in) - ref.process(in)) < 2.0e-6f, "pre-switch oracle mismatch");
    }
    hpf.set_position(dsp::JUNO106_HPF_236HZ);
    ref.set_mode(2);
    for (int i = 0; i < 29; ++i) {
        float in = 0.6f * sinf(2.0f * kPi * 541.0f * i / kFs);
        TEST_ASSERT(fabsf(hpf.process(in) - ref.process(in)) < 2.0e-6f, "first crossfade mismatch");
    }
    hpf.set_position(dsp::JUNO106_HPF_754HZ);
    ref.set_mode(3);
    for (int i = 0; i < 17; ++i) {
        float in = -0.3f + 0.2f * sinf(2.0f * kPi * 991.0f * i / kFs);
        TEST_ASSERT(fabsf(hpf.process(in) - ref.process(in)) < 2.0e-6f, "rapid-switch crossfade mismatch");
    }
    hpf.set_position(dsp::JUNO106_HPF_BYPASS);
    ref.set_mode(1);
    for (int i = 0; i < 65; ++i) {
        float in = 0.25f * sinf(2.0f * kPi * 83.0f * i / kFs);
        TEST_ASSERT(fabsf(hpf.process(in) - ref.process(in)) < 2.0e-6f, "64-sample completion mismatch");
    }
    test_pass();
}

static void test_split_processing_deterministic(void) {
    test_begin("juno106_hpf: split and contiguous processing deterministic");
    dsp::Juno106Hpf contiguous;
    dsp::Juno106Hpf split;
    contiguous.init(kFs);
    split.init(kFs);
    contiguous.set_position(dsp::JUNO106_HPF_BASS_BOOST);
    split.set_position(dsp::JUNO106_HPF_BASS_BOOST);
    float expected[4096];
    for (int i = 0; i < 4096; ++i) {
        if (i == 1379) contiguous.set_position(dsp::JUNO106_HPF_754HZ);
        float in    = 0.5f * sinf(2.0f * kPi * 113.0f * i / kFs);
        expected[i] = contiguous.process(in);
    }
    for (int begin = 0; begin < 4096; begin += 1024) {
        int end = begin + 1024;
        for (int i = begin; i < end; ++i) {
            if (i == 1379) split.set_position(dsp::JUNO106_HPF_754HZ);
            float in = 0.5f * sinf(2.0f * kPi * 113.0f * i / kFs);
            TEST_ASSERT(expected[i] == split.process(in), "split processing changed deterministic output");
        }
    }
    test_pass();
}

static void test_nonfinite_recovery_and_bounds(void) {
    test_begin("juno106_hpf: non-finite recovery and bounded modes");
    for (int mode = 0; mode < 4; ++mode) {
        dsp::Juno106Hpf hpf;
        hpf.init(kFs);
        hpf.set_position(static_cast<dsp::Juno106HpfPosition>(mode));
        for (int i = 0; i < 12000; ++i) {
            if ((i % 997) == 0) hpf.set_position(static_cast<dsp::Juno106HpfPosition>((mode + i) & 3));
            float in  = 0.8f * sinf(2.0f * kPi * 31.0f * i / kFs);
            float out = hpf.process(in);
            TEST_ASSERT(isfinite(out), "mode produced non-finite output");
            TEST_ASSERT(fabsf(out) < 4.0f, "mode exceeded bounded envelope");
        }
        TEST_ASSERT(isfinite(hpf.process(NAN)), "NaN input did not recover");
        TEST_ASSERT(isfinite(hpf.process(INFINITY)), "+Inf input did not recover");
        TEST_ASSERT(isfinite(hpf.process(-INFINITY)), "-Inf input did not recover");
        TEST_ASSERT(isfinite(hpf.process(0.25f)), "finite input after poison did not recover");
    }
    test_pass();
}

static void test_long_silence_normal_or_zero(void) {
    test_begin("juno106_hpf: FTZ-off silence remains normal or zero");
    for (int mode = 0; mode < 4; ++mode) {
        dsp::Juno106Hpf hpf;
        hpf.init(kFs);
        hpf.set_position(static_cast<dsp::Juno106HpfPosition>(mode));
        hpf.process(1.0f);
        float out = 0.0f;
        for (int i = 0; i < 500000; ++i) out = hpf.process(0.0f);
        int classification = fpclassify(out);
        TEST_ASSERT(classification == FP_NORMAL || classification == FP_ZERO,
                    "silence output is neither normal nor zero");
    }
    test_pass();
}

void test_juno106_hpf_suite(void) {
    printf("--- dsp/juno106_hpf.h ---\n");
    printf("  sizeof(dsp::Juno106Hpf): %zu bytes\n", sizeof(dsp::Juno106Hpf));
    test_cut_positions();
    test_common_ac_coupling();
    test_bass_response();
    test_exact_crossfade_and_rapid_switching();
    test_split_processing_deterministic();
    test_nonfinite_recovery_and_bounds();
    test_long_silence_normal_or_zero();
}
