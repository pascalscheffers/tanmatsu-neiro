/* tests/host/main.cpp — host DSP test runner entry point */
#include "runner.h"
#include <stdio.h>

/* Declarations — one per test file */
void test_osc_polyblep_aliasing();
void test_voice_suite();
void test_alloc_suite();
void test_command_queue_suite();

int main(void)
{
    printf("=== tanmatsu host DSP tests ===\n");
    test_osc_polyblep_aliasing();
    test_voice_suite();
    test_alloc_suite();
    test_command_queue_suite();
    printf("All tests passed.\n");
    return 0;
}
