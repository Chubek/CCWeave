#include <stdio.h>
static inline int
clamp (int x)
{
  return x < 0 ? 0 : x > 10 ? 10 : x;
}
int
main (void)
{
  printf ("%d %d\n", clamp (-2), clamp (12));
  return 0;
}
