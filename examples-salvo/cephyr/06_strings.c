#include <stdio.h>
#include <string.h>
int main(void) { char text[32] = "cephyr"; strcat(text, " salvo"); printf("%zu %s\n", strlen(text), text); return 0; }
