#include <stdio.h>
int main(void) { unsigned x = 0x5a; printf("%u %u\n", x & 0xf, x ^ 0xff); return 0; }
