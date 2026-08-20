#include <stdio.h>
int
main (void)
{
  int a[] = { 10, 9, 2, 5, 3, 7, 101, 18 }, d[8], best = 0;
  for (int i = 0; i < 8; i++)
    {
      d[i] = 1;
      for (int j = 0; j < i; j++)
        if (a[j] < a[i] && d[j] + 1 > d[i])
          d[i] = d[j] + 1;
      if (d[i] > best)
        best = d[i];
    }
  printf ("%d\n", best);
}
