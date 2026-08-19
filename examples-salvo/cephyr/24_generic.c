#include <stdio.h>
#define name(x) _Generic((x), int: "int", double: "double", default: "other")
int main(void) { printf("%s %s\n", name(1), name(1.0)); return 0; }
