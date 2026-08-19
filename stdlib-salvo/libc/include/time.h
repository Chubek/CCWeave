/* salvo-libc <time.h> — wall clock via clock_gettime(2). */

#ifndef SALVO_TIME_H
#define SALVO_TIME_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef int clockid_t;

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

  struct timespec
  {
    time_t tv_sec;
    long tv_nsec;
  };

  time_t time (time_t *tloc);
  int clock_gettime (clockid_t clock_id, struct timespec *ts);

#ifdef __cplusplus
}
#endif

#endif /* SALVO_TIME_H */
