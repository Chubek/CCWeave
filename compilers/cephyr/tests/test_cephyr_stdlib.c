#define _POSIX_C_SOURCE 200809L

#include "../stdlib/cephyr_stdlib.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CEPHYR_STDLIB_FIXTURE_DIR
#define CEPHYR_STDLIB_FIXTURE_DIR "."
#endif

static void
path_join (char *out, size_t capacity, const char *directory, const char *name)
{
  int written = snprintf (out, capacity, "%s/%s", directory, name);
  assert (written > 0 && (size_t)written < capacity);
}

static void
test_load_global_and_target_merge (void)
{
  char path[1024];
  char expected[1024];
  char error[256] = { 0 };
  cephyr_stdlib stdlib;

  path_join (path, sizeof (path), CEPHYR_STDLIB_FIXTURE_DIR, "stdlib.yaml");
  memset (&stdlib, 0, sizeof (stdlib));
  assert (cephyr_stdlib_load (path, "x86_64-linux-gnu", &stdlib, error,
                              sizeof (error)));
  assert (stdlib.version == 1);
  assert (stdlib.name != NULL && strcmp (stdlib.name, "fixture-libc") == 0);
  assert (stdlib.manifest_path != NULL
          && strcmp (stdlib.manifest_path, path) == 0);

  /* Global entries resolve relative to the manifest directory;
   * absolute entries pass through. */
  assert (stdlib.include_dir_count == 3);
  path_join (expected, sizeof (expected), CEPHYR_STDLIB_FIXTURE_DIR,
             "include");
  assert (strcmp (stdlib.include_dirs[0], expected) == 0);
  assert (strcmp (stdlib.include_dirs[1], "/absolute/include") == 0);
  /* Matched target sequences merge after the global lists. */
  path_join (expected, sizeof (expected), CEPHYR_STDLIB_FIXTURE_DIR,
             "arch/x86_64/include");
  assert (strcmp (stdlib.include_dirs[2], expected) == 0);

  assert (stdlib.library_path_count == 1);
  path_join (expected, sizeof (expected), CEPHYR_STDLIB_FIXTURE_DIR, "lib");
  assert (strcmp (stdlib.library_paths[0], expected) == 0);

  assert (stdlib.library_count == 1);
  assert (strcmp (stdlib.libraries[0], "salvoc") == 0);

  assert (stdlib.start_file_count == 2);
  path_join (expected, sizeof (expected), CEPHYR_STDLIB_FIXTURE_DIR,
             "lib/crt0.o");
  assert (strcmp (stdlib.start_files[0], expected) == 0);
  path_join (expected, sizeof (expected), CEPHYR_STDLIB_FIXTURE_DIR,
             "arch/x86_64/crt0.o");
  assert (strcmp (stdlib.start_files[1], expected) == 0);
  cephyr_stdlib_destroy (&stdlib);
  assert (stdlib.include_dirs == NULL && stdlib.include_dir_count == 0);
}

static void
test_load_other_target (void)
{
  char path[1024];
  char error[256] = { 0 };
  cephyr_stdlib stdlib;

  path_join (path, sizeof (path), CEPHYR_STDLIB_FIXTURE_DIR, "stdlib.yaml");
  memset (&stdlib, 0, sizeof (stdlib));
  assert (cephyr_stdlib_load (path, "riscv64-linux-gnu", &stdlib, error,
                              sizeof (error)));
  /* No include merge for riscv64; library lists merge. */
  assert (stdlib.include_dir_count == 2);
  assert (stdlib.library_count == 2);
  assert (strcmp (stdlib.libraries[0], "salvoc") == 0);
  assert (strcmp (stdlib.libraries[1], "salvoc-riscv") == 0);
  assert (stdlib.start_file_count == 1);
  cephyr_stdlib_destroy (&stdlib);

  /* An unmatched triple selects the global lists only. */
  memset (&stdlib, 0, sizeof (stdlib));
  assert (cephyr_stdlib_load (path, "wasm32-wasi", &stdlib, error,
                              sizeof (error)));
  assert (stdlib.include_dir_count == 2);
  assert (stdlib.library_count == 1);
  assert (stdlib.start_file_count == 1);
  cephyr_stdlib_destroy (&stdlib);
}

static void
test_load_failure (void)
{
  char error[256] = { 0 };
  cephyr_stdlib stdlib;
  memset (&stdlib, 0, sizeof (stdlib));
  assert (!cephyr_stdlib_load ("/nonexistent/stdlib.yaml", NULL, &stdlib,
                               error, sizeof (error)));
  assert (error[0] != '\0');
}

static void
test_discovery_prefers_environment (void)
{
  char *manifest;
  int is_explicit = 0;

  setenv (CEPHYR_STDLIB_MANIFEST_ENV, "/custom/salvo.yaml", 1);
  manifest = cephyr_stdlib_discover_manifest (&is_explicit);
  assert (manifest != NULL);
  assert (is_explicit == 1);
  assert (strcmp (manifest, "/custom/salvo.yaml") == 0);
  free (manifest);

  unsetenv (CEPHYR_STDLIB_MANIFEST_ENV);
  is_explicit = 1;
  manifest = cephyr_stdlib_discover_manifest (&is_explicit);
  /* In-tree defaults depend on the working directory; only the
   * explicitness contract is asserted. */
  assert (is_explicit == 0);
  free (manifest);
}

int
main (void)
{
  test_load_global_and_target_merge ();
  test_load_other_target ();
  test_load_failure ();
  test_discovery_prefers_environment ();
  return 0;
}
