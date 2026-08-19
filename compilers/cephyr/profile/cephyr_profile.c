#define _POSIX_C_SOURCE 200809L

#include "cephyr_profile.h"

#include <cyaml/cyaml.h>
#include <toml.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kstring.h"
#include "kvec.h"

typedef struct
{
  const char *name;
  const char *capability;
  const char *prefer;
} yaml_kernel;

typedef struct
{
  const char *name;
  const char *command;
} yaml_command;

typedef struct
{
  int version;
  const char *name;
  const char *target;
  const char *opt_level;
  const char *preprocessor;
  const char *manifest_dir;
  const char *sched_script;
  const char *assembler;
  const char *linker;
  const char **include_paths;
  uint64_t include_paths_count;
  const char **defines;
  uint64_t defines_count;
  const char **preprocessor_options;
  uint64_t preprocessor_options_count;
  const char **preprocessor_args;
  uint64_t preprocessor_args_count;
  const char **assembler_options;
  uint64_t assembler_options_count;
  const char **assembler_args;
  uint64_t assembler_args_count;
  const char **linker_options;
  uint64_t linker_options_count;
  const char **linker_args;
  uint64_t linker_args_count;
  const char **library_paths;
  uint64_t library_paths_count;
  const char **libraries;
  uint64_t libraries_count;
  bool pic;
  bool pie;
  bool shared;
  yaml_kernel *kernels;
  uint64_t kernels_count;
  const char **rewrites;
  uint64_t rewrites_count;
  yaml_command *commands;
  uint64_t commands_count;
} yaml_profile;

static void
profile_error (char *out, size_t capacity, const char *fmt, ...)
{
  va_list ap;
  if (out == NULL || capacity == 0)
    return;
  va_start (ap, fmt);
  vsnprintf (out, capacity, fmt, ap);
  va_end (ap);
}

