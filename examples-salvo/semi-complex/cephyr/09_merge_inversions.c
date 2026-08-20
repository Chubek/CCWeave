#include <stdio.h>
long
merge (int *a, int *t, int l, int m, int r)
{
  long n = 0;
  int i = l, j = m + 1, k = l;
  while (i <= m && j <= r)
    if (a[i] <= a[j])
      t[k++] = a[i++];
    else
      {
        t[k++] = a[j++];
        n += m - i + 1;
      }
  while (i <= m)
    t[k++] = a[i++];
  while (j <= r)
    t[k++] = a[j++];
  for (i = l; i <= r; i++)
    a[i] = t[i];
  return n;
}
long
sort (int *a, int *t, int l, int r)
{
  if (l >= r)
    return 0;
  int m = (l + r) / 2;
  return sort (a, t, l, m) + sort (a, t, m + 1, r) + merge (a, t, l, m, r);
}
int
main (void)
{
  int a[] = { 2, 4, 1, 3, 5 }, t[5];
  printf ("%ld\n", sort (a, t, 0, 4));
}
