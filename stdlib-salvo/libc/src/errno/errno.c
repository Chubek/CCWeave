/* salvo-libc <errno.h> backing storage.
 *
 * Single-threaded v0.1: one global behind __errno_location. The macro
 * indirection in the public header keeps the ABI compatible with a
 * future per-thread errno. */

static int salvo_errno_storage;

int *
__errno_location (void)
{
  return &salvo_errno_storage;
}
