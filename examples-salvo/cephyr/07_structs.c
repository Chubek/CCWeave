#include <stdio.h>
struct point { int x; int y; };
int main(void) { struct point p = {3, 4}; printf("%d\n", p.x * p.x + p.y * p.y); return 0; }
