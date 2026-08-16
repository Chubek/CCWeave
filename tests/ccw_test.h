/* Minimal assertion helpers shared by the CCWeave test programs. */

#ifndef CCW_TEST_H
#define CCW_TEST_H

#include <stdio.h>
#include <string.h>

static int ccw_test_failures = 0;
static int ccw_test_checks = 0;

#define CCW_CHECK(cond, ...)                                       \
    do {                                                           \
        ccw_test_checks++;                                         \
        if (!(cond)) {                                             \
            ccw_test_failures++;                                   \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);   \
            fprintf(stderr, __VA_ARGS__);                          \
            fprintf(stderr, "\n");                                 \
        }                                                          \
    } while (0)

#define CCW_CHECK_STREQ(a, b)                                      \
    CCW_CHECK((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0, \
              "expected \"%s\", got \"%s\"", (b) ? (b) : "(null)", \
              (a) ? (a) : "(null)")

static int ccw_test_report(const char *name)
{
    if (ccw_test_failures == 0) {
        printf("ok - %s (%d checks)\n", name, ccw_test_checks);
        return 0;
    }
    printf("not ok - %s (%d/%d checks failed)\n", name,
           ccw_test_failures, ccw_test_checks);
    return 1;
}

#endif /* CCW_TEST_H */
