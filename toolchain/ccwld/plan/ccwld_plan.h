/* §2: The link-plan IR — sealed, serializable, canonical.
 *
 * Both frontends (ld-script via mpc, Lua via lccwld) lower to this
 * single immutable IR. Once sealed, the plan is read-only; post-seal
 * mutation only through phase hooks within their mutability scope. */
#ifndef CCWLD_PLAN_H
#define CCWLD_PLAN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ccwld_expr;

/* --- output descriptor (§2.1) --- */
typedef struct {
    char *kind;      /* "exe" | "dso" | "reloc" | "pie" */
    char *format;    /* "elf" | "pe" | "macho"           */
    char *entry;     /* entry symbol name                */
    char *soname;    /* DSO soname (ELF only)            */
    char *osabi;     /* format-specific OS/ABI           */
} ccwld_output;

/* --- memory region (§2.1) --- */
typedef struct {
    char    *name;         /* region name                     */
    char    *attrs;        /* "rx", "rwx", "rw", etc.         */
    uint64_t origin;       /* start address                   */
    uint64_t length;       /* region size                     */
} ccwld_mem;

/* --- output section (§2.1) --- */
typedef struct {
    char    *name;         /* output section name             */
    char    *region;       /* memory region name              */
    char    *at_region;    /* LMA region (or NULL)            */
    char    *selector;     /* input section selector (match)  */
    char    *keep;         /* KEEP selector (or NULL)         */
    uint64_t align;        /* alignment requirement           */
    int      load;         /* loadable flag                   */
    struct ccwld_expr *fill;    /* fill expression (or NULL)  */
    /* --- layout output (set during layout phase) --- */
    uint64_t vma;          /* virtual address after layout    */
    uint64_t lma;          /* load address after layout       */
    uint64_t size;         /* size after layout               */
} ccwld_sec;

/* --- symbol assignment (§2.1) --- */
typedef struct {
    char    *name;                /* symbol name                    */
    struct ccwld_expr *expr;     /* value expression                */
    int      provide;            /* PROVIDE semantic                */
    int      hidden;             /* hidden visibility               */
    char    *visibility;         /* "default"|"hidden"|"protected"|"internal" */
    char    *binding;            /* "global"|"local"|"weak"         */
    /* --- resolved value (set during layout) --- */
    uint64_t resolved_value;
    int      resolved;
} ccwld_sym;

/* --- program header / segment (§2.1) --- */
typedef struct {
    char    *name;         /* segment name (for segment_start) */
    char    *type;         /* "LOAD"|"DYNAMIC"|"NOTE"|etc.     */
    uint64_t vaddr;        /* virtual address                  */
    uint64_t paddr;        /* physical address                 */
    uint64_t filesz;       /* file size                        */
    uint64_t memsz;        /* memory size                      */
    uint32_t flags;        /* PF_R|PF_W|PF_X                   */
    uint64_t align;        /* alignment                        */
} ccwld_phdr;

/* --- input file (§2.1) --- */
typedef struct {
    char *path;         /* file path                            */
    int   as_needed;    /* --as-needed flag                     */
    int   startup;      /* forced-first in output order         */
    int   is_group;     /* archive group with repeated scan     */
} ccwld_input;

/* --- version node (§2.1) --- */
typedef struct {
    char *symbol;        /* symbol name                */
    char *version;       /* version string              */
    int   is_default;    /* default version flag        */
} ccwld_ver;

/* --- LTO configuration (§4) --- */
typedef struct {
    char    *pipeline;    /* LTO pipeline name              */
    unsigned jobs;        /* parallel jobs (1 for repro)    */
    char    *cache_dir;   /* LTO cache directory            */
    int      enabled;     /* whether LTO is active          */
} ccwld_lto_cfg;

/* --- plugin registration (§5) --- */
typedef struct {
    char   *path;         /* plugin shared-object path      */
    char   *name;         /* plugin name                    */
    char   *options;      /* JSON options string            */
    int     loaded;       /* loaded flag (internal)         */
} ccwld_plugin;

/* --- phase hook (§3) --- */
typedef enum {
    CCWLD_PHASE_LOAD     = 0,
    CCWLD_PHASE_RESOLVE  = 1,
    CCWLD_PHASE_LTO      = 2,
    CCWLD_PHASE_GC       = 3,
    CCWLD_PHASE_LAYOUT   = 4,
    CCWLD_PHASE_RELOCATE = 5,
    CCWLD_PHASE_EMIT     = 6,
} ccwld_phase_id;

struct ccwld_link; /* forward */

typedef int (*ccwld_hook_fn)(ccwld_phase_id phase,
                              struct ccwld_link *link,
                              void *user);

typedef struct {
    ccwld_phase_id phase;
    ccwld_hook_fn  fn;
    void          *user;
} ccwld_hook;

