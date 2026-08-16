## 1. `glue/GlueSTD.h` — complete, normative

The design decision that makes this header work: **IR nodes cross the ABI as 64-bit ids, never as pointers.** The host resolves ids inside its own IR; the executor and Kernels only ever hold integers. This eliminates ownership questions for nodes entirely. Only strings need an ownership rule, and there is exactly one: *strings are copied at the boundary, each side frees its own copies.*

```c
/* =========================================================================
 * GlueSTD.h — CCWeave Glue Standard
 * ABI version 1
 *
 * Contract between a CCWeave host and a Kernel Executor (an embedded
 * R7RS Scheme engine wrapper). S7 is the reference executor.
 *
 * Ownership rules (normative):
 *   - ccw_ir handles are owned by the host, opaque to the executor.
 *   - IR nodes cross this ABI as ccw_node (uint64_t) ids, never pointers.
 *     Id 0 is reserved as "no node" / nil.
 *   - Strings are copied at the boundary. A ccw_val of type CCW_T_STRING
 *     or CCW_T_SYMBOL owns its buffer; release with ccw_val_clear().
 *   - error_message out-params are malloc'd by the callee; caller frees.
 * ========================================================================= */

#ifndef CCW_GLUESTD_H
#define CCW_GLUESTD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCW_GLUE_ABI_VERSION 1

/* ---------- opaque handles ---------- */

typedef struct ccw_executor ccw_executor;  /* one embedded Scheme engine   */
typedef struct ccw_ir       ccw_ir;        /* one Weave IR module, host-owned */
typedef uint64_t            ccw_node;      /* IR node id; 0 = nil          */

/* ---------- status codes ---------- */

typedef enum {
    CCW_OK = 0,
    CCW_ERR_LOAD          = -1,  /* kernel file failed to load/parse       */
    CCW_ERR_NO_CAPABILITY = -2,  /* kernel lacks requested capability      */
    CCW_ERR_KERNEL        = -3,  /* kernel-apply raised a Scheme error     */
    CCW_ERR_ABI           = -4,  /* ABI version mismatch                   */
    CCW_ERR_ACCESSOR      = -5,  /* host accessor reported failure         */
    CCW_ERR_TYPE          = -6,  /* value of wrong type crossed boundary   */
    CCW_ERR_ARITY         = -7,  /* accessor called with wrong arg count   */
    CCW_ERR_OOM           = -8
} ccw_status;

/* ---------- boundary values ----------
 *
 * The closed set of value types that may cross between Scheme and C.
 * Deliberately minimal: no pairs/vectors cross the boundary; aggregate
 * results are returned as nodes (the host models lists of nodes as
 * iterable IR collections reachable via further accessor calls, or the
 * accessor takes an index argument — see Core Accessor Set).
 */

typedef enum {
    CCW_T_NIL = 0,
    CCW_T_BOOL,
    CCW_T_INT,      /* int64_t  */
    CCW_T_FLOAT,    /* double   */
    CCW_T_STRING,   /* owned, NUL-terminated, UTF-8 */
    CCW_T_SYMBOL,   /* owned, NUL-terminated; maps to a Scheme symbol */
    CCW_T_NODE      /* ccw_node id */
} ccw_type;

typedef struct {
    ccw_type type;
    union {
        bool     b;
        int64_t  i;
        double   f;
        char    *s;      /* STRING and SYMBOL; owned by this ccw_val */
        ccw_node node;
    } as;
} ccw_val;

/* Constructors (by value; string constructors copy their input). */
ccw_val ccw_nil(void);
ccw_val ccw_bool(bool b);
ccw_val ccw_int(int64_t i);
ccw_val ccw_float(double f);
ccw_val ccw_string(const char *s);   /* copies s */
ccw_val ccw_symbol(const char *s);   /* copies s */
ccw_val ccw_node_val(ccw_node n);

/* Frees owned string storage (if any) and resets to CCW_T_NIL.
 * Safe to call on any ccw_val, idempotent. */
void ccw_val_clear(ccw_val *v);

/* ---------- executor lifecycle ---------- */

ccw_executor *ccw_executor_create(void);
void          ccw_executor_destroy(ccw_executor *ex);

/* MUST return CCW_GLUE_ABI_VERSION for a conformant executor. Hosts
 * MUST verify this before any other call and refuse on mismatch. */
int ccw_executor_abi_version(const ccw_executor *ex);

/* Executor identification, e.g. "s7 10.6". Static string, do not free. */
const char *ccw_executor_name(const ccw_executor *ex);

/* ---------- host accessor registration ----------
 *
 * The host exposes its IR to Kernels by registering accessors BEFORE
 * loading any kernel. The executor binds each accessor as a procedure
 * in the (ccweave glue) library under `scheme_name`.
 *
 * Call discipline (normative):
 *   - `args` and `nargs` are the Scheme call's arguments, converted to
 *     ccw_val. args[] is executor-owned and valid only for the duration
 *     of the call; the accessor MUST NOT retain pointers into it.
 *   - On success: return CCW_OK and write the result into *result
 *     (host allocates via ccw_* constructors; executor takes ownership
 *     and will ccw_val_clear it after conversion to Scheme).
 *   - On failure: return CCW_ERR_ACCESSOR and, optionally, set
 *     *error_message (malloc'd; executor frees). The executor MUST
 *     surface this to the Kernel as a raised Scheme error whose
 *     condition message includes `scheme_name` and the message.
 *   - The executor performs arity checking against min/max_arity and
 *     raises in Scheme without calling fn on violation (CCW_ERR_ARITY).
 *
 * `ir` passed to the accessor is the handle given to ccw_kernel_apply
 * for the current invocation. Accessors are only callable during a
 * kernel-apply; calling a glue procedure outside one MUST raise.
 */

typedef ccw_status (*ccw_accessor_fn)(void          *host_ctx,
                                      ccw_ir        *ir,
                                      const ccw_val *args,
                                      int            nargs,
                                      ccw_val       *result,
                                      char         **error_message);

/* max_arity == -1 means variadic. Re-registering a name replaces the
 * previous binding (MUST NOT be done after any kernel is loaded). */
ccw_status ccw_glue_register(ccw_executor    *ex,
                             const char      *scheme_name,
                             int              min_arity,
                             int              max_arity,
                             ccw_accessor_fn  fn,
                             void            *host_ctx);

/* ---------- Core Accessor Set ----------
 *
 * A conformant HOST MUST register at least the following accessors.
 * Kernels MAY rely on these names being present; anything beyond this
 * set is a host extension and portable Kernels MUST feature-test with
 * (glue-has? 'name) before use.
 *
 *   Reflection
 *     (glue-has? sym)                       -> bool
 *     (ir-profile)                          -> symbol: tilly | on1x
 *   Navigation (collections are indexed; -count then -ref)
 *     (ir-function-count)                   -> int
 *     (ir-function-ref i)                   -> node
 *     (function-name f)                     -> string
 *     (function-block-count f)              -> int
 *     (function-block-ref f i)              -> node
 *     (block-instr-count b)                 -> int
 *     (block-instr-ref b i)                 -> node
 *   Inspection
 *     (instr-opcode ins)                    -> symbol
 *     (instr-operand-count ins)             -> int
 *     (instr-operand ins i)                 -> node
 *     (operand-const? n)                    -> bool
 *     (const-int-value n)                   -> int   (raises if not int const)
 *   Mutation (the ONLY mutation channel; host may interpose/log/reject)
 *     (instr-build opcode-sym operand-node ...) -> node  (detached instr)
 *     (instr-replace! old-ins new-ins)      -> nil
 *     (instr-insert-before! anchor new-ins) -> nil
 *     (instr-delete! ins)                   -> nil
 *     (const-int-build value)               -> node
 *
 * Profile-specific accessors (inline-cache slots, relocations, ...) are
 * host extensions following the same feature-test rule.
 */

/* ---------- kernel loading and invocation ---------- */

/* Loads and evaluates a kernel library. Returns kernel id >= 0, or a
 * negative ccw_status. The executor MUST verify the three required
 * exports (kernel-info, kernel-capabilities, kernel-apply) at load
 * time and fail with CCW_ERR_LOAD if any is missing. */
int ccw_kernel_load(ccw_executor *ex, const char *path,
                    char **error_message);

/* kernel-info, marshalled. Strings are copies; caller frees each. */
ccw_status ccw_kernel_info(ccw_executor *ex, int kernel_id,
                           char **name, char **version, char **description);

/* Live capability enumeration (single source of truth).
 * Returned capability strings are executor-owned and valid until the
 * next call on this executor; copy if you need to keep them. */
int         ccw_kernel_capability_count(ccw_executor *ex, int kernel_id);
const char *ccw_kernel_capability(ccw_executor *ex, int kernel_id, int idx);

/* Invoke (kernel-apply capability ir options).
 * `options` is a NULL-terminated array of "key=value" UTF-8 strings,
 * surfaced to the Kernel as an alist of (symbol . string).
 * The executor MUST verify the capability is in the kernel's live
 * capability list before dispatching (CCW_ERR_NO_CAPABILITY).
 * On CCW_ERR_KERNEL, *error_message carries the Scheme condition text. */
ccw_status ccw_kernel_apply(ccw_executor *ex, int kernel_id,
                            const char *capability,
                            ccw_ir *ir,
                            const char *const *options,
                            char **error_message);

/* Unload a kernel and release executor-side resources for it.
 * Kernel ids are never reused within one executor's lifetime. */
ccw_status ccw_kernel_unload(ccw_executor *ex, int kernel_id);

#ifdef __cplusplus
}
#endif
#endif /* CCW_GLUESTD_H */
```

