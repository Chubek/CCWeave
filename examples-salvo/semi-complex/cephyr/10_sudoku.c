#include <stdio.h>
int b[9][9] = { { 5, 3, 0, 0, 7, 0, 0, 0, 0 }, { 6, 0, 0, 1, 9, 5, 0, 0, 0 },
                { 0, 9, 8, 0, 0, 0, 0, 6, 0 }, { 8, 0, 0, 0, 6, 0, 0, 0, 3 },
                { 4, 0, 0, 8, 0, 3, 0, 0, 1 }, { 7, 0, 0, 0, 2, 0, 0, 0, 6 },
                { 0, 6, 0, 0, 0, 0, 2, 8, 0 }, { 0, 0, 0, 4, 1, 9, 0, 0, 5 },
                { 0, 0, 0, 0, 8, 0, 0, 7, 9 } };
int
ok (int r, int c, int n)
{
  for (int i = 0; i < 9; i++)
    if (b[r][i] == n || b[i][c] == n
        || b[r / 3 * 3 + i / 3][c / 3 * 3 + i % 3] == n)
      return 0;
  return 1;
}
int
solve (void)
{
  for (int r = 0; r < 9; r++)
    for (int c = 0; c < 9; c++)
      if (!b[r][c])
        {
          for (int n = 1; n <= 9; n++)
            if (ok (r, c, n))
              {
                b[r][c] = n;
                if (solve ())
                  return 1;
                b[r][c] = 0;
              }
          return 0;
        }
  return 1;
}
int
main (void)
{
  solve ();
  printf ("%d %d\n", b[0][0], b[8][8]);
}
