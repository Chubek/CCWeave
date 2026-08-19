/* salvo-libc <stdlib.h> — allocation, conversion, process control. */

#ifndef SALVO_STDLIB_H
#define SALVO_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

/* Largest allocation the v0.1 sbrk arena hands out in one request. */
#define SALVO_MALLOC_MAX ((size_t)1 << 40)

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

int          abs(int x);
long         labs(long x);
long long    llabs(long long x);

int          atoi(const char *s);
long         atol(const char *s);
long         strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
long long    strtoll(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);

void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

void  exit(int status);
void  _Exit(int status);
int   atexit(void (*function)(void));
void  abort(void);
char *getenv(const char *name);

extern char **environ;

#ifdef __cplusplus
}
#endif

#endif /* SALVO_STDLIB_H */