Two normative points worth calling out, since they resolve gaps the v0.1 sketch left open:

- **No aggregate types cross the ABI.** Collections are traversed with `-count`/`-ref` accessor pairs. This keeps the marshalling layer trivial in every executor (S7, CHICKEN, Racket all only need to convert seven scalar-ish types) at the cost of chattier traversal — acceptable because accessor calls are in-process function calls, not IPC.
- **Mutation is builder-based.** Kernels construct detached instructions with `instr-build`, then splice with `instr-replace!`/`instr-insert-before!`. The host sees every structural edit as one of three operations, which is what makes the "host can interpose, log, or reject edits" requirement of §2.3 implementable.

---

## 2. Worked example Kernel

`kernels/strength-reduce.scm` — rewrites integer multiply-by-power-of-two into left shifts. Profile-agnostic (uses only the Core Accessor Set), engine-agnostic (pure R7RS, no S7-isms).

```scheme
;;; kernels/strength-reduce.scm
;;; CCWeave Kernel: strength reduction for integer multiplication.

(define-library (ccweave kernel strength-reduce)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . strength-reduce)
        (version     . "1.0.0")
        (description . "Rewrites imul-by-power-of-two into shl.")))

    (define (kernel-capabilities)
      '(opt.strength-reduction))

    ;; Returns the exponent k if n = 2^k with k >= 1, else #f.
    (define (log2-exact n)
      (let loop ((n n) (k 0))
        (cond ((<= n 1) (and (= n 1) (> k 0) k))
              ((odd? n) #f)
              (else (loop (quotient n 2) (+ k 1))))))

    ;; If ins is (imul x const-2^k), build (shl x k) — else #f.
    (define (reduction-for ins)
      (and (eq? (instr-opcode ins) 'imul)
           (= (instr-operand-count ins) 2)
           (let ((a (instr-operand ins 0))
                 (b (instr-operand ins 1)))
             ;; Normalize: constant on the right.
             (let-values (((x c) (if (operand-const? a)
                                     (values b a)
                                     (values a b))))
               (and (operand-const? c)
                    (not (operand-const? x))
                    (let ((k (log2-exact (const-int-value c))))
                      (and k
                           (instr-build 'shl x (const-int-build k)))))))))

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (new (reduction-for ins)))
                (when new (instr-replace! ins new))
                (loop (+ i 1) (if new (+ changed 1) changed)))))))

    (define (rewrite-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (rewrite-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.strength-reduction)
        (error "strength-reduce: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
```

