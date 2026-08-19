#include <stdio.h>
static int
find (const int *a, int n, int needle)
{
  for (int i = 0; i < n; ++i)
    if (a[i] == needle)
      return i;
  return -1;
}
int
main (void)
{
  int a[] = { 4, 8, 15, 16, 23, 42 };
  printf ("%d\n", find (a, 6, 23));
  return 0;
}
