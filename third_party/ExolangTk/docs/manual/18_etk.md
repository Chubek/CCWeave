# ExtensionTk: Dynamic Loading, Versioning, Registry, and Service Tables {#manual_18_etk}

Modules: [etk_platform.h](../include/ExtensionTk/etk_platform.h),
[etk_types.h](../include/ExtensionTk/etk_types.h),
[etk_dynload.h](../include/ExtensionTk/etk_dynload.h),
[etk_version.h](../include/ExtensionTk/etk_version.h),
[etk_registry.h](../include/ExtensionTk/etk_registry.h),
[etk_api.h](../include/ExtensionTk/etk_api.h) | Stability: varies

## Overview

ExtensionTk is the extension-management layer. It provides portable
shared-library loading with structured errors, semantic-version parsing and
constraint checking, a registry that activates extensions in dependency order,
and host-service tables that extensions consume through stable, versioned
capability handles.

## Platform Shims

`etk_platform.h` defines `ETK_DEF` and re-exports target detection macros as
`ETK_OS_*` and `ETK_ARCH_*` aliases.

## Common Types

`etk_types.h` defines:

~~~c
typedef enum etk_status {
    ETK_OK        = 0,
    ETK_EINVAL    = -1,
    ETK_ENOMEM    = -2,
    ETK_ENOTFOUND = -4,
    ETK_EFAIL     = -6
} etk_status;

typedef unsigned char etk_bool;
#define ETK_TRUE  1
#define ETK_FALSE 0
~~~

## Dynamic Loading

`etk_dynload.h` wraps `dlopen`/`dlsym`/`dlclose` (POSIX) and
`LoadLibraryEx`/`GetProcAddress`/`FreeLibrary` (Windows):

~~~c
typedef struct etk_lib_handle etk_lib_handle;
typedef void *etk_sym_handle;

etk_status etk_lib_open(const char *path, int flags, etk_lib_handle **out);
etk_status etk_lib_close(etk_lib_handle *lib);
etk_status etk_lib_sym(etk_lib_handle *lib, const char *name,
                       etk_sym_handle *out);
etk_status etk_lib_sym_optional(etk_lib_handle *lib, const char *name,
                                etk_sym_handle *out);
etk_bool etk_lib_path_search(const char *name, char *out, size_t cap);
~~~

Load-flag macros abstract platform differences:

~~~c
#define ETK_LIB_FLAG_NOW    0x01
#define ETK_LIB_FLAG_LAZY   0x02
#define ETK_LIB_FLAG_GLOBAL 0x04
#define ETK_LIB_FLAG_LOCAL  0x08
~~~

Feature macros indicate available backends:

~~~c
#define ETK_HAS_DLOPEN       /* defined when dlopen is available */
#define ETK_HAS_LOADLIBRARY  /* defined when LoadLibrary is available */
~~~

## Semantic Versioning

`etk_version.h` provides MAJOR.MINOR.PATCH parsing, comparison, and
constraint checking:

~~~c
typedef struct etk_version {
    unsigned major;
    unsigned minor;
    unsigned patch;
    const char *prerelease;
} etk_version;

etk_status etk_version_parse(const char *text, etk_version *out);
int etk_version_compare(const etk_version *a, const etk_version *b);
etk_bool etk_version_fmt(const etk_version *v, char *buf, size_t cap);
etk_bool etk_version_satisfies(const etk_version *v, const char *constraint);
~~~

`etk_version_satisfies()` supports constraints like `"^1.2.0"`,
`">=2.0.0 <3.0.0"`, and `"1.x"`.

## Extension Registry

`etk_registry.h` manages extension lifecycle:

~~~c
typedef etk_status (*etk_activate_fn)(void *ctx);
typedef void (*etk_deactivate_fn)(void *ctx);

typedef struct etk_extension {
    const char *id;
    etk_version version;
    const char **dependencies;
    size_t dep_count;
    etk_activate_fn activate;
    etk_deactivate_fn deactivate;
    void *ctx;
    etk_bool active;
} etk_extension;

typedef struct etk_registry {
    etk_extension entries[ETK_REGISTRY_MAX_EXTENSIONS];
    size_t count;
} etk_registry;

#define ETK_REGISTRY_MAX_EXTENSIONS 64

void etk_registry_init(etk_registry *r);
etk_status etk_registry_register(etk_registry *r, const etk_extension *e);
etk_status etk_registry_activate(etk_registry *r);
etk_extension *etk_registry_find(etk_registry *r, const char *id);
size_t etk_registry_count(const etk_registry *r);
void etk_registry_deactivate_all(etk_registry *r);
~~~

`etk_registry_activate()` resolves the dependency graph and activates
extensions in topological order. `etk_registry_deactivate_all()` tears them
down in reverse order.

## Host Service Tables

`etk_api.h` provides the mechanism for hosts to expose versioned services to
extensions:

~~~c
typedef struct etk_service {
    const char *name;
    etk_version version;
    void *fn;
} etk_service;

typedef struct etk_service_table {
    etk_service entries[64];
    size_t count;
} etk_service_table;

void etk_service_table_init(etk_service_table *t);
etk_status etk_service_register(etk_service_table *t, const char *name,
                                etk_version version, void *fn);
etk_status etk_service_lookup(const etk_service_table *t, const char *name,
                              const char *constraint, void **out);
etk_bool etk_api_probe(const etk_service_table *t, const char *name);
~~~

Services are looked up by (name, version-range) and returned as typed
function-pointer slots. The ABI surface stays stable while implementations
evolve.

## Full Example: Loading and Activating an Extension

~~~c
#define ETK_DYNLOAD_IMPLEMENTATION
#define ETK_VERSION_IMPLEMENTATION
#define ETK_REGISTRY_IMPLEMENTATION
#define ETK_API_IMPLEMENTATION
#include "ExtensionTk/etk_registry.h"
#include "ExtensionTk/etk_api.h"

etk_registry reg;
etk_registry_init(&reg);

etk_extension ext = {
    .id = "my_plugin",
    .version = {1, 0, 0, NULL},
    .dependencies = NULL,
    .dep_count = 0,
    .activate = my_activate,
    .deactivate = my_deactivate,
    .ctx = NULL,
    .active = ETK_FALSE
};
etk_registry_register(&reg, &ext);
etk_registry_activate(&reg);

etk_service_table svc;
etk_service_table_init(&svc);
etk_service_register(&svc, "allocator", (etk_version){1, 0, 0, NULL},
                     (void *)my_alloc_fn);

void *fn = NULL;
etk_service_lookup(&svc, "allocator", "^1.0.0", &fn);

etk_registry_deactivate_all(&reg);
~~~