Notes on how this exercises the spec: all IR access goes through Glue accessors (§2.3 purity), the capability check inside `kernel-apply` is defensive only — the executor already verified it per §3's invocation sequence; `options` is accepted but unused, which is legal; the same node-id handles flow in and out, and the Kernel returns the (mutated-through-accessors) `ir` handle.

---

## 3. Generated manifest entries

Output of `tools/ccw-manifest` after loading the kernel and calling its live `kernel-capabilities` (§4.2). Both files carry the mandatory generated-file header.

`manifests/Kernel.yaml`:

```yaml
# GENERATED by ccw-manifest — DO NOT EDIT.
# Source of truth: kernel-capabilities of each listed kernel.
# Regenerate: ccw-manifest ; verify: ccw-manifest --check
generator: ccw-manifest/0.1
glue_abi: 1
kernels:
  - path: kernels/strength-reduce.scm
    library: (ccweave kernel strength-reduce)
    name: strength-reduce
    version: 1.0.0
    description: Rewrites imul-by-power-of-two into shl.
    capabilities:
      - opt.strength-reduction
```

`manifests/Capabilities.yaml`:

```yaml
# GENERATED by ccw-manifest — DO NOT EDIT.
# Inverted index derived from Kernel.yaml.
# Regenerate: ccw-manifest ; verify: ccw-manifest --check
generator: ccw-manifest/0.1
glue_abi: 1
capabilities:
  opt.strength-reduction:
    - kernels/strength-reduce.scm
```

