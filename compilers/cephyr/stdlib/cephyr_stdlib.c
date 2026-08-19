/* Cephyr stdlib wiring — stdlib-salvo manifest loader. See the header
 * for the discovery contract. Parsing reuses the vendored libcyaml,
 * following the cephyr_profile schema conventions. */

#define _POSIX_C_SOURCE 200809L

#include "cephyr_stdlib.h"

#include <cyaml/cyaml.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kstring.h"

/* ---------- YAML raw model ---------- */

typedef struct {
    const char  *triple;
    const char **include_dirs;
    uint64_t     include_dirs_count;
    const char **library_paths;
    uint64_t     library_paths_count;
    const char **libraries;
    uint64_t     libraries_count;
    const char **start_files;
    uint64_t     start_files_count;
} yaml_stdlib_target;

typedef struct {
    int          version;
    const char  *name;
    const char  *description;
    const char **include_dirs;
    uint64_t     include_dirs_count;
    const char **library_paths;
    uint64_t     library_paths_count;
    const char **libraries;
    uint64_t     libraries_count;
    const char **start_files;
    uint64_t     start_files_count;
    yaml_stdlib_target *targets;
    uint64_t     targets_count;
} yaml_stdlib;

static const cyaml_schema_value_t yaml_string_schema = {
    CYAML_VALUE_STRING(CYAML_FLAG_POINTER, char, 0, CYAML_UNLIMITED),
};

