#include <stdio.h>
typedef struct
{
  int a, b, w;
} E;
int
f (int *p, int x)
{
  return p[x] == x ? x : (p[x] = f (p, p[x]));
}
int
main (void)
{
  E e[] = { { 0, 1, 1 }, { 1, 2, 2 }, { 0, 2, 3 }, { 2, 3, 1 }, { 1, 3, 4 } };
  int p[] = { 0, 1, 2, 3 }, t = 0;
  for (int k = 0; k < 5; k++)
    for (int j = k + 1; j < 5; j++)
      if (e[j].w < e[k].w)
        {
          E z = e[k];
          e[k] = e[j];
          e[j] = z;
        }
  for (int k = 0; k < 5; k++)
    {
      int a = f (p, e[k].a), b = f (p, e[k].b);
      if (a != b)
        {
          p[a] = b;
          t += e[k].w;
        }
    }
  printf ("%d\n", t);
}
