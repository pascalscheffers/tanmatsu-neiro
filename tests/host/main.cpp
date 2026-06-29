/* tests/host/main.cpp — host DSP test runner entry point */
#include <stdio.h>
#include "runner.h"

/* Declarations — one per test file */
void test_osc_polyblep_aliasing();
void test_osc_waveform_suite();
void test_voice_suite();
void test_alloc_suite();
void test_command_queue_suite();
void test_param_store_suite();
void test_saturate_suite();
void test_preset_suite();
void test_mod_sources_suite();
void test_mod_matrix_suite();
void test_params_suite();
void test_clock_suite();
void test_scheduler_suite();
void test_arp_suite();
void test_arp_clock_suite();
void test_midi_parse_suite();
void test_sustain_suite();
void test_ui_presets_suite();

int main(void) {
    printf("=== tanmatsu host DSP tests ===\n");
    test_osc_polyblep_aliasing();
    test_osc_waveform_suite();
    test_voice_suite();
    test_alloc_suite();
    test_command_queue_suite();
    test_param_store_suite();
    test_saturate_suite();
    test_preset_suite();
    test_mod_sources_suite();
    test_mod_matrix_suite();
    test_params_suite();
    test_clock_suite();
    test_scheduler_suite();
    test_arp_suite();
    test_arp_clock_suite();
    test_midi_parse_suite();
    test_sustain_suite();
    test_ui_presets_suite();
    printf("All tests passed.\n");
    return 0;
}