static const cyaml_schema_field_t yaml_stdlib_target_fields[] = {
    CYAML_FIELD_STRING_PTR("triple", CYAML_FLAG_DEFAULT,
                           yaml_stdlib_target, triple, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("include_dirs",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib_target, include_dirs,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("library_paths",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib_target, library_paths,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("libraries",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib_target, libraries,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("start_files",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib_target, start_files,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_END
};

static const cyaml_schema_value_t yaml_stdlib_target_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT, yaml_stdlib_target,
                        yaml_stdlib_target_fields),
};

static const cyaml_schema_field_t yaml_stdlib_fields[] = {
    CYAML_FIELD_INT("version", CYAML_FLAG_OPTIONAL, yaml_stdlib, version),
    CYAML_FIELD_STRING_PTR("name", CYAML_FLAG_OPTIONAL,
                           yaml_stdlib, name, 0, CYAML_UNLIMITED),
    CYAML_FIELD_STRING_PTR("description", CYAML_FLAG_OPTIONAL,
                           yaml_stdlib, description, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("include_dirs",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib, include_dirs,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("library_paths",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib, library_paths,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("libraries",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib, libraries,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("start_files",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib, start_files,
                         &yaml_string_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE("targets",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         yaml_stdlib, targets,
                         &yaml_stdlib_target_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_END
};

static const cyaml_schema_value_t yaml_stdlib_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_POINTER, yaml_stdlib,
                        yaml_stdlib_fields),
};

static const cyaml_config_t yaml_config = {
    .log_fn = cyaml_log,
    .mem_fn = cyaml_mem,
    .log_level = CYAML_LOG_ERROR,
    .flags = CYAML_CFG_IGNORE_UNKNOWN_KEYS
};

/* ---------- small helpers ---------- */

static void stdlib_error(char *out, size_t capacity, const char *fmt, ...)
{
    va_list ap;
    if (out == NULL || capacity == 0) return;
    va_start(ap, fmt);
    vsnprintf(out, capacity, fmt, ap);
    va_end(ap);
}

static char *stdlib_strdup(const char *value)
{
    kstring_t copy = { 0, 0, NULL };
    if (value == NULL || kputs(value, &copy) == EOF) return NULL;
    return ks_release(&copy);
}

static void stdlib_free_list(char ***items, size_t *count)
{
    if (items == NULL || count == NULL) return;
    for (size_t i = 0; i < *count; ++i)
        free((*items)[i]);
    free(*items);
    *items = NULL;
    *count = 0;
}

static int stdlib_push_owned(char ***items, size_t *count, char *owned)
{
    char **grown;
    if (owned == NULL) return 0;
    grown = (char **)realloc(*items, (*count + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(owned);
        return 0;
    }
    grown[*count] = owned;
    *items = grown;
    ++*count;
    return 1;
}

/* Manifest-relative entries rebase against the manifest's directory;
 * absolute entries pass through unchanged. */
static char *stdlib_resolve_entry(const char *manifest_dir, const char *entry)
{
    kstring_t joined = { 0, 0, NULL };
    if (entry == NULL) return NULL;
    if (entry[0] == '/') return stdlib_strdup(entry);
    if (ksprintf(&joined, "%s/%s", manifest_dir, entry) < 0)
        return NULL;
    return ks_release(&joined);
}

static int stdlib_push_entry(const char *manifest_dir,
                             char ***items, size_t *count,
                             const char *entry)
{
    return stdlib_push_owned(items, count,
                             stdlib_resolve_entry(manifest_dir, entry));
}

/* Verbatim copy for entries that are names, not paths (libraries). */
static int stdlib_push_verbatim(char ***items, size_t *count,
                                const char *entry)
{
    return stdlib_push_owned(items, count, stdlib_strdup(entry));
}

static char *stdlib_manifest_dirname(const char *path)
{
    char *copy = stdlib_strdup(path);
    char *slash;
    if (copy == NULL) return NULL;
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        free(copy);
        return stdlib_strdup(".");
    }
    if (slash == copy)
        copy[1] = '\0';
    else
        *slash = '\0';
    return copy;
}

/* ---------- public API ---------- */

void cephyr_stdlib_init(cephyr_stdlib *stdlib)
{
    if (stdlib == NULL) return;
    memset(stdlib, 0, sizeof(*stdlib));
    stdlib->version = 1;
}

void cephyr_stdlib_destroy(cephyr_stdlib *stdlib)
{
    if (stdlib == NULL) return;
    free(stdlib->name);
    free(stdlib->description);
    free(stdlib->manifest_path);
    stdlib_free_list(&stdlib->include_dirs, &stdlib->include_dir_count);
    stdlib_free_list(&stdlib->library_paths, &stdlib->library_path_count);
    stdlib_free_list(&stdlib->libraries, &stdlib->library_count);
    stdlib_free_list(&stdlib->start_files, &stdlib->start_file_count);
    memset(stdlib, 0, sizeof(*stdlib));
}

char *cephyr_stdlib_discover_manifest(int *is_explicit)
{
    const char *environment = getenv(CEPHYR_STDLIB_MANIFEST_ENV);

    if (is_explicit != NULL) *is_explicit = 0;
    if (environment != NULL && *environment != '\0') {
        if (is_explicit != NULL) *is_explicit = 1;
        return stdlib_strdup(environment);
    }
    if (access(CEPHYR_STDLIB_DEFAULT_MANIFEST, R_OK) == 0)
        return stdlib_strdup(CEPHYR_STDLIB_DEFAULT_MANIFEST);
    if (access(CEPHYR_STDLIB_DEFAULT_MANIFEST_UP, R_OK) == 0)
        return stdlib_strdup(CEPHYR_STDLIB_DEFAULT_MANIFEST_UP);
    return NULL;
}

/* Merge one sequence group (global or matched target) into the public
 * structure. Path-kind sequences resolve; libraries stay verbatim. */
static int stdlib_merge_group(cephyr_stdlib *stdlib,
                              const char *manifest_dir,
                              const char **include_dirs,
                              uint64_t include_dir_count,
                              const char **library_paths,
                              uint64_t library_path_count,
                              const char **libraries,
                              uint64_t library_count,
                              const char **start_files,
                              uint64_t start_file_count)
{
    for (uint64_t i = 0; i < include_dir_count; ++i)
        if (!stdlib_push_entry(manifest_dir, &stdlib->include_dirs,
                               &stdlib->include_dir_count,
                               include_dirs[i]))
            return 0;
    for (uint64_t i = 0; i < library_path_count; ++i)
        if (!stdlib_push_entry(manifest_dir, &stdlib->library_paths,
                               &stdlib->library_path_count,
                               library_paths[i]))
            return 0;
    for (uint64_t i = 0; i < library_count; ++i)
        if (!stdlib_push_verbatim(&stdlib->libraries,
                                  &stdlib->library_count, libraries[i]))
            return 0;
    for (uint64_t i = 0; i < start_file_count; ++i)
        if (!stdlib_push_entry(manifest_dir, &stdlib->start_files,
                               &stdlib->start_file_count, start_files[i]))
            return 0;
    return 1;
}

int cephyr_stdlib_load(const char *manifest_path, const char *target_triple,
                       cephyr_stdlib *stdlib,
                       char *error_message, size_t error_capacity)
{
    yaml_stdlib *raw = NULL;
    cyaml_err_t rc;
    char *manifest_dir = NULL;
    int ok = 0;

    if (manifest_path == NULL || stdlib == NULL) {
        stdlib_error(error_message, error_capacity,
                     "invalid stdlib manifest arguments");
        return 0;
    }
    cephyr_stdlib_init(stdlib);

    rc = cyaml_load_file(manifest_path, &yaml_config, &yaml_stdlib_schema,
                         (cyaml_data_t **)&raw, NULL);
    if (rc != CYAML_OK) {
        stdlib_error(error_message, error_capacity,
                     "stdlib manifest load failed: %s", cyaml_strerror(rc));
        return 0;
    }
    if (raw == NULL) {
        stdlib_error(error_message, error_capacity,
                     "stdlib manifest is empty");
        return 0;
    }
    if (raw->version != 0 && raw->version != 1) {
        stdlib_error(error_message, error_capacity,
                     "unsupported stdlib manifest version %d", raw->version);
        goto out;
    }

    manifest_dir = stdlib_manifest_dirname(manifest_path);
    if (manifest_dir == NULL) goto oom;

    stdlib->version = raw->version != 0 ? raw->version : 1;
    if (raw->name != NULL) {
        stdlib->name = stdlib_strdup(raw->name);
        if (stdlib->name == NULL) goto oom;
    }
    if (raw->description != NULL) {
        stdlib->description = stdlib_strdup(raw->description);
        if (stdlib->description == NULL) goto oom;
    }
    stdlib->manifest_path = stdlib_strdup(manifest_path);
    if (stdlib->manifest_path == NULL) goto oom;

    if (!stdlib_merge_group(stdlib, manifest_dir,
                            raw->include_dirs, raw->include_dirs_count,
                            raw->library_paths, raw->library_paths_count,
                            raw->libraries, raw->libraries_count,
                            raw->start_files, raw->start_files_count))
        goto oom;

    /* Per-target sequences merge after the global lists on an exact
     * triple match; an entry may legitimately carry only a triple. */
    if (target_triple != NULL) {
        for (uint64_t i = 0; i < raw->targets_count; ++i) {
            const yaml_stdlib_target *target = &raw->targets[i];
            if (target->triple == NULL ||
                strcmp(target->triple, target_triple) != 0)
                continue;
            if (!stdlib_merge_group(stdlib, manifest_dir,
                                    target->include_dirs,
                                    target->include_dirs_count,
                                    target->library_paths,
                                    target->library_paths_count,
                                    target->libraries,
                                    target->libraries_count,
                                    target->start_files,
                                    target->start_files_count))
                goto oom;
        }
    }

    ok = 1;
    goto out;

oom:
    stdlib_error(error_message, error_capacity,
                 "out of memory loading stdlib manifest");
    cephyr_stdlib_destroy(stdlib);
out:
    free(manifest_dir);
    cyaml_free(&yaml_config, &yaml_stdlib_schema, raw, 0);
    return ok;
}
