/* Tiny test helpers: no framework needed. One test program = one .c file. */
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_failures;

#define CHECK(cond) do { if (!(cond)) { \
    printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

#define CHECK_EQ(got, want) do { long long _g = (long long)(got), _w = (long long)(want); \
    if (_g != _w) { printf("    FAIL %s:%d: %s  (got %lld, want %lld)\n", \
                           __FILE__, __LINE__, #got, _g, _w); g_failures++; } } while (0)

#define CHECK_BYTES(got, want, n) do { if (memcmp((got), (want), (n)) != 0) { \
    printf("    FAIL %s:%d: bytes differ: %s\n", __FILE__, __LINE__, #got); g_failures++; } } while (0)

#define RUN(fn) do { int _before = g_failures; fn(); \
    printf("%s %s\n", g_failures == _before ? "ok  " : "FAIL", #fn); } while (0)

#define TESTS_DONE() do { printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", \
    g_failures, g_failures == 1 ? "" : "s"); return g_failures ? 1 : 0; } while (0)
#endif
