#include <stdio.h>
#include <string.h>
int
main (void)
{
  const char *t = "ababcabcabababd", *p = "ababd";
  int l[5] = { 0 }, j = 0, i;
  for (i = 1; i < 5; i++)
    {
      while (j && p[i] != p[j])
        j = l[j - 1];
      if (p[i] == p[j])
        j++;
      l[i] = j;
    }
  j = 0;
  for (i = 0; t[i]; i++)
    {
      while (j && t[i] != p[j])
        j = l[j - 1];
      if (t[i] == p[j])
        j++;
      if (j == 5)
        {
          printf ("%d\n", i - 4);
          return 0;
        }
    }
  printf ("-1\n");
}
