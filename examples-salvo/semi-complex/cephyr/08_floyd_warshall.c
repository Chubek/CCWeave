#include <stdio.h>
int
main (void)
{
  int d[4][4] = {
    { 0, 3, 999, 7 }, { 8, 0, 2, 999 }, { 5, 999, 0, 1 }, { 2, 999, 999, 0 }
  };
  for (int k = 0; k < 4; k++)
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        if (d[i][k] + d[k][j] < d[i][j])
          d[i][j] = d[i][k] + d[k][j];
  printf ("%d\n", d[0][3]);
}
