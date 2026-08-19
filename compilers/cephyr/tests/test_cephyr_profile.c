#define _POSIX_C_SOURCE 200809L

#include "../profile/cephyr_profile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CEPHYR_PROFILE_FIXTURE_DIR
#define CEPHYR_PROFILE_FIXTURE_DIR "."
#endif

static void
path_join (char *out, size_t capacity, const char *directory, const char *name)
{
  int written = snprintf (out, capacity, "%s/%s", directory, name);
  assert (written > 0 && (size_t)written < capacity);
}

static void
test_yaml (void)
{
  char path[1024];
  char error[256] = { 0 };
  cephyr_profile profile;
  path_join (path, sizeof (path), CEPHYR_PROFILE_FIXTURE_DIR, "profile.yaml");
  memset (&profile, 0, sizeof (profile));
  assert (cephyr_profile_load (path, &profile, error, sizeof (error)));
  assert (profile.version == 1);
  assert (strcmp (profile.name, "yaml-test") == 0);
  assert (strcmp (profile.opt_level, "O1") == 0);
  assert (profile.include_path_count == 1);
  assert (profile.define_count == 1);
  assert (profile.preprocessor_option_count == 1);
  assert (profile.preprocessor_arg_count == 1);
  assert (profile.assembler_option_count == 1);
  assert (profile.assembler_arg_count == 1);
  assert (profile.linker_option_count == 1);
  assert (profile.linker_arg_count == 1);
  assert (profile.library_path_count == 1);
  assert (profile.library_count == 1);
  assert (profile.pic && !profile.pie && !profile.shared);
  assert (profile.rewrite_count == 0);
  assert (profile.command_count == 1);
  assert (strcmp (profile.sched_script, "compilers/cephyr/sched/O1.lua") == 0);
  assert (cephyr_profile_find_command (&profile, "check") != NULL);
  cephyr_profile_destroy (&profile);
}

static void
test_toml (void)
{
  char path[1024];
  char error[256] = { 0 };
  cephyr_profile profile;
  path_join (path, sizeof (path), CEPHYR_PROFILE_FIXTURE_DIR, "profile.toml");
  memset (&profile, 0, sizeof (profile));
  assert (cephyr_profile_load (path, &profile, error, sizeof (error)));
  assert (profile.version == 1);
  assert (strcmp (profile.name, "toml-test") == 0);
  assert (strcmp (profile.opt_level, "O2") == 0);
  assert (profile.kernel_count == 1);
  assert (profile.kernels[0].capability != NULL);
  assert (strcmp (profile.kernels[0].prefer, "strength-reduce") == 0);
  assert (profile.rewrite_count == 1);
  assert (profile.preprocessor_option_count == 1);
  assert (profile.assembler_option_count == 1);
  assert (profile.linker_option_count == 1);
  assert (profile.library_path_count == 1);
  assert (profile.library_count == 1);
  assert (profile.pic && !profile.pie && !profile.shared);
  cephyr_profile_destroy (&profile);
}

static void
test_init_and_discovery (void)
{
  char directory[] = "/tmp/cephyr-profile-test-XXXXXX";
  char yaml_path[1024];
  char toml_path[1024];
  char error[256] = { 0 };
  char *discovered;
  cephyr_profile profile;

  assert (mkdtemp (directory) != NULL);
  path_join (yaml_path, sizeof (yaml_path), directory, "CEPHYR.yaml");
  path_join (toml_path, sizeof (toml_path), directory, "CEPHYR.toml");
  assert (cephyr_profile_init_file (yaml_path, CEPHYR_PROFILE_YAML, error,
                                    sizeof (error)));
  memset (&profile, 0, sizeof (profile));
  assert (cephyr_profile_load (yaml_path, &profile, error, sizeof (error)));
  assert (profile.version == 1);
  cephyr_profile_destroy (&profile);
  assert (!cephyr_profile_init_file (yaml_path, CEPHYR_PROFILE_YAML, error,
                                     sizeof (error)));

  assert (cephyr_profile_init_file (toml_path, CEPHYR_PROFILE_TOML, error,
                                    sizeof (error)));
  discovered = cephyr_profile_discover (directory, error, sizeof (error));
  assert (discovered != NULL);
  assert (strcmp (discovered, yaml_path) == 0);
  free (discovered);

  unlink (yaml_path);
  discovered = cephyr_profile_discover (directory, error, sizeof (error));
  assert (discovered != NULL);
  assert (strcmp (discovered, toml_path) == 0);
  free (discovered);
  unlink (toml_path);
  rmdir (directory);
}

int
main (void)
{
  test_yaml ();
  test_toml ();
  test_init_and_discovery ();
  return 0;
}
