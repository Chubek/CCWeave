#include <stdio.h>
union number
{
  int i;
  float f;
};
int
main (void)
{
  union number n = { .i = 42 };
  printf ("%d\n", n.i);
  return 0;
}
