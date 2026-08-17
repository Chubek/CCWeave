/* Cephyr user profile support.
 *
 * Profiles are deliberately configuration-only.  They select an already
 * sealed Sched script or construct a plan from manifest-backed kernel and
 * Stdrewrite references; they never provide an IR mutation escape hatch.
 */
#ifndef CEPHYR_PROFILE_H
#define CEPHYR_PROFILE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CEPHYR_PROFILE_YAML = 0,
    CEPHYR_PROFILE_TOML = 1
} cephyr_profile_format;

typedef struct {
    char *name;
    char *capability;
    char *prefer;
} cephyr_profile_kernel;

typedef struct {
    char *name;
    char *command;
} cephyr_profile_command;

typedef struct {
    int version;
    char *name;
    char *target_triple;
    char *opt_level;
    char *preprocessor;
    char *manifest_dir;
    char *sched_script;

    char **include_paths;
    size_t include_path_count;
    char **defines;
    size_t define_count;
    char **preprocessor_options;
    size_t preprocessor_option_count;
    char **preprocessor_args;
    size_t preprocessor_arg_count;
    char **assembler_options;
    size_t assembler_option_count;
    char **assembler_args;
    size_t assembler_arg_count;
    char **linker_options;
    size_t linker_option_count;
    char **linker_args;
    size_t linker_arg_count;
    char **library_paths;
    size_t library_path_count;
    char **libraries;
    size_t library_count;
    bool pic;
    bool pie;
    bool shared;
    char *assembler;
    char *linker;

    cephyr_profile_kernel *kernels;
    size_t kernel_count;
    char **rewrites;
    size_t rewrite_count;

    cephyr_profile_command *commands;
    size_t command_count;
} cephyr_profile;

void cephyr_profile_init(cephyr_profile *profile);
void cephyr_profile_destroy(cephyr_profile *profile);

/* Loads and validates a profile.  Returns 1 on success, 0 on failure. */
int cephyr_profile_load(const char *path, cephyr_profile *profile,
                        char *error_message, size_t error_capacity);

/* Writes a non-destructive boilerplate profile.  Returns 1 on success. */
int cephyr_profile_init_file(const char *path, cephyr_profile_format format,
                             char *error_message, size_t error_capacity);

/* Finds CEPHYR.yaml first, then CEPHYR.toml, in directory. */
char *cephyr_profile_discover(const char *directory,
                              char *error_message, size_t error_capacity);

const cephyr_profile_command *cephyr_profile_find_command(
    const cephyr_profile *profile, const char *name);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_PROFILE_H */
