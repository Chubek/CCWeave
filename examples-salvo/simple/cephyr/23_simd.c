#include <immintrin.h>
#include <stdio.h>
int
main (void)
{
  float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, out[4];
  _mm_storeu_ps (out, _mm_add_ps (_mm_loadu_ps (a), _mm_loadu_ps (b)));
  printf ("%.0f\n", out[0]);
  return 0;
}
