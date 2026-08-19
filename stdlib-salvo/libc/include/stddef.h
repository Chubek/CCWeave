/* salvo-libc <stddef.h> — §stdlib-salvo stage 10.
 *
 * Freestanding definitions for LP64 targets (x86-64, aarch64, riscv64).
 * salvo-libc v0.1 is LP64-only; ILP32 targets (wasm32) are out of scope. */

#ifndef SALVO_STDDEF_H
#define SALVO_STDDEF_H

#if !defined(__LP64__) && !defined(_LP64)
#error "salvo-libc currently supports LP64 targets only"
#endif

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef int wchar_t;
typedef unsigned int wint_t;

/* max_align_t: the largest fundamental alignment on the supported ABIs. */
typedef struct
{
  long long __salvo_ll;
  long double __salvo_ld;
} max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif /* SALVO_STDDEF_H */
