/* §2.3: deferred expression AST — shared between both frontends.
 * Expression-valued fields hold these AST nodes, never pre-computed integers.
 * Evaluation happens during layout in plan order against a live location
 * counter.
 *
 * Also defines the plan IR struct types (§2.1) that the expression
 * evaluator needs to dereference (mems, secs, syms, phdrs, plan). */
#ifndef CCWLD_EXPR_H
#define CCWLD_EXPR_H

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

  /* --- output descriptor (§2.1) --- */
  typedef struct
  {
    char *kind;   /* "exe" | "dso" | "reloc" | "pie" */
    char *format; /* "elf" | "pe" | "macho"           */
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

  /* --- output section (§2.1) --- */
  typedef struct
  {
    char *name;              /* output section name             */
    char *region;            /* memory region name              */
    char *at_region;         /* LMA region (or NULL)            */
    char *selector;          /* input section selector (match)  */
    char *keep;              /* KEEP selector (or NULL)         */
    uint64_t align;          /* alignment requirement           */
    int load;                /* loadable flag                   */
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
    /* --- resolved value (set during layout) --- */
    uint64_t resolved_value;
    int resolved;
  } ccwld_sym;

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
  typedef enum
  {
    CCWLD_PHASE_LOAD = 0,
    CCWLD_PHASE_RESOLVE = 1,
    CCWLD_PHASE_LTO = 2,
    CCWLD_PHASE_GC = 3,
    CCWLD_PHASE_LAYOUT = 4,
    CCWLD_PHASE_RELOCATE = 5,
    CCWLD_PHASE_EMIT = 6,
  } ccwld_phase;

  typedef int (*ccwld_hook_fn) (ccwld_phase phase, struct ccwld_link *link,
                                void *user);

  typedef struct
  {
    ccwld_phase phase;
    ccwld_hook_fn fn;
    void *user;
  } ccwld_hook;

  /* --- link handle (passed to phase hooks) --- */
  typedef struct ccwld_link
  {
    struct ccwld_plan *plan;
    ccwld_phase phase;
    /* --- introspection --- */
    void *phase_state; /* phase-specific opaque state */
  } ccwld_link;

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

    /* --- symbols --- */
    ccwld_sym *syms;
    size_t nsyms;
    size_t csyms;

    /* --- program headers --- */
    ccwld_phdr *phdrs;
    size_t nphdrs;
    size_t cphdrs;

    /* --- version nodes --- */
    ccwld_ver *vers;
    size_t nvers;
    size_t cvers;

    /* --- LTO / plugins / hooks --- */
    ccwld_lto_cfg lto;
    ccwld_plugin *plugins;
    size_t nplugins;
    size_t cplugins;
    ccwld_hook *hooks;
    size_t nhooks;
    size_t chooks;

    /* --- serialized canonical form --- */
    char *serialized;
    size_t serialized_len;

    /* --- gensym counter --- */
    unsigned gensym;

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

  /* Binary / unary operator tags. */
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
  } ccwld_op_tag;

  typedef struct ccwld_expr
  {
    ccwld_expr_kind kind;
    ccwld_op_tag op;      /* for BINARY / UNARY nodes          */
    uint64_t ival;        /* for INT literal                   */
    char *name;           /* for SYMBOL / REGION / SECTION refs */
    struct ccwld_expr *a; /* left child  (or operand for UNARY/ALIGN) */
    struct ccwld_expr *b; /* right child (or boundary for ALIGN)     */
    struct ccwld_expr *c; /* third child (for COND: test-a-then-b-else-c) */
    /* --- internal: cycle-detection mark --- */
    int visited;
  } ccwld_expr;

  /* Constructors. */
  ccwld_expr *ccwld_expr_int (uint64_t value);
  ccwld_expr *ccwld_expr_symbol (const char *name);
  ccwld_expr *ccwld_expr_dot (void);
  ccwld_expr *ccwld_expr_binary (ccwld_op_tag op, ccwld_expr *a,
                                 ccwld_expr *b);
  ccwld_expr *ccwld_expr_unary (ccwld_op_tag op, ccwld_expr *a);
  ccwld_expr *ccwld_expr_align (ccwld_expr *a, uint64_t boundary);
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
