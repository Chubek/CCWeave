/* Cephyr stdlib wiring — stdlib-salvo manifests.
 *
 * A stdlib manifest (YAML, schema version 1) declares where a standard
 * library's headers, archives, and start files live. Cephyr consults
 * $CEPHYR_STDLIB_MANIFEST first; when unset it discovers the in-tree
 * Salvo libc manifest (stdlib-salvo/libc/Libc.yaml) relative to the
 * working directory, mirroring the Sched-script search convention.
 * Manifest-relative paths resolve against the manifest's directory, and
 * optional per-target sequences merge after the global lists when the
 * compile target matches exactly. */

#ifndef CEPHYR_STDLIB_H
#define CEPHYR_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CEPHYR_STDLIB_MANIFEST_ENV "CEPHYR_STDLIB_MANIFEST"
#define CEPHYR_STDLIB_DEFAULT_MANIFEST "stdlib-salvo/libc/Libc.yaml"
#define CEPHYR_STDLIB_DEFAULT_MANIFEST_UP "../stdlib-salvo/libc/Libc.yaml"

  typedef struct
  {
    int version;
    char *name;
    char *description;
    char *manifest_path; /* path the manifest was loaded from */

    /* Resolved paths (manifest-relative entries rebased to absolute or
     * working-directory-relative form). */
    char **include_dirs; /* appended after user -I paths */
    size_t include_dir_count;
    char **library_paths; /* linker -L search paths */
    size_t library_path_count;
    char **libraries; /* linker -l names, linked last */
    size_t library_count;
    char **start_files; /* objects linked before all inputs */
    size_t start_file_count;
  } cephyr_stdlib;

  void cephyr_stdlib_init (cephyr_stdlib *stdlib);
  void cephyr_stdlib_destroy (cephyr_stdlib *stdlib);

  /* Resolve which manifest to use. Returns a heap-allocated path, or NULL
   * when no manifest applies. *is_explicit is set to 1 when the path came
   * from $CEPHYR_STDLIB_MANIFEST (a load failure is then a hard error),
   * and to 0 for the in-tree default (advisory; absence is tolerated). */
  char *cephyr_stdlib_discover_manifest (int *is_explicit);

  /* Load a manifest and merge the sequences for target_triple (exact
   * match; NULL selects the global lists only). Returns 1 on success. */
  int cephyr_stdlib_load (const char *manifest_path, const char *target_triple,
                          cephyr_stdlib *stdlib, char *error_message,
                          size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_STDLIB_H */
