/* salvo-libc <assert.h> backing routine. */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void salvo_write_all(const char *s)
{
    size_t left = strlen(s);
    while (left != 0) {
        ssize_t n = write(STDERR_FILENO, s, left);
        if (n <= 0)
            return;
        s += (size_t)n;
        left -= (size_t)n;
    }
}

void __assert_fail(const char *expr, const char *file,
                   int line, const char *func)
{
    char digits[16];
    int ndigits = 0;
    unsigned value = (unsigned)line;

    if (value == 0) {
        digits[ndigits++] = '0';
    } else {
        while (value != 0 && ndigits < (int)sizeof(digits)) {
            digits[ndigits++] = (char)('0' + value % 10);
            value /= 10;
        }
    }

    salvo_write_all("Assertion failed: ");
    salvo_write_all(expr != NULL ? expr : "?");
    salvo_write_all(" (");
    salvo_write_all(file != NULL ? file : "?");
    salvo_write_all(":");
    while (ndigits > 0) {
        char c = digits[--ndigits];
        ssize_t ignored = write(STDERR_FILENO, &c, 1);
        (void)ignored;
    }
    salvo_write_all(", ");
    salvo_write_all(func != NULL ? func : "?");
    salvo_write_all(")\n");
    abort();
}
