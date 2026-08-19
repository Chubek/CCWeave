/* salvo-libc: block memory operations (memcpy, memmove, memset, memcmp,
 * memchr). Word-at-a-time where alignment permits; the compiler is free
 * to inline these at call sites, these definitions back the ABI. */

#include <string.h>
#include <stdint.h>

#define SALVO_WORD unsigned long
#define SALVO_WORD_SIZE sizeof(SALVO_WORD)

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (n >= SALVO_WORD_SIZE &&
        ((uintptr_t)d & (SALVO_WORD_SIZE - 1)) ==
        ((uintptr_t)s & (SALVO_WORD_SIZE - 1))) {
        while ((uintptr_t)d & (SALVO_WORD_SIZE - 1)) {
            *d++ = *s++;
            --n;
        }
        {
            SALVO_WORD *dw = (SALVO_WORD *)d;
            const SALVO_WORD *sw = (const SALVO_WORD *)s;
            while (n >= SALVO_WORD_SIZE) {
                *dw++ = *sw++;
                n -= SALVO_WORD_SIZE;
            }
            d = (unsigned char *)dw;
            s = (const unsigned char *)sw;
        }
    }
    while (n != 0) {
        *d++ = *s++;
        --n;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s)
        return memcpy(dst, src, n);
    /* Copy backwards so overlapping regions survive. */
    while (n != 0) {
        --n;
        d[n] = s[n];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char byte = (unsigned char)c;

    if (n >= SALVO_WORD_SIZE) {
        SALVO_WORD pattern = byte;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        pattern |= pattern << 32;
        while ((uintptr_t)d & (SALVO_WORD_SIZE - 1)) {
            *d++ = byte;
            --n;
        }
        {
            SALVO_WORD *dw = (SALVO_WORD *)d;
            while (n >= SALVO_WORD_SIZE) {
                *dw++ = pattern;
                n -= SALVO_WORD_SIZE;
            }
            d = (unsigned char *)dw;
        }
    }
    while (n != 0) {
        *d++ = byte;
        --n;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;

    while (n != 0) {
        if (*pa != *pb)
            return (int)*pa - (int)*pb;
        ++pa;
        ++pb;
        --n;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char byte = (unsigned char)c;

    while (n != 0) {
        if (*p == byte)
            return (void *)p;
        ++p;
        --n;
    }
    return NULL;
}
