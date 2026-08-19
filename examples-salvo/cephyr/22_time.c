#include <stdio.h>
#include <time.h>
int
main (void)
{
  struct timespec ts;
  int status = clock_gettime (CLOCK_MONOTONIC, &ts);
  printf ("%d\n", status == 0);
  return 0;
}
