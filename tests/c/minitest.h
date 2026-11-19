/* Minimal assert helpers for host-side C tests (no dependencies, C11). */
#ifndef MINITEST_H
#define MINITEST_H

#include <stdio.h>

static int mt_checks = 0;
static int mt_failures = 0;

#define MT_CHECK(cond)                                                                             \
    do {                                                                                           \
        mt_checks++;                                                                               \
        if (!(cond)) {                                                                             \
            mt_failures++;                                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                          \
    } while (0)

#define MT_CHECK_EQ(a, b)                                                                          \
    do {                                                                                           \
        long long mt_a_ = (long long)(a);                                                          \
        long long mt_b_ = (long long)(b);                                                          \
        mt_checks++;                                                                               \
        if (mt_a_ != mt_b_) {                                                                      \
            mt_failures++;                                                                         \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", __FILE__,         \
                    __LINE__, #a, #b, mt_a_, mt_b_);                                               \
        }                                                                                          \
    } while (0)

#define MT_RUN(fn)                                                                                 \
    do {                                                                                           \
        int mt_before_ = mt_failures;                                                              \
        fn();                                                                                      \
        printf("%s %s\n", mt_failures == mt_before_ ? "PASS" : "FAIL", #fn);                       \
    } while (0)

#define MT_REPORT()                                                                                \
    (printf("%d checks, %d failures\n", mt_checks, mt_failures), mt_failures == 0 ? 0 : 1)

#endif /* MINITEST_H */
