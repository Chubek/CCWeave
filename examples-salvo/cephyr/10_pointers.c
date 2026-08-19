#include <stdio.h>
int main(void) { int value = 41; int *p = &value; ++*p; printf("%d\n", value); return 0; }
