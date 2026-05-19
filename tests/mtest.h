#ifndef MTEST_H
#define MTEST_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    int failures;
    int skipped;
    const char *current;
} mtest_ctx_t;

#ifdef MTEST_IMPLEMENTATION
mtest_ctx_t g_mtest;
void mtest_fail(const char *file, int line, const char *expr)
{
    g_mtest.failures++;
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}
#else
extern mtest_ctx_t g_mtest;
void mtest_fail(const char *file, int line, const char *expr);
#endif

#define MTEST_BEGIN(argc, argv) do { (void)(argc); (void)(argv); g_mtest.failures = 0; g_mtest.skipped = 0; g_mtest.current = NULL; } while (0)
#define MTEST_END() (g_mtest.failures)

#define MTEST(name) static void name(void)
#define MTEST_SUITE(name) static void mtest_suite_##name(void)
#define MTEST_SUITE_RUN(name) do { mtest_suite_##name(); } while (0)
#define MTEST_RUN(test_fn) do { g_mtest.current = #test_fn; test_fn(); } while (0)

#define MTEST_SKIP(msg) do { g_mtest.skipped++; fprintf(stderr, "SKIP %s: %s\n", g_mtest.current ? g_mtest.current : "(unknown)", (msg)); return; } while (0)

#define MTEST_ASSERT_TRUE(cond) do { if (!(cond)) { mtest_fail(__FILE__, __LINE__, #cond); return; } } while (0)
#define MTEST_ASSERT_NOT_NULL(ptr) do { if ((ptr) == NULL) { mtest_fail(__FILE__, __LINE__, #ptr " != NULL"); return; } } while (0)

#define MTEST_ASSERT_EQ(a, b) do { long long _a = (long long)(a); long long _b = (long long)(b); if (_a != _b) { char _buf[128]; snprintf(_buf, sizeof(_buf), "%s == %s", #a, #b); mtest_fail(__FILE__, __LINE__, _buf); fprintf(stderr, "  got %lld vs %lld\n", _a, _b); return; } } while (0)
#define MTEST_ASSERT_NE(a, b) do { long long _a = (long long)(a); long long _b = (long long)(b); if (_a == _b) { char _buf[128]; snprintf(_buf, sizeof(_buf), "%s != %s", #a, #b); mtest_fail(__FILE__, __LINE__, _buf); return; } } while (0)
#define MTEST_ASSERT_GT(a, b) do { long long _a = (long long)(a); long long _b = (long long)(b); if (!(_a > _b)) { char _buf[128]; snprintf(_buf, sizeof(_buf), "%s > %s", #a, #b); mtest_fail(__FILE__, __LINE__, _buf); return; } } while (0)

#define MTEST_ASSERT_MEM_EQ(a, b, n) do { if (memcmp((a), (b), (n)) != 0) { mtest_fail(__FILE__, __LINE__, "memcmp(" #a "," #b ") == 0"); return; } } while (0)

#define MTEST_ASSERT_FALSE(cond) do { if ((cond)) { mtest_fail(__FILE__, __LINE__, #cond " is false"); return; } } while (0)
#define MTEST_ASSERT_GE(a, b) do { long long _a = (long long)(a); long long _b = (long long)(b); if (!(_a >= _b)) { char _buf[128]; snprintf(_buf, sizeof(_buf), "%s >= %s", #a, #b); mtest_fail(__FILE__, __LINE__, _buf); return; } } while (0)
#define MTEST_ASSERT_STR_EQ(a, b) do { if (strcmp((a), (b)) != 0) { mtest_fail(__FILE__, __LINE__, "strcmp(" #a "," #b ") == 0"); return; } } while (0)
#endif
