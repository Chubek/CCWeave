/* Compile-time configuration for the vendored S7 (s7.c includes
 * "mus-config.h" and expects the embedder to supply it). Keeping it
 * here means third_party/s7 is never edited. */

#ifndef CCW_MUS_CONFIG_H
#define CCW_MUS_CONFIG_H

#define HAVE_COMPLEX_NUMBERS 0
#define HAVE_COMPLEX_TRIG 0
#define WITH_GMP 0
#define WITH_MULTITHREAD_CHECKS 0
#define WITH_SYSTEM_EXTRAS 0
#define WITH_C_LOADER 0
#define DISABLE_DEPRECATED 1

#endif /* CCW_MUS_CONFIG_H */
