#ifndef CCWEAVE_ISL_CONFIG_POST_H
#define CCWEAVE_ISL_CONFIG_POST_H
#ifndef HAVE___ATTRIBUTE__
#define __attribute__(x)
#endif
#ifdef GCC_WARN_UNUSED_RESULT
#define WARN_UNUSED GCC_WARN_UNUSED_RESULT
#else
#define WARN_UNUSED
#endif
#endif
