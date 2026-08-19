#include <stdio.h>
static int
fact (int n)
{
  return n < 2 ? 1 : n * fact (n - 1);
}
int
main (void)
{
  printf ("%d\n", fact (6));
  return 0;
}
