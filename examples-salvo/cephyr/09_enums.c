#include <stdio.h>
enum color { RED, GREEN, BLUE };
int main(void) { enum color c = GREEN; printf("%d\n", (int)c); return 0; }
