/* tests/host/runner.h — minimal test runner (no external framework) */
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_ASSERT(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

#define TEST_ASSERT_LT(a, b, msg)                                                                          \
    do {                                                                                                   \
        if (!((a) < (b))) {                                                                                \
            fprintf(stderr, "FAIL [%s:%d]: %s  (%.6g not < %.6g)\n", __FILE__, __LINE__, msg, (double)(a), \
                    (double)(b));                                                                          \
            exit(1);                                                                                       \
        }                                                                                                  \
    } while (0)

static inline void test_begin(const char* name) {
    printf("  %-50s ", name);
    fflush(stdout);
}

static inline void test_pass(void) {
    printf("PASS\n");
}
