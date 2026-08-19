#include <stdio.h>
#include <stdlib.h>
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(void) { int a[] = {5, 1, 4, 2, 3}; qsort(a, 5, sizeof(a[0]), cmp); printf("%d %d\n", a[0], a[4]); return 0; }
