#include <stdio.h>
static int
add (int a, int b)
{
  return a + b;
}
int
main (void)
{
  int (*op) (int, int) = add;
  printf ("%d\n", op (20, 22));
  return 0;
}
