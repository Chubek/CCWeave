/* Parthia runtime internals (§SML-PARTHIA §4–§8).
 *
 * This header is the shared contract between Parthia's compiler front end
 * (surface S-expression -> core AST), the evaluator, the Basis environment,
 * and the kernel-backed IO bridge.  Nothing here is installed; the public
 * façade remains include/sml_parthia.h.
 *
 * Memory discipline: every object created by the compiler or the evaluator
 * (AST nodes, values, environments, strings) comes from the runtime arena
 * and is released wholesale by ccw_sml_parthia_runtime_free.  Individual
 * objects are never freed; the arena is the GC (D-0052 keeps allocation
 * order deterministic).
 *
 * Execution modes:
 *   AoT — ccw_sml_parthia_run compiles the whole program, then evaluates it.
 *   JIT — ccw_sml_parthia_eval compiles one phrase at a time against the
 *         persistent runtime environment, caches compiled phrases by source
 *         text, and tiers hot closures up (environment flattening) once they
 *         pass PA_JIT_HOT_THRESHOLD calls. */

#ifndef CCW_PARTHIA_RT_H
#define CCW_PARTHIA_RT_H

#include "sml_parthia.h"

#include <setjmp.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct ccw_sml_parthia_runtime prt;

  /* ---------- arena ---------- */

  void *pa_alloc (prt *rt, size_t size);
  char *pa_strdup (prt *rt, const char *text);
  char *pa_strndup (prt *rt, const char *text, size_t length);

  /* ---------- surface S-expression reader ---------- */

  typedef struct psx
  {
    int is_list;
    char *atom;         /* when !is_list */
    struct psx **items; /* when is_list */
    size_t count;
  } psx;

  /* Reads one S-expression (the whole surface AST text).  Atoms that are
   * SML string/character constants keep their quotes and may contain
   * whitespace and parentheses. */
  psx *pa_sexp_read (prt *rt, const char *text, char **error);

  /* ---------- core AST ---------- */

  typedef struct pv pv;
  typedef struct pa_env pa_env;
  typedef struct pa_exp pa_exp;
  typedef struct pa_pat pa_pat;
  typedef struct pa_dec pa_dec;
  typedef struct pa_strdec pa_strdec;
  typedef struct pa_strexp pa_strexp;

  typedef struct
  {
    pa_pat *pat;
    pa_exp *body;
  } pa_rule;

  enum pa_exp_kind
  {
    PE_LIT,    /* lit */
    PE_VID,    /* path */
    PE_RECORD, /* labs + items */
    PE_SEL,    /* sel: #lab as a function */
    PE_UNIT,
    PE_TUPLE,  /* items */
    PE_LIST,   /* items */
    PE_SEQ,    /* items */
    PE_LET,    /* decs + a (body sequence) */
    PE_APP,    /* items[0] applied to items[1..] */
    PE_CONJ,   /* a, b */
    PE_DISJ,   /* a, b */
    PE_HANDLE, /* a + rules */
    PE_RAISE,  /* a */
    PE_IF,     /* a, b, c (c may be NULL -> unit) */
    PE_WHILE,  /* a, b */
    PE_CASE,   /* a + rules */
    PE_FN      /* rules */
  };

  struct pa_exp
  {
    int kind;
    pv *lit;     /* PE_LIT */
    char **path; /* PE_VID */
    size_t path_len;
    char *sel;      /* PE_SEL */
    char **labs;    /* PE_RECORD */
    pa_exp **items; /* RECORD/TUPLE/LIST/SEQ/APP */
    size_t count;
    pa_dec **decs; /* PE_LET */
    size_t ndecs;
    pa_rule *rules; /* PE_FN/PE_CASE/PE_HANDLE */
    size_t nrules;
    pa_exp *a, *b, *c;
  };

  enum pa_pat_kind
  {
    PP_WILD,
    PP_LIT, /* lit */
    PP_VID, /* path: binds unless it resolves to a constructor */
    PP_UNIT,
    PP_TUPLE,  /* items */
    PP_LIST,   /* items */
    PP_RECORD, /* labs + items + ellipsis */
    PP_CTOR,   /* path + arg */
    PP_AS,     /* aname + asub */
    PP_OR      /* l, r */
  };

  struct pa_pat
  {
    int kind;
    pv *lit;     /* PP_LIT */
    char **path; /* PP_VID/PP_CTOR */
    size_t path_len;
    char *aname;   /* PP_AS */
    pa_pat *asub;  /* PP_AS */
    pa_pat *l, *r; /* PP_OR */
    char **labs;   /* PP_RECORD */
    int ellipsis;
    pa_pat **items; /* TUPLE/LIST/RECORD */
    size_t count;
    pa_pat *arg; /* PP_CTOR */
  };

  typedef struct
  {
    char *name;
    int has_arg;
    char **alias_path; /* exception replication: source constructor */
    size_t alias_len;
  } pa_condef;

  typedef struct
  {
    char *tycon;
    pa_condef *cons;
    size_t ncons;
  } pa_datdef;

  enum pa_dec_kind
  {
    PD_SEQ,      /* seq: simultaneous `and` groups and dec runs */
    PD_VAL,      /* pat = rhs */
    PD_VALREC,   /* recnames[i] = recfns[i] (always PE_FN) */
    PD_TYPE,     /* erased */
    PD_DATATYPE, /* dts */
    PD_DATAREPL, /* repl_name = datatype repl_path */
    PD_ABSTYPE,  /* dts + b_decs (with-body; no opacity at runtime) */
    PD_EXN,      /* exns */
    PD_LOCAL,    /* a_decs (local) then b_decs (in) */
    PD_OPEN,     /* open_paths */
    PD_FIXITY,   /* already resolved by the parser; no-op */
    PD_DO        /* rhs; bind_it also binds `it` */
  };

  struct pa_dec
  {
    int kind;
    pa_dec **seq; /* PD_SEQ */
    size_t nseq;
    pa_pat *pat;     /* PD_VAL */
    pa_exp *rhs;     /* PD_VAL/PD_DO */
    int bind_it;     /* PD_DO */
    char **recnames; /* PD_VALREC */
    pa_exp **recfns;
    size_t nrec;
    pa_datdef *dts; /* PD_DATATYPE/PD_ABSTYPE */
    size_t ndts;
    char *repl_name; /* PD_DATAREPL */
    char **repl_path;
    size_t repl_path_len;
    pa_condef *exns; /* PD_EXN */
    size_t nexns;
    pa_dec **a_decs; /* PD_LOCAL/PD_ABSTYPE */
    size_t na;
    pa_dec **b_decs;
    size_t nb;
    char ***open_paths; /* PD_OPEN */
    size_t *open_lens;
    size_t nopen;
  };

  enum pa_strexp_kind
  {
    PSE_STRUCT,    /* decs */
    PSE_STRID,     /* path */
    PSE_CONSTRAIN, /* sub (signature is transparent at runtime) */
    PSE_FCTAPP,    /* fct + arg or argdecs */
    PSE_LET        /* decs + sub */
  };

  struct pa_strexp
  {
    int kind;
    pa_strdec **decs; /* STRUCT/LET */
    size_t ndecs;
    char **path; /* STRID */
    size_t path_len;
    pa_strexp *sub;      /* CONSTRAIN/LET */
    char *fct;           /* FCTAPP */
    pa_strexp *arg;      /* FCTAPP (strexp argument) */
    pa_strdec **argdecs; /* FCTAPP (inline strdec argument) */
    size_t nargdecs;
  };

  enum pa_strdec_kind
  {
    PSD_DEC,       /* dec */
    PSD_STRUCTURE, /* binds */
    PSD_LOCAL,     /* a (local) then b (in) */
    PSD_SIGNATURE, /* recorded at elaboration; no runtime content */
    PSD_FUNCTOR    /* fct_name (fct_param) = fct_body */
  };

  typedef struct
  {
    char *name;
    pa_strexp *def;
  } pa_strbind;

  struct pa_strdec
  {
    int kind;
    pa_dec *dec;       /* PSD_DEC */
    pa_strbind *binds; /* PSD_STRUCTURE */
    size_t nbinds;
    pa_strdec **a; /* PSD_LOCAL */
    size_t na;
    pa_strdec **b;
    size_t nb;
    char *fct_name; /* PSD_FUNCTOR */
    char *fct_param;
    pa_strexp *fct_body;
  };

  typedef struct
  {
    pa_strdec **decs;
    size_t count;
  } pa_program;

  /* ---------- values ---------- */

  enum pv_kind
  {
    PV_UNIT,
    PV_INT,
    PV_WORD,
    PV_REAL,
    PV_CHAR,
    PV_STRING,
    PV_TUPLE,
    PV_RECORD,
    PV_CTOR,   /* constructed value: nullary when c.arg == NULL */
    PV_CTORFN, /* constructor awaiting its argument */
    PV_CLOSURE,
    PV_BUILTIN,
    PV_REF,
    PV_STRUCT,
    PV_FUNCTOR,
    PV_STREAM
  };

  struct pv
  {
    int kind;
    long long i;          /* PV_INT */
    unsigned long long w; /* PV_WORD */
    double r;             /* PV_REAL */
    int ch;               /* PV_CHAR */
    struct
    {
      char *data;
      size_t len;
    } s; /* PV_STRING */
    struct
    {
      pv **items;
      size_t n;
    } t; /* PV_TUPLE */
    struct
    {
      char **labs;
      pv **items;
      size_t n;
    } rec; /* PV_RECORD */
    struct
    {
      char *name;
      pv *arg;
      int is_exn;
    } c; /* PV_CTOR */
    struct
    {
      char *name;
      int is_exn;
    } cf; /* PV_CTORFN */
    struct
    {
      pa_rule *rules;
      size_t nrules;
      pa_env *env;
      char *name;
      unsigned long calls;
      int hot;
    } f; /* PV_CLOSURE */
    struct
    {
      const char *name;
      int arity;
      pv *(*fn) (prt *, pv *, pv **, int);
      pv **got;
      int ngot;
    } b;         /* PV_BUILTIN (curried) */
    pv *refcell; /* PV_REF */
    pa_env *str; /* PV_STRUCT */
    struct
    {
      char *param;
      pa_strexp *body;
      pa_env *env;
    } fc; /* PV_FUNCTOR */
    struct
    {
      int fd;
      int readable;
      void *file;
      unsigned char *buf;
      size_t len;
      size_t pos;
      int is_open;
    } st; /* PV_STREAM */
  };

  /* ---------- environments ---------- */

  typedef struct pa_binding
  {
    char *name;
    pv *val;
    struct pa_binding *next;
  } pa_binding;

  struct pa_env
  {
    pa_binding *binds;
    pa_env *parent;
  };

  /* ---------- runtime ---------- */

  typedef struct sml_ext_entry
  {
    char *name;
    ccw_sml_native_fn invoke;
    void *userdata;
    void *handle;
    struct sml_ext_entry *next;
  } sml_ext_entry;

  typedef struct pa_handler
  {
    jmp_buf jb;
    struct pa_handler *prev;
  } pa_handler;

  typedef struct pa_tyc
  {
    char *name;
    pv **ctors;
    size_t n;
    struct pa_tyc *next;
  } pa_tyc;

  typedef struct pa_cache
  {
    char *key; /* expanded source text */
    pa_program *prog;
    struct pa_cache *next;
  } pa_cache;

  /* A closure is tiered up (its captured environment is flattened to a
   * single frame above the global frame) after this many calls. */
