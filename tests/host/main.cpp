/* tests/host/main.cpp — host DSP test runner entry point */
#include "runner.h"
#include <stdio.h>

/* Declarations — one per test file */
void test_osc_polyblep_aliasing();

int main(void)
{
    printf("=== tanmatsu host DSP tests ===\n");
    test_osc_polyblep_aliasing();
    printf("All tests passed.\n");
    return 0;
}
