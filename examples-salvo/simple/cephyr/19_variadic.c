#include <stdarg.h>
#include <stdio.h>
static int
total (int count, ...)
{
  va_list ap;
  int sum = 0;
  va_start (ap, count);
  for (int i = 0; i < count; ++i)
    sum += va_arg (ap, int);
  va_end (ap);
  return sum;
}
int
main (void)
{
  printf ("%d\n", total (4, 1, 2, 3, 4));
  return 0;
}
