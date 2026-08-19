#include <stdio.h>
int
main (void)
{
  int a[4] = { 2, 4, 6, 8 };
  int total = 0;
  for (int i = 0; i < 4; ++i)
    total += a[i];
  printf ("%d\n", total);
  return 0;
}