#define PA_JIT_HOT_THRESHOLD 64
#define PA_MAX_EVAL_DEPTH 25000

  struct ccw_sml_parthia_runtime
  {
    sml_ext_entry *extensions; /* native extension registry (public ABI) */
    void *arena;               /* klib blocked arena */
    pa_env *global;
    pv *v_true;
    pv *v_false;
    pa_handler *handlers;
    pv *exn;      /* packet in flight during a raise */
    pa_tyc *tycs; /* tycon -> constructor table (datarepl) */
    unsigned gensym;
    unsigned long depth;
    pa_cache *cache;           /* JIT phrase code cache */
    unsigned long jit_phrases; /* phrases compiled */
    unsigned long jit_hits;    /* cache hits */
    unsigned long jit_spec;    /* closures tiered up */
    int init_failed;
    char init_error[256];
  };

  /* ---------- compiler ---------- */

  pa_program *pa_compile_surface (prt *rt, const char *surface, char **error);

  /* ---------- evaluator ---------- */

  /* Evaluates the program into rt->global.  Returns 1 on success; on an
   * uncaught exception or runtime fault returns 0 with a malloc'd message
   * in *error (NULL allowed). */
  int pa_eval_program (prt *rt, pa_program *prog, char **error);

  pv *pa_eval_exp (prt *rt, pa_env *env, pa_exp *exp);
  pv *pa_apply (prt *rt, pv *fn, pv *arg);
  void pa_raise (prt *rt, pv *packet);
  void pa_fail (prt *rt, const char *fmt, ...);

  /* ---------- values and environments ---------- */

  pv *pa_new (prt *rt, int kind);
  pv *pa_ctor_make (prt *rt, const char *name, pv *arg, int is_exn);
  pv *pa_bool (prt *rt, int truth);
  pv *pa_int (prt *rt, long long value);
  pv *pa_real (prt *rt, double value);
  pv *pa_string (prt *rt, const char *data, size_t len);
  pv *pa_unit (prt *rt);
  char *pa_show (prt *rt, pv *value); /* arena string */
  int pa_equal (prt *rt, pv *left, pv *right);

  pa_env *pa_env_new (prt *rt, pa_env *parent);
  void pa_bind (prt *rt, pa_env *frame, const char *name, pv *value);
  pv *pa_lookup (prt *rt, pa_env *env, const char *name);
  pv *pa_lookup_path (prt *rt, pa_env *env, char **path, size_t n);
  void pa_def_native (prt *rt, pa_env *env, const char *name, int arity,
                      pv *(*fn) (prt *, pv *, pv **, int));

  /* ---------- basis / kernel IO ---------- */

  void pa_basis_install (prt *rt);
  void pa_kio_install (prt *rt);
  const char *pa_prelude_source (void);

#ifdef __cplusplus
}
#endif
#endif /* CCW_PARTHIA_RT_H */
