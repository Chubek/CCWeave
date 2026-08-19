#include <stdio.h>
int
main (void)
{
  int value = 2;
  switch (value)
    {
    case 1:
      puts ("one");
      break;
    case 2:
      puts ("two");
      break;
    default:
      puts ("other");
    }
  return 0;
}
