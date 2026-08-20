#include <stdio.h>
int
main (void)
{
  int g[4][4]
      = { { 0, 1, 1, 0 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 0 } },
      in[4] = { 0 }, q[4], h = 0, t = 0;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      in[j] += g[i][j];
  for (int i = 0; i < 4; i++)
    if (!in[i])
      q[t++] = i;
  while (h < t)
    {
      int x = q[h++];
      printf ("%d%s", x + 1, h == t ? "\n" : " ");
      for (int i = 0; i < 4; i++)
        if (g[x][i] && !--in[i])
          q[t++] = i;
    }
}