static char *
profile_strdup (const char *value)
{
  kstring_t copy = { 0, 0, NULL };
  if (value == NULL)
    return NULL;
  if (kputs (value, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

static int
profile_set_string (char **dst, const char *src)
{
  char *copy;
  if (src == NULL)
    return 1;
  copy = profile_strdup (src);
  if (copy == NULL)
    return 0;
  free (*dst);
  *dst = copy;
  return 1;
}

static int
profile_push_string (char ***items, size_t *count, const char *value)
{
  char *copy;
  kvec_t (char *) vector = { *count, *count, *items };
  if (value == NULL)
    return 0;
  copy = profile_strdup (value);
  if (copy == NULL)
    return 0;
  if (kv_resize (char *, vector, vector.n + 1u) == NULL)
    {
      free (copy);
      return 0;
    }
  vector.a[vector.n++] = copy;
  *items = vector.a;
  *count = vector.n;
  return 1;
}

static int
profile_push_kernel (cephyr_profile *profile, const char *name,
                     const char *capability, const char *prefer)
{
  cephyr_profile_kernel *item;
  kvec_t (cephyr_profile_kernel) vector
      = { profile->kernel_count, profile->kernel_count, profile->kernels };
  if (kv_resize (cephyr_profile_kernel, vector, vector.n + 1u) == NULL)
    return 0;
  profile->kernels = vector.a;
  item = &profile->kernels[profile->kernel_count];
  memset (item, 0, sizeof (*item));
  if ((name != NULL && !(item->name = profile_strdup (name)))
      || (capability != NULL
          && !(item->capability = profile_strdup (capability)))
      || (prefer != NULL && !(item->prefer = profile_strdup (prefer))))
    {
      free (item->name);
      free (item->capability);
      free (item->prefer);
      return 0;
    }
  ++profile->kernel_count;
  return 1;
}

static int
profile_push_command (cephyr_profile *profile, const char *name,
                      const char *command)
{
  cephyr_profile_command *item;
  kvec_t (cephyr_profile_command) vector
      = { profile->command_count, profile->command_count, profile->commands };
  if (name == NULL || command == NULL)
    return 0;
  if (kv_resize (cephyr_profile_command, vector, vector.n + 1u) == NULL)
    return 0;
  profile->commands = vector.a;
  item = &profile->commands[profile->command_count];
  item->name = profile_strdup (name);
  item->command = profile_strdup (command);
  if (item->name == NULL || item->command == NULL)
    {
      free (item->name);
      free (item->command);
      return 0;
    }
  ++profile->command_count;
  return 1;
}

void
cephyr_profile_init (cephyr_profile *profile)
{
  if (profile == NULL)
    return;
  memset (profile, 0, sizeof (*profile));
  profile->version = 1;
  profile->name = profile_strdup ("my-cephyr-profile");
  profile->target_triple = profile_strdup ("x86_64-linux-gnu");
  profile->opt_level = profile_strdup ("O0");
  profile->preprocessor = profile_strdup ("ucpp");
  profile->manifest_dir = profile_strdup ("manifests");
}

void
cephyr_profile_destroy (cephyr_profile *profile)
{
  if (profile == NULL)
    return;
  free (profile->name);
  free (profile->target_triple);
  free (profile->opt_level);
  free (profile->preprocessor);
  free (profile->manifest_dir);
  free (profile->sched_script);
  free (profile->assembler);
  free (profile->linker);
  for (size_t i = 0; i < profile->include_path_count; ++i)
    free (profile->include_paths[i]);
  for (size_t i = 0; i < profile->define_count; ++i)
    free (profile->defines[i]);
  for (size_t i = 0; i < profile->preprocessor_option_count; ++i)
    free (profile->preprocessor_options[i]);
  for (size_t i = 0; i < profile->preprocessor_arg_count; ++i)
    free (profile->preprocessor_args[i]);
  for (size_t i = 0; i < profile->assembler_option_count; ++i)
    free (profile->assembler_options[i]);
  for (size_t i = 0; i < profile->assembler_arg_count; ++i)
    free (profile->assembler_args[i]);
  for (size_t i = 0; i < profile->linker_option_count; ++i)
    free (profile->linker_options[i]);
  for (size_t i = 0; i < profile->linker_arg_count; ++i)
    free (profile->linker_args[i]);
  for (size_t i = 0; i < profile->library_path_count; ++i)
    free (profile->library_paths[i]);
  for (size_t i = 0; i < profile->library_count; ++i)
    free (profile->libraries[i]);
  for (size_t i = 0; i < profile->kernel_count; ++i)
    {
      free (profile->kernels[i].name);
      free (profile->kernels[i].capability);
      free (profile->kernels[i].prefer);
    }
  for (size_t i = 0; i < profile->rewrite_count; ++i)
    free (profile->rewrites[i]);
  for (size_t i = 0; i < profile->command_count; ++i)
    {
      free (profile->commands[i].name);
      free (profile->commands[i].command);
    }
  free (profile->include_paths);
  free (profile->defines);
  free (profile->preprocessor_options);
  free (profile->preprocessor_args);
  free (profile->assembler_options);
  free (profile->assembler_args);
  free (profile->linker_options);
  free (profile->linker_args);
  free (profile->library_paths);
  free (profile->libraries);
  free (profile->kernels);
  free (profile->rewrites);
  free (profile->commands);
  memset (profile, 0, sizeof (*profile));
}

static const cyaml_schema_value_t yaml_string_schema = {
  CYAML_VALUE_STRING (CYAML_FLAG_POINTER, char, 0, CYAML_UNLIMITED),
};

static const cyaml_schema_field_t yaml_kernel_fields[]
    = { CYAML_FIELD_STRING_PTR ("name", CYAML_FLAG_OPTIONAL, yaml_kernel, name,
                                0, CYAML_UNLIMITED),
        CYAML_FIELD_STRING_PTR ("capability", CYAML_FLAG_OPTIONAL, yaml_kernel,
                                capability, 0, CYAML_UNLIMITED),
        CYAML_FIELD_STRING_PTR ("prefer", CYAML_FLAG_OPTIONAL, yaml_kernel,
                                prefer, 0, CYAML_UNLIMITED),
        CYAML_FIELD_END };

static const cyaml_schema_value_t yaml_kernel_schema = {
  CYAML_VALUE_MAPPING (CYAML_FLAG_DEFAULT, yaml_kernel, yaml_kernel_fields),
};

static const cyaml_schema_field_t yaml_command_fields[]
    = { CYAML_FIELD_STRING_PTR ("name", CYAML_FLAG_DEFAULT, yaml_command, name,
                                0, CYAML_UNLIMITED),
        CYAML_FIELD_STRING_PTR ("command", CYAML_FLAG_DEFAULT, yaml_command,
                                command, 0, CYAML_UNLIMITED),
        CYAML_FIELD_END };

static const cyaml_schema_value_t yaml_command_schema = {
  CYAML_VALUE_MAPPING (CYAML_FLAG_DEFAULT, yaml_command, yaml_command_fields),
};

static const cyaml_schema_field_t yaml_profile_fields[] = {
  CYAML_FIELD_INT ("version", CYAML_FLAG_OPTIONAL, yaml_profile, version),
  CYAML_FIELD_STRING_PTR ("name", CYAML_FLAG_OPTIONAL, yaml_profile, name, 0,
                          CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("target", CYAML_FLAG_OPTIONAL, yaml_profile, target,
                          0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("opt_level", CYAML_FLAG_OPTIONAL, yaml_profile,
                          opt_level, 0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("preprocessor", CYAML_FLAG_OPTIONAL, yaml_profile,
                          preprocessor, 0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("manifest_dir", CYAML_FLAG_OPTIONAL, yaml_profile,
                          manifest_dir, 0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("sched_script", CYAML_FLAG_OPTIONAL, yaml_profile,
                          sched_script, 0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("assembler", CYAML_FLAG_OPTIONAL, yaml_profile,
                          assembler, 0, CYAML_UNLIMITED),
  CYAML_FIELD_STRING_PTR ("linker", CYAML_FLAG_OPTIONAL, yaml_profile, linker,
                          0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE (
      "include_paths", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
      include_paths, &yaml_string_schema, 0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("defines", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                        yaml_profile, defines, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("preprocessor_options",
                        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
                        preprocessor_options, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("preprocessor_args",
                        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
                        preprocessor_args, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("assembler_options",
                        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
                        assembler_options, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE (
      "assembler_args", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
      assembler_args, &yaml_string_schema, 0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE (
      "linker_options", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
      linker_options, &yaml_string_schema, 0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("linker_args",
                        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
                        linker_args, &yaml_string_schema, 0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE (
      "library_paths", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, yaml_profile,
      library_paths, &yaml_string_schema, 0, CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("libraries", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                        yaml_profile, libraries, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_BOOL ("pic", CYAML_FLAG_OPTIONAL, yaml_profile, pic),
  CYAML_FIELD_BOOL ("pie", CYAML_FLAG_OPTIONAL, yaml_profile, pie),
  CYAML_FIELD_BOOL ("shared", CYAML_FLAG_OPTIONAL, yaml_profile, shared),
  CYAML_FIELD_SEQUENCE ("kernels", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                        yaml_profile, kernels, &yaml_kernel_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("rewrites", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                        yaml_profile, rewrites, &yaml_string_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_SEQUENCE ("commands", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                        yaml_profile, commands, &yaml_command_schema, 0,
                        CYAML_UNLIMITED),
  CYAML_FIELD_END
};

static const cyaml_schema_value_t yaml_profile_schema = {
  CYAML_VALUE_MAPPING (CYAML_FLAG_POINTER, yaml_profile, yaml_profile_fields),
};

static const cyaml_config_t yaml_config
    = { .log_fn = cyaml_log,
        .mem_fn = cyaml_mem,
        .log_level = CYAML_LOG_ERROR,
        .flags = CYAML_CFG_IGNORE_UNKNOWN_KEYS };

static int
profile_from_yaml (const char *path, cephyr_profile *profile,
                   char *error_message, size_t error_capacity)
{
  yaml_profile *raw = NULL;
  cyaml_err_t rc;
  rc = cyaml_load_file (path, &yaml_config, &yaml_profile_schema,
                        (cyaml_data_t **)&raw, NULL);
  if (rc != CYAML_OK)
    {
      profile_error (error_message, error_capacity,
                     "YAML profile load failed: %s", cyaml_strerror (rc));
      return 0;
    }
  if (raw == NULL)
    {
      profile_error (error_message, error_capacity, "YAML profile is empty");
      return 0;
    }
  if (raw->version != 0 && raw->version != 1)
    {
      profile_error (error_message, error_capacity,
                     "unsupported profile version %d", raw->version);
      cyaml_free (&yaml_config, &yaml_profile_schema, raw, 0);
      return 0;
    }
  cephyr_profile_init (profile);
  if ((raw->name && !profile_set_string (&profile->name, raw->name))
      || (raw->target
          && !profile_set_string (&profile->target_triple, raw->target))
      || (raw->opt_level
          && !profile_set_string (&profile->opt_level, raw->opt_level))
      || (raw->preprocessor
          && !profile_set_string (&profile->preprocessor, raw->preprocessor))
      || (raw->manifest_dir
          && !profile_set_string (&profile->manifest_dir, raw->manifest_dir))
      || (raw->sched_script
          && !profile_set_string (&profile->sched_script, raw->sched_script))
      || (raw->assembler
          && !profile_set_string (&profile->assembler, raw->assembler))
      || (raw->linker && !profile_set_string (&profile->linker, raw->linker)))
    {
      profile_error (error_message, error_capacity,
                     "out of memory loading YAML profile");
      cyaml_free (&yaml_config, &yaml_profile_schema, raw, 0);
      cephyr_profile_destroy (profile);
      return 0;
    }
  profile->version = raw->version ? raw->version : 1;
  for (uint64_t i = 0; i < raw->include_paths_count; ++i)
    if (!profile_push_string (&profile->include_paths,
                              &profile->include_path_count,
                              raw->include_paths[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->defines_count; ++i)
    if (!profile_push_string (&profile->defines, &profile->define_count,
                              raw->defines[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->preprocessor_options_count; ++i)
    if (!profile_push_string (&profile->preprocessor_options,
                              &profile->preprocessor_option_count,
                              raw->preprocessor_options[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->preprocessor_args_count; ++i)
    if (!profile_push_string (&profile->preprocessor_args,
                              &profile->preprocessor_arg_count,
                              raw->preprocessor_args[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->assembler_options_count; ++i)
    if (!profile_push_string (&profile->assembler_options,
                              &profile->assembler_option_count,
                              raw->assembler_options[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->assembler_args_count; ++i)
    if (!profile_push_string (&profile->assembler_args,
                              &profile->assembler_arg_count,
                              raw->assembler_args[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->linker_options_count; ++i)
    if (!profile_push_string (&profile->linker_options,
                              &profile->linker_option_count,
                              raw->linker_options[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->linker_args_count; ++i)
    if (!profile_push_string (&profile->linker_args,
                              &profile->linker_arg_count, raw->linker_args[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->library_paths_count; ++i)
    if (!profile_push_string (&profile->library_paths,
                              &profile->library_path_count,
                              raw->library_paths[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->libraries_count; ++i)
    if (!profile_push_string (&profile->libraries, &profile->library_count,
                              raw->libraries[i]))
      goto oom;
  profile->pic = raw->pic;
  profile->pie = raw->pie;
  profile->shared = raw->shared;
  for (uint64_t i = 0; i < raw->kernels_count; ++i)
    if (!profile_push_kernel (profile, raw->kernels[i].name,
                              raw->kernels[i].capability,
                              raw->kernels[i].prefer))
      goto oom;
  for (uint64_t i = 0; i < raw->rewrites_count; ++i)
    if (!profile_push_string (&profile->rewrites, &profile->rewrite_count,
                              raw->rewrites[i]))
      goto oom;
  for (uint64_t i = 0; i < raw->commands_count; ++i)
    if (!profile_push_command (profile, raw->commands[i].name,
                               raw->commands[i].command))
      goto oom;
  cyaml_free (&yaml_config, &yaml_profile_schema, raw, 0);
  return 1;
oom:
  profile_error (error_message, error_capacity,
                 "out of memory loading YAML profile");
  cyaml_free (&yaml_config, &yaml_profile_schema, raw, 0);
  cephyr_profile_destroy (profile);
  return 0;
}

static int
toml_copy_string (toml_table_t *table, const char *key, char **dst)
{
  toml_datum_t value = toml_string_in (table, key);
  int ok = 1;
  if (!value.ok)
    return 1;
  ok = profile_set_string (dst, value.u.s);
  free (value.u.s);
  return ok;
}

static int
toml_copy_string_array (toml_table_t *root, const char *key, char ***items,
                        size_t *count)
{
  toml_array_t *array = toml_array_in (root, key);
  for (int i = 0; array != NULL && i < toml_array_nelem (array); ++i)
    {
      toml_datum_t value = toml_string_at (array, i);
      if (!value.ok || !profile_push_string (items, count, value.u.s))
        {
          if (value.ok)
            free (value.u.s);
          return 0;
        }
      free (value.u.s);
    }
  return 1;
}

static int
profile_from_toml (const char *path, cephyr_profile *profile,
                   char *error_message, size_t error_capacity)
{
  FILE *file = NULL;
  char parse_error[256] = { 0 };
  toml_table_t *root = NULL;
  toml_array_t *array;
  toml_datum_t integer;

  file = fopen (path, "r");
  if (file == NULL)
    {
      profile_error (error_message, error_capacity,
                     "cannot open TOML profile '%s': %s", path,
                     strerror (errno));
      return 0;
    }
  root = toml_parse_file (file, parse_error, (int)sizeof (parse_error));
  fclose (file);
  if (root == NULL)
    {
      profile_error (error_message, error_capacity,
                     "TOML profile parse failed: %s",
                     parse_error[0] ? parse_error : "invalid TOML");
      return 0;
    }
  cephyr_profile_init (profile);
  integer = toml_int_in (root, "version");
  if (integer.ok)
    profile->version = (int)integer.u.i;
  if (profile->version != 1)
    {
      profile_error (error_message, error_capacity,
                     "unsupported profile version %d", profile->version);
      toml_free (root);
      cephyr_profile_destroy (profile);
      return 0;
    }
  if (!toml_copy_string (root, "name", &profile->name)
      || !toml_copy_string (root, "target", &profile->target_triple)
      || !toml_copy_string (root, "opt_level", &profile->opt_level)
      || !toml_copy_string (root, "preprocessor", &profile->preprocessor)
      || !toml_copy_string (root, "manifest_dir", &profile->manifest_dir)
      || !toml_copy_string (root, "sched_script", &profile->sched_script)
      || !toml_copy_string (root, "assembler", &profile->assembler)
      || !toml_copy_string (root, "linker", &profile->linker))
    {
      profile_error (error_message, error_capacity,
                     "out of memory loading TOML profile");
      toml_free (root);
      cephyr_profile_destroy (profile);
      return 0;
    }
  if (!toml_copy_string_array (root, "include_paths", &profile->include_paths,
                               &profile->include_path_count)
      || !toml_copy_string_array (root, "defines", &profile->defines,
                                  &profile->define_count)
      || !toml_copy_string_array (root, "preprocessor_options",
                                  &profile->preprocessor_options,
                                  &profile->preprocessor_option_count)
      || !toml_copy_string_array (root, "preprocessor_args",
                                  &profile->preprocessor_args,
                                  &profile->preprocessor_arg_count)
      || !toml_copy_string_array (root, "assembler_options",
                                  &profile->assembler_options,
                                  &profile->assembler_option_count)
      || !toml_copy_string_array (root, "assembler_args",
                                  &profile->assembler_args,
                                  &profile->assembler_arg_count)
      || !toml_copy_string_array (root, "linker_options",
                                  &profile->linker_options,
                                  &profile->linker_option_count)
      || !toml_copy_string_array (root, "linker_args", &profile->linker_args,
                                  &profile->linker_arg_count)
      || !toml_copy_string_array (root, "library_paths",
                                  &profile->library_paths,
                                  &profile->library_path_count)
      || !toml_copy_string_array (root, "libraries", &profile->libraries,
                                  &profile->library_count)
      || !toml_copy_string_array (root, "rewrites", &profile->rewrites,
                                  &profile->rewrite_count))
    {
      profile_error (error_message, error_capacity,
                     "invalid string array in TOML profile");
      toml_free (root);
      cephyr_profile_destroy (profile);
      return 0;
    }
  {
    toml_datum_t value = toml_bool_in (root, "pic");
    if (value.ok)
      profile->pic = value.u.b;
    value = toml_bool_in (root, "pie");
    if (value.ok)
      profile->pie = value.u.b;
    value = toml_bool_in (root, "shared");
    if (value.ok)
      profile->shared = value.u.b;
  }
  array = toml_array_in (root, "kernels");
  for (int i = 0; array != NULL && i < toml_array_nelem (array); ++i)
    {
      toml_table_t *item = toml_table_at (array, i);
      toml_datum_t name
          = item ? toml_string_in (item, "name") : (toml_datum_t){ 0 };
      toml_datum_t capability
          = item ? toml_string_in (item, "capability") : (toml_datum_t){ 0 };
      toml_datum_t prefer
          = item ? toml_string_in (item, "prefer") : (toml_datum_t){ 0 };
      if (item == NULL
          || !profile_push_kernel (profile, name.ok ? name.u.s : NULL,
                                   capability.ok ? capability.u.s : NULL,
                                   prefer.ok ? prefer.u.s : NULL))
        {
          if (name.ok)
            free (name.u.s);
          if (capability.ok)
            free (capability.u.s);
          if (prefer.ok)
            free (prefer.u.s);
          profile_error (error_message, error_capacity,
                         "invalid kernels in TOML profile");
          toml_free (root);
          cephyr_profile_destroy (profile);
          return 0;
        }
      if (name.ok)
        free (name.u.s);
      if (capability.ok)
        free (capability.u.s);
      if (prefer.ok)
        free (prefer.u.s);
    }
  array = toml_array_in (root, "commands");
  for (int i = 0; array != NULL && i < toml_array_nelem (array); ++i)
    {
      toml_table_t *item = toml_table_at (array, i);
      toml_datum_t name
          = item ? toml_string_in (item, "name") : (toml_datum_t){ 0 };
      toml_datum_t command
          = item ? toml_string_in (item, "command") : (toml_datum_t){ 0 };
      if (item == NULL || !name.ok || !command.ok
          || !profile_push_command (profile, name.u.s, command.u.s))
        {
          if (name.ok)
            free (name.u.s);
          if (command.ok)
            free (command.u.s);
          profile_error (error_message, error_capacity,
                         "invalid commands in TOML profile");
          toml_free (root);
          cephyr_profile_destroy (profile);
          return 0;
        }
      free (name.u.s);
      free (command.u.s);
    }
  toml_free (root);
  if (profile->version != 1)
    {
      profile_error (error_message, error_capacity,
                     "unsupported profile version %d", profile->version);
      cephyr_profile_destroy (profile);
      return 0;
    }
  return 1;
}

static int
has_suffix (const char *path, const char *suffix)
{
  size_t n, m;
  if (path == NULL || suffix == NULL)
    return 0;
  n = strlen (path);
  m = strlen (suffix);
  return n >= m && strcmp (path + n - m, suffix) == 0;
}

int
cephyr_profile_load (const char *path, cephyr_profile *profile,
                     char *error_message, size_t error_capacity)
{
  if (error_message != NULL && error_capacity != 0)
    error_message[0] = '\0';
  if (path == NULL || profile == NULL)
    {
      profile_error (error_message, error_capacity, "invalid profile path");
      return 0;
    }
  if (has_suffix (path, ".yaml") || has_suffix (path, ".yml"))
    return profile_from_yaml (path, profile, error_message, error_capacity);
  if (has_suffix (path, ".toml"))
    return profile_from_toml (path, profile, error_message, error_capacity);
  profile_error (error_message, error_capacity,
                 "profile extension must be .yaml, .yml, or .toml");
  return 0;
}

static int
write_text_file (const char *path, const char *text, char *error_message,
                 size_t error_capacity)
{
  FILE *file;
  if (path == NULL || text == NULL)
    {
      profile_error (error_message, error_capacity, "invalid profile output");
      return 0;
    }
  file = fopen (path, "wx");
  if (file == NULL)
    {
      profile_error (error_message, error_capacity, "cannot create '%s': %s",
                     path, strerror (errno));
      return 0;
    }
  int write_rc = fputs (text, file);
  int close_rc = fclose (file);
  if (write_rc < 0 || close_rc != 0)
    {
      profile_error (error_message, error_capacity, "cannot write '%s'", path);
      return 0;
    }
  return 1;
}

int
cephyr_profile_init_file (const char *path, cephyr_profile_format format,
                          char *error_message, size_t error_capacity)
{
  static const char yaml[] = "# Cephyr profile v1.  Choose sched_script OR "
                             "explicit kernels/rewrites.\n"
                             "version: 1\n"
                             "name: my-cephyr-profile\n"
                             "target: x86_64-linux-gnu\n"
                             "opt_level: O0\n"
                             "preprocessor: ucpp\n"
                             "manifest_dir: manifests\n"
                             "preprocessor_options: []\n"
                             "preprocessor_args: []\n"
                             "assembler_options: []\n"
                             "assembler_args: []\n"
                             "linker_options: []\n"
                             "linker_args: []\n"
                             "library_paths: []\n"
                             "libraries: []\n"
                             "pic: false\n"
                             "pie: false\n"
                             "shared: false\n"
                             "# sched_script: compilers/cephyr/sched/O0.lua\n"
                             "# kernels:\n"
                             "#   - name: const-fold\n"
                             "#   - capability: opt.strength-reduction\n"
                             "#     prefer: strength-reduce\n"
                             "rewrites: []\n"
                             "include_paths: []\n"
                             "defines: []\n"
                             "commands: []\n";
  static const char toml[]
      = "# Cephyr profile v1.  Choose sched_script OR explicit "
        "kernels/rewrites.\n"
        "version = 1\n"
        "name = \"my-cephyr-profile\"\n"
        "target = \"x86_64-linux-gnu\"\n"
        "opt_level = \"O0\"\n"
        "preprocessor = \"ucpp\"\n"
        "manifest_dir = \"manifests\"\n"
        "preprocessor_options = []\n"
        "preprocessor_args = []\n"
        "assembler_options = []\n"
        "assembler_args = []\n"
        "linker_options = []\n"
        "linker_args = []\n"
        "library_paths = []\n"
        "libraries = []\n"
        "pic = false\n"
        "pie = false\n"
        "shared = false\n"
        "# sched_script = \"compilers/cephyr/sched/O0.lua\"\n"
        "rewrites = []\n"
        "include_paths = []\n"
        "defines = []\n"
        "# [[kernels]]\n"
        "# name = \"const-fold\"\n"
        "# [[commands]]\n"
        "# name = \"check\"\n"
        "# command = \"cephyr --help\"\n";
  return write_text_file (path, format == CEPHYR_PROFILE_TOML ? toml : yaml,
                          error_message, error_capacity);
}

char *
cephyr_profile_discover (const char *directory, char *error_message,
                         size_t error_capacity)
{
  const char *dir = directory ? directory : ".";
  const char *names[] = { "CEPHYR.yaml", "CEPHYR.toml" };
  char path[4096];
  FILE *file;
  for (size_t i = 0; i < sizeof (names) / sizeof (names[0]); ++i)
    {
      snprintf (path, sizeof (path), "%s/%s", dir, names[i]);
      file = fopen (path, "r");
      if (file != NULL)
        {
          fclose (file);
          return profile_strdup (path);
        }
    }
  profile_error (error_message, error_capacity,
                 "no CEPHYR.yaml or CEPHYR.toml found in '%s'", dir);
  return NULL;
}

const cephyr_profile_command *
cephyr_profile_find_command (const cephyr_profile *profile, const char *name)
{
  if (profile == NULL || name == NULL)
    return NULL;
  for (size_t i = 0; i < profile->command_count; ++i)
    if (strcmp (profile->commands[i].name, name) == 0)
      return &profile->commands[i];
  return NULL;
}
