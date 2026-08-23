/* §2.3: deferred expression AST — shared between both frontends.
 * Expression-valued fields hold these AST nodes, never pre-computed integers.
 * Evaluation happens during layout in plan order against a live location
 * counter.
 *
 * Also defines the plan IR struct types (§2.1) that the expression
 * evaluator needs to dereference (mems, secs, syms, phdrs, plan). */
#ifndef CCWLD_EXPR_H
#define CCWLD_EXPR_H

#include "ccwld-plugin.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct ccwld_plan;
  struct ccwld_link;

  /* --- error --- */
  typedef struct
  {
    int code;
    char message[512];
  } ccwld_error;

  /* Exit-code mapping (CCWld §9), consumed by both frontends. */
  enum
  {
    CCWLD_EXIT_OK = 0,         /* link succeeded                       */
    CCWLD_EXIT_LINK = 1,       /* unresolved/unplaced/relocation       */
    CCWLD_EXIT_USAGE = 2,      /* usage / configuration error          */
    CCWLD_EXIT_ABI = 3,        /* plugin / LTO ABI error               */
    CCWLD_EXIT_INTERNAL = 4    /* internal error                       */
  };

  /* --- output descriptor (§2.1) --- */
  typedef struct
  {
    char *kind;   /* "exe" | "dso" | "reloc" | "pie" */
    char *format; /* "elf" | "pe" | "macho" | "wasm" */
    char *entry;  /* entry symbol name                */
    char *soname; /* DSO soname (ELF only)            */
    char *osabi;  /* format-specific OS/ABI           */
  } ccwld_output;

  /* --- memory region (§2.1) --- */
  typedef struct
  {
    char *name;      /* region name                     */
    char *attrs;     /* "rx", "rwx", "rw", etc.         */
    uint64_t origin; /* start address                   */
    uint64_t length; /* region size                     */
  } ccwld_mem;

  /* --- input-section selector (§2.1, lccwld §4.4) ---
   * Ordered: `keep` selectors mark their matches as GC roots. */
  typedef struct
  {
    char *file_glob;  /* glob over the input file path ("*") */
    char **globs;     /* globs over input section names      */
    size_t nglobs;
    int keep;         /* KEEP semantics (GC root)            */
  } ccwld_sel;

  /* --- output section (§2.1) --- */
  typedef struct
  {
    char *name;              /* output section name             */
    char *region;            /* memory region name              */
    char *at_region;         /* LMA region (or NULL)            */
    char *phdr;              /* bound phdr/segment name         */
    ccwld_sel *sels;         /* ordered input selectors         */
    size_t nsels;
    uint64_t align;          /* alignment requirement           */
    uint64_t subalign;       /* input member alignment (0 = per-member) */
    struct ccwld_expr *vma_expr; /* explicit start-address expression */
    struct ccwld_expr *at_expr;  /* AT(load-address) expression (§2.3) */
    int load;                /* loadable flag (NOLOAD clears)   */
    struct ccwld_expr *fill; /* fill expression (or NULL)    */
    /* --- layout output (set during layout phase) --- */
    uint64_t vma;  /* virtual address after layout    */
    uint64_t lma;  /* load address after layout       */
    uint64_t size; /* size after layout               */
  } ccwld_sec;

  /* --- symbol assignment (§2.1) --- */
  typedef struct
  {
    char *name;              /* symbol name                    */
    struct ccwld_expr *expr; /* value expression               */
    int provide;             /* PROVIDE semantic               */
    int hidden;              /* hidden visibility              */
    char *visibility;        /* "default"|"hidden"|"protected"|"internal" */
    char *binding;           /* "global"|"local"|"weak"         */
    int sec_idx;             /* section context (-1 = top level) */
    unsigned seq;            /* statement order within SECTIONS */
    char *site;              /* provenance: definition site     */
    /* --- resolved value (set during layout) --- */
    uint64_t resolved_value;
    int resolved;
  } ccwld_sym;

  /* --- location-counter assignment (`. = expr`) --- */
  typedef struct
  {
    struct ccwld_expr *expr;
    int sec_idx; /* section context (-1 = top level) */
    unsigned seq;
    char *site;
  } ccwld_dotstep;

  /* --- visibility/binding override (lccwld §4.7) --- */
  typedef struct
  {
    char *name;       /* target symbol name                    */
    char *visibility; /* or NULL                               */
    char *binding;    /* or NULL                               */
    char *alias;      /* alias target (new = old) or NULL      */
  } ccwld_attr;

  /* --- program header / segment (§2.1) --- */
  typedef struct
  {
    char *name;      /* segment name (for segment_start) */
    char *type;      /* "LOAD"|"DYNAMIC"|"NOTE"|etc.     */
    uint64_t vaddr;  /* virtual address                  */
    uint64_t paddr;  /* physical address                 */
    uint64_t filesz; /* file size                        */
    uint64_t memsz;  /* memory size                      */
    uint32_t flags;  /* PF_R|PF_W|PF_X                   */
    uint64_t align;  /* alignment                        */
  } ccwld_phdr;

  /* --- input file (§2.1) --- */
  typedef struct
  {
    char *path;    /* file path                            */
    int as_needed; /* --as-needed flag                     */
    int startup;   /* forced-first in output order         */
    int is_group;  /* archive group with repeated scan     */
    int group_start; /* first member of a group            */
  } ccwld_input;

  /* --- version node (§2.1) --- */
  typedef struct
  {
    char *symbol;   /* symbol name                */
    char *version;  /* version string              */
    int is_default; /* default version flag        */
  } ccwld_ver;

  /* --- LTO configuration (§4) --- */
  typedef struct
  {
    char *pipeline;  /* LTO pipeline name              */
    unsigned jobs;   /* parallel jobs (1 for repro)    */
    char *cache_dir; /* LTO cache directory            */
    int enabled;     /* whether LTO is active          */
  } ccwld_lto_cfg;

  /* --- plugin registration (§5) --- */
  typedef struct
  {
    char *path;    /* plugin shared-object path      */
    char *name;    /* plugin name                    */
    char *options; /* JSON options string            */
    int loaded;    /* loaded flag (internal)         */
  } ccwld_plugin;

  /* --- phase hook (§3) --- */
  /* ccwld_phase is defined in ccwld-plugin.h (ABI header) */
  typedef int (*ccwld_hook_fn) (ccwld_phase phase, struct ccwld_link *link,
                                void *user);

  typedef struct
  {
    ccwld_phase phase;
    ccwld_hook_fn fn;
    void *user;
    char *site;   /* provenance of the registration  */
    int is_lua;   /* registered from the Lua frontend */
  } ccwld_hook;

  /* --- link handle (passed to phase hooks and plugins) --- */
  typedef struct ccwld_link
  {
    struct ccwld_plan *plan;
    ccwld_phase phase;
    /* --- introspection --- */
    void *phase_state; /* phase-specific opaque state */
  } ccwld_link;

  /* Optional resolver for input-object symbols, installed by the phase
   * pipeline before layout so deferred expressions can reference
   * symbols defined by inputs (linker's S).  Returns 1 and sets *value
   * when the symbol has a known value. */
  typedef int (*ccwld_sym_resolver) (const struct ccwld_plan *p,
                                     const char *name, uint64_t *value);

  /* --- pipeline options (CLI/driver level, not script-level) --- */
  typedef struct
  {
    int gc_sections;        /* --gc-sections                     */
    int as_needed_default;  /* --as-needed applied to all inputs */
    int print_plan;         /* --print-plan                      */
    int no_cache;           /* --no-cache                        */
    int reproducible;       /* --no-reproducible clears          */
    int unsafe_lua;         /* --unsafe-lua: marks note false    */
    char *cache_dir;        /* --cache-dir (NULL disables)       */
    char *out_name;         /* OUTPUT() name from ld-script      */
  } ccwld_plan_opts;

  /* --- the plan IR (§2.1) --- */
  typedef struct ccwld_plan
  {
    /* --- declarative plan --- */
    char *target;        /* target triple                    */
    ccwld_output output; /* output declaration               */
    bool sealed;         /* post-seal mutation only via hooks */

    /* --- inputs --- */
    ccwld_input *inputs;
    size_t ninputs;
    size_t cinputs;

    /* --- search paths --- */
    char **paths;
    size_t npaths;
    size_t cpaths;

    /* --- memory regions --- */
    ccwld_mem *mems;
    size_t nmems;
    size_t cmems;

    /* --- output sections --- */
    ccwld_sec *secs;
    size_t nsecs;
    size_t csecs;

    /* --- symbols / dot assignments / attr overrides --- */
    ccwld_sym *syms;
    size_t nsyms;
    size_t csyms;
    ccwld_dotstep *dotsteps;
    size_t ndotsteps;
    size_t cdotsteps;
    unsigned stmt_seq; /* statement order counter            */
    ccwld_attr *attrs;
    size_t nattrs;
    size_t cattrs;

    /* --- program headers --- */
    ccwld_phdr *phdrs;
    size_t nphdrs;
    size_t cphdrs;

    /* --- version nodes --- */
    ccwld_ver *vers;
    size_t nvers;
    size_t cvers;

    /* --- -D / --defsym environment (lccwld §3) --- */
    char **env_keys;
    char **env_vals;
    size_t nenv;
    size_t cenv;

    /* --- LTO / plugins / hooks --- */
    ccwld_lto_cfg lto;
    ccwld_plugin *plugins;
    size_t nplugins;
    size_t cplugins;
    ccwld_hook *hooks;
    size_t nhooks;
    size_t chooks;

    /* --- pipeline options --- */
    ccwld_plan_opts options;

    /* --- runtime (not serialized, not parity-relevant) --- */
    ccwld_sym_resolver resolve_sym; /* installed by pipeline     */
    void *state;                    /* opaque ccwld_state        */
    char *frontend;                 /* "ldscript"|"lua"|"api"    */
    unsigned gensym;                /* counter regime (lccwld §6)*/
    /* lccwld runtime (Lua state + hook refs), closed with the plan */
    void *frontend_ctx;
    void (*frontend_ctx_free) (void *);

    /* --- serialized canonical form --- */
    char *serialized;
    size_t serialized_len;

    /* --- reproducibility flag --- */
    bool reproducible;

    /* --- internal: manifest hash for cache --- */
    char plan_hash[65];
  } ccwld_plan;

  /* Expression node kinds (§2.3). */
  typedef enum
  {
    CCWLD_EXPR_INT = 1,             /* integer literal               */
    CCWLD_EXPR_SYMBOL = 2,          /* symbol reference              */
    CCWLD_EXPR_DOT = 3,             /* current location counter      */
    CCWLD_EXPR_BINARY = 4,          /* arithmetic / bitwise node     */
    CCWLD_EXPR_UNARY = 5,           /* unary op (neg, not, abs, …)   */
    CCWLD_EXPR_ALIGN = 6,           /* align(expr, boundary)         */
    CCWLD_EXPR_MAX = 7,             /* max(a, b)                     */
    CCWLD_EXPR_MIN = 8,             /* min(a, b)                     */
    CCWLD_EXPR_COND = 9,            /* cond(test, then, else)        */
    CCWLD_EXPR_DEFINED = 10,        /* defined(sym) — 1 or 0         */
    CCWLD_EXPR_REGION_ORIGIN = 11,  /* origin of named region        */
    CCWLD_EXPR_REGION_LENGTH = 12,  /* length of named region        */
    CCWLD_EXPR_SIZEOF = 13,         /* sizeof(section-name)          */
    CCWLD_EXPR_ADDR = 14,           /* addr(section-name)           */
    CCWLD_EXPR_LOADADDR = 15,       /* loadaddr(section-name)        */
    CCWLD_EXPR_SIZEOF_HEADERS = 16, /* sizeof_headers                */
    CCWLD_EXPR_SEGMENT_START = 17,  /* start of named segment        */
  } ccwld_expr_kind;

  /* Binary / unary operator tags.  Comparison and logical operators
   * use letter tags to avoid colliding with the arithmetic characters. */
  typedef enum
  {
    CCWLD_OP_ADD = '+',
    CCWLD_OP_SUB = '-',
    CCWLD_OP_MUL = '*',
    CCWLD_OP_DIV = '/',
    CCWLD_OP_MOD = '%',
    CCWLD_OP_AND = '&',
    CCWLD_OP_OR = '|',
    CCWLD_OP_XOR = '^',
    CCWLD_OP_SHL = '<',
    CCWLD_OP_SHR = '>',
    CCWLD_OP_NEG = '~',
    CCWLD_OP_NOT = '!',
    CCWLD_OP_ABS = 'A',
    CCWLD_OP_EQ = 'e', /* == */
    CCWLD_OP_NE = 'n', /* != */
    CCWLD_OP_LT = 'l', /* <  */
    CCWLD_OP_LE = 'L', /* <= */
    CCWLD_OP_GT = 'g', /* >  */
    CCWLD_OP_GE = 'G', /* >= */
    CCWLD_OP_LAND = 'a', /* && */
    CCWLD_OP_LOR = 'o'   /* || */
  } ccwld_op_tag;

  typedef struct ccwld_expr
  {
    ccwld_expr_kind kind;
    ccwld_op_tag op;      /* for BINARY / UNARY nodes          */
    uint64_t ival;        /* for INT literal / ALIGN boundary  */
    char *name;           /* for SYMBOL / REGION / SECTION refs */
    struct ccwld_expr *a; /* left child  (or operand for UNARY/ALIGN) */
    struct ccwld_expr *b; /* right child (ALIGN: expr boundary when ival==0;
                           * SEGMENT_START: default expr)             */
    struct ccwld_expr *c; /* third child (for COND: test-a-then-b-else-c) */
    /* --- internal: cycle-detection mark --- */
    int visited;
  } ccwld_expr;

  /* Constructors.  Unless documented otherwise the constructor takes
   * ownership of nothing — arguments are copied or borrowed per the
   * existing regime (names copied, child nodes owned). */
  ccwld_expr *ccwld_expr_int (uint64_t value);
  ccwld_expr *ccwld_expr_symbol (const char *name);
  ccwld_expr *ccwld_expr_dot (void);
  ccwld_expr *ccwld_expr_binary (ccwld_op_tag op, ccwld_expr *a,
                                 ccwld_expr *b);
  ccwld_expr *ccwld_expr_unary (ccwld_op_tag op, ccwld_expr *a);
  ccwld_expr *ccwld_expr_align (ccwld_expr *a, uint64_t boundary);
  ccwld_expr *ccwld_expr_align_to (ccwld_expr *a, ccwld_expr *boundary);
  ccwld_expr *ccwld_expr_max (ccwld_expr *a, ccwld_expr *b);
  ccwld_expr *ccwld_expr_min (ccwld_expr *a, ccwld_expr *b);
  ccwld_expr *ccwld_expr_cond (ccwld_expr *test, ccwld_expr *then_e,
                               ccwld_expr *else_e);
  ccwld_expr *ccwld_expr_defined (const char *symbol_name);
  ccwld_expr *ccwld_expr_region_origin (const char *region_name);
  ccwld_expr *ccwld_expr_region_length (const char *region_name);
  ccwld_expr *ccwld_expr_sizeof (const char *section_name);
  ccwld_expr *ccwld_expr_addr (const char *section_name);
  ccwld_expr *ccwld_expr_loadaddr (const char *section_name);
  ccwld_expr *ccwld_expr_sizeof_headers (void);
  ccwld_expr *ccwld_expr_segment_start (const char *segment_name);
  ccwld_expr *ccwld_expr_segment_start2 (const char *segment_name,
                                         ccwld_expr *default_value);

  /* Deep copy. */
  ccwld_expr *ccwld_expr_clone (const ccwld_expr *e);

  /* Free an expression tree recursively. */
  void ccwld_expr_free (ccwld_expr *e);

  /* Evaluate expression against a plan+dot.  Returns 0 on failure and
   * fills *error_message if provided.  Cycle detection is built-in. */
  int ccwld_expr_eval (const ccwld_expr *e, const struct ccwld_plan *plan,
                       uint64_t dot, uint64_t *out, char **error_message);

  /* Serialize an expression to a canonical string (for plan serialization). */
  void ccwld_expr_to_string (const ccwld_expr *e, char **out, size_t *len,
                             size_t *cap);

  /* Compare two expression trees for structural equality. */
  int ccwld_expr_equal (const ccwld_expr *a, const ccwld_expr *b);

  /* Reset internal state (visited flags) on the whole tree — called
   * after each evaluation pass. */
  void ccwld_expr_reset_visited (ccwld_expr *e);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_EXPR_H */