/* --- the plan IR (opaque; only the fields the expr engine needs are here) --- */
typedef struct ccwld_plan {
    /* --- declarative plan --- */
    char        *target;       /* target triple                   */
    ccwld_output output;       /* output descriptor               */
    bool         sealed;       /* post-seal immutability          */

    /* --- inputs --- */
    ccwld_input *inputs;
    size_t       ninputs;
    size_t       cinputs;

    /* --- search paths --- */
    char **paths;
    size_t npaths;
    size_t cpaths;

    /* --- memory regions --- */
    ccwld_mem *mems;
    size_t     nmems;
    size_t     cmems;

    /* --- output sections --- */
    ccwld_sec *secs;
    size_t     nsecs;
    size_t     csecs;

    /* --- symbols --- */
    ccwld_sym *syms;
    size_t     nsyms;
    size_t     csyms;

    /* --- program headers --- */
    ccwld_phdr *phdrs;
    size_t      nphdrs;
    size_t      cphdrs;

    /* --- version nodes --- */
    ccwld_ver *vers;
    size_t     nvers;
    size_t     cvers;

    /* --- LTO / plugins / hooks --- */
    ccwld_lto_cfg  lto;
    ccwld_plugin  *plugins;
    size_t         nplugins;
    size_t         cplugins;
    ccwld_hook    *hooks;
    size_t         nhooks;
    size_t         chooks;

    /* --- serialized canonical form --- */
    char   *serialized;
    size_t  serialized_len;

    /* --- gensym counter --- */
    unsigned gensym;

    /* --- reproducibility flag --- */
    bool reproducible;

    /* --- internal: manifest hash for cache --- */
    char plan_hash[65];
} ccwld_plan;

/* --- link handle (passed to phase hooks) --- */
typedef struct ccwld_link {
    ccwld_plan     *plan;
    ccwld_phase_id  phase;
    /* --- introspection --- */
    void           *phase_state;  /* phase-specific opaque state */
} ccwld_link;

/* --- error --- */
typedef struct {
    int  code;
    char message[512];
} ccwld_error;

/* --- lifecycle --- */
ccwld_plan *ccwld_plan_new(const char *target);
void        ccwld_plan_free(ccwld_plan *p);

/* --- plan builders (only valid before seal) --- */
int ccwld_plan_output(ccwld_plan *p, const ccwld_output *o, ccwld_error *e);
int ccwld_plan_input(ccwld_plan *p, const char *path, int as_needed,
                     int startup, ccwld_error *e);
int ccwld_plan_group(ccwld_plan *p, const char **paths, size_t n,
                     ccwld_error *e);
int ccwld_plan_search_path(ccwld_plan *p, const char *path, ccwld_error *e);
int ccwld_plan_memory(ccwld_plan *p, const char *name, const char *attrs,
                      uint64_t origin, uint64_t length, ccwld_error *e);
int ccwld_plan_section(ccwld_plan *p, const char *name, const char *region,
                       uint64_t align, const char *selector,
                       const char *at_region, ccwld_error *e);
int ccwld_plan_section_full(ccwld_plan *p, const char *name,
                            const char *region, uint64_t align,
                            const char *selector, const char *keep,
                            const char *at_region, struct ccwld_expr *fill,
                            ccwld_error *e);
int ccwld_plan_symbol(ccwld_plan *p, const char *name,
                      struct ccwld_expr *expr, int provide,
                      int hidden, ccwld_error *e);
int ccwld_plan_symbol_full(ccwld_plan *p, const char *name,
                           struct ccwld_expr *expr, int provide,
                           const char *visibility, const char *binding,
                           ccwld_error *e);
int ccwld_plan_phdr(ccwld_plan *p, const char *name, const char *type,
                    uint32_t flags, uint64_t align, ccwld_error *e);
int ccwld_plan_version(ccwld_plan *p, const char *symbol, const char *version,
                       int is_default, ccwld_error *e);
int ccwld_plan_lto(ccwld_plan *p, const char *pipeline, unsigned jobs,
                   const char *cache_dir, ccwld_error *e);
int ccwld_plan_plugin(ccwld_plan *p, const char *path, const char *options,
                      ccwld_error *e);
int ccwld_plan_hook(ccwld_plan *p, ccwld_phase_id phase, ccwld_hook_fn fn,
                    void *user, ccwld_error *e);

/* --- seal / serialize / hash --- */
int  ccwld_plan_seal(ccwld_plan *p, ccwld_error *e);
int  ccwld_plan_serialize(const ccwld_plan *p, char **out, size_t *len,
                          ccwld_error *e);
int  ccwld_plan_hash(const ccwld_plan *p, char out[65]);

/* --- link execution --- */
int  ccwld_link_run(ccwld_plan *p, const char *output_path, ccwld_error *e);

/* --- expression constructors (thin wrappers around expr/ engine) --- */
struct ccwld_expr *ccwld_expr_int(uint64_t value);
struct ccwld_expr *ccwld_expr_symbol(const char *name);
struct ccwld_expr *ccwld_expr_dot(void);
struct ccwld_expr *ccwld_expr_binary(char op, struct ccwld_expr *a,
                                     struct ccwld_expr *b);
struct ccwld_expr *ccwld_expr_unary(char op, struct ccwld_expr *a);
struct ccwld_expr *ccwld_expr_align_expr(struct ccwld_expr *a, uint64_t boundary);
void ccwld_expr_free(struct ccwld_expr *e);
int  ccwld_expr_eval(const struct ccwld_expr *e, const ccwld_plan *p,
                     uint64_t dot, uint64_t *out, ccwld_error *e_);

/* --- frontend entry points --- */
int  ccwld_run_lua(const char *script, const char *target,
                   ccwld_plan **out, ccwld_error *e);
int  ccwld_run_ldscript(const char *script, const char *target,
                        ccwld_plan **out, ccwld_error *e);
int  ccwld_run_script(const char *script, const char *target,
                      const char *output_path, ccwld_error *e);

void ccwld_free(void *p);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_PLAN_H */
