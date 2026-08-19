#include <stdio.h>
int main(void) { int a = 0, b = 1; for (int i = 0; i < 10; ++i) { int next = a + b; a = b; b = next; } printf("%d\n", a); return 0; }
