#include "moonix_stdlib.h"
#include "../runtime/moonix_internal.h"

/* The v0.1 T0 core owns the scalar environment.  Library registration is
 * deliberately explicit and host-owned; no external Lua ABI is involved. */
int
moonix_install_stdlib (moonix_state *state)
{
  return state != NULL;
}
