#include <stdio.h>
int
main (void)
{
  int g[4][4]
      = { { 0, 1, 4, 0 }, { 1, 0, 3, 3 }, { 4, 3, 0, 1 }, { 0, 3, 1, 0 } },
      h[4] = { 4, 3, 1, 0 }, d[4] = { 0, 999, 999, 999 }, u[4] = { 0 };
  for (int k = 0; k < 4; k++)
    {
      int x = -1;
      for (int i = 0; i < 4; i++)
        if (!u[i] && (x < 0 || d[i] + h[i] < d[x] + h[x]))
          x = i;
      u[x] = 1;
      for (int i = 0; i < 4; i++)
        if (g[x][i] && d[i] > d[x] + g[x][i])
          d[i] = d[x] + g[x][i];
    }
  printf ("%d\n", d[3]);
}
