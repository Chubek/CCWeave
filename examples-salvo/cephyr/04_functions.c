#include <stdio.h>
static int
square (int x)
{
  return x * x;
}
int
main (void)
{
  printf ("%d\n", square (12));
  return 0;
}
