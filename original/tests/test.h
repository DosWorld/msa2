/*
 * MSA2 unit-test harness. C89 only.
 *
 * Each test file defines functions named test_<group>_<case>() returning
 * 0 on success, nonzero on failure. The functions are registered in
 * test_runner.c. The macros below print the first failure in a test and
 * return early.
 */

#ifndef MSA2_TEST_H
#define MSA2_TEST_H

#include <stdio.h>
#include <string.h>

extern int g_test_failures;
extern const char *g_test_current;

#define TEST_FAIL_FMT(fmt, ...) do {                                       \
        fprintf(stderr, "FAIL: %s: " fmt " (%s:%d)\n",                     \
                g_test_current, __VA_ARGS__, __FILE__, __LINE__);          \
        g_test_failures++;                                                 \
        return 1;                                                          \
    } while(0)

#define TEST_ASSERT(cond) do {                                             \
        if(!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s: assertion failed: %s (%s:%d)\n",    \
                    g_test_current, #cond, __FILE__, __LINE__);            \
            g_test_failures++;                                             \
            return 1;                                                      \
        }                                                                  \
    } while(0)

#define TEST_ASSERT_EQ_INT(expected, actual) do {                          \
        long _e = (long)(expected);                                        \
        long _a = (long)(actual);                                          \
        if(_e != _a) {                                                     \
            fprintf(stderr,                                                \
                "FAIL: %s: expected %ld got %ld for %s (%s:%d)\n",         \
                g_test_current, _e, _a, #actual, __FILE__, __LINE__);      \
            g_test_failures++;                                             \
            return 1;                                                      \
        }                                                                  \
    } while(0)

#define TEST_ASSERT_EQ_STR(expected, actual) do {                          \
        const char *_e = (expected);                                       \
        const char *_a = (actual);                                         \
        if(_a == NULL || strcmp(_e, _a) != 0) {                            \
            fprintf(stderr,                                                \
                "FAIL: %s: expected \"%s\" got \"%s\" (%s:%d)\n",          \
                g_test_current, _e, _a ? _a : "(null)",                    \
                __FILE__, __LINE__);                                       \
            g_test_failures++;                                             \
            return 1;                                                      \
        }                                                                  \
    } while(0)

#define TEST_ASSERT_MEM_EQ(expected, actual, n) do {                       \
        const unsigned char *_e = (const unsigned char *)(expected);       \
        const unsigned char *_a = (const unsigned char *)(actual);         \
        size_t _i;                                                         \
        for(_i = 0; _i < (size_t)(n); _i++) {                              \
            if(_e[_i] != _a[_i]) {                                         \
                fprintf(stderr,                                            \
                    "FAIL: %s: byte %lu: expected 0x%02x got 0x%02x"       \
                    " (%s:%d)\n",                                          \
                    g_test_current, (unsigned long)_i,                     \
                    _e[_i], _a[_i], __FILE__, __LINE__);                   \
                g_test_failures++;                                         \
                return 1;                                                  \
            }                                                              \
        }                                                                  \
    } while(0)

#endif
