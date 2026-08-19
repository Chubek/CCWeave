/* salvo-libc: absolute values. */

#include <stdlib.h>

int abs(int x)           { return x < 0 ? -x : x; }
long labs(long x)        { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }
