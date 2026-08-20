#include <stdio.h>
int
main (void)
{
  int w[] = { 2, 3, 4, 5 }, v[] = { 3, 4, 5, 6 }, d[9] = { 0 };
  for (int i = 0; i < 4; i++)
    for (int c = 8; c >= w[i]; c--)
      if (d[c - w[i]] + v[i] > d[c])
        d[c] = d[c - w[i]] + v[i];
  printf ("%d\n", d[8]);
}
