#include <stdio.h>
struct rgb { int r; int g; int b; };
int main(void) { struct rgb c = {.b = 255, .r = 12}; printf("%d %d\n", c.r, c.b); return 0; }
