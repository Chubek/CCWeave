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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CCW_GLUE_ABI_VERSION 1

  /* ---------- opaque handles ---------- */

  typedef struct ccw_executor ccw_executor; /* one embedded Scheme engine   */
  typedef struct ccw_ir ccw_ir; /* one Weave IR module, host-owned */
  typedef uint64_t ccw_node;    /* IR node id; 0 = nil          */

  /* ---------- status codes ---------- */

  typedef enum
  {
    CCW_OK = 0,
    CCW_ERR_LOAD = -1,          /* kernel file failed to load/parse       */
    CCW_ERR_NO_CAPABILITY = -2, /* kernel lacks requested capability      */
    CCW_ERR_KERNEL = -3,        /* kernel-apply raised a Scheme error     */
    CCW_ERR_ABI = -4,           /* ABI version mismatch                   */
    CCW_ERR_ACCESSOR = -5,      /* host accessor reported failure         */
    CCW_ERR_TYPE = -6,          /* value of wrong type crossed boundary   */
    CCW_ERR_ARITY = -7,         /* accessor called with wrong arg count   */
    CCW_ERR_OOM = -8,
    CCW_E_ISL_NONAFFINE = -9, /* region cannot be represented affinely */
    CCW_E_ISL_QUOTA = -10,    /* deterministic ISL operation quota    */
    CCW_E_VEC_ILLEGAL = -11   /* vector op reached lowering without map */
  } ccw_status;

  /* ---------- portable SIMD value surface (SIMDe-backed) ----------
   *
   * These are fixed-width value types, deliberately separate from ccw_val.
   * They never cross the Scheme boundary.  The implementation stores the
   * corresponding SIMDe native/emulated value in the byte representation.
   */
  typedef struct
  {
    unsigned char bytes[16];
  } ccw_v128;
  typedef struct
  {
    unsigned char bytes[32];
  } ccw_v256;

  ccw_v128 ccw_simde_load (const void *p);
  ccw_v128 ccw_simde_loadu (const void *p);
  void ccw_simde_store (void *p, ccw_v128 v);
  void ccw_simde_storeu (void *p, ccw_v128 v);
  ccw_v256 ccw_simde_load256 (const void *p);
  ccw_v256 ccw_simde_loadu256 (const void *p);
  void ccw_simde_store256 (void *p, ccw_v256 v);
  void ccw_simde_storeu256 (void *p, ccw_v256 v);

  ccw_v128 ccw_simde_add_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_sub_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_mul_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_add_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_sub_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_mul_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_div_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v256 ccw_simde_add_f32x8 (ccw_v256 a, ccw_v256 b);
  ccw_v256 ccw_simde_mul_f32x8 (ccw_v256 a, ccw_v256 b);
  ccw_v128 ccw_simde_shuffle (ccw_v128 a, const int indices[4]);
  ccw_v128 ccw_simde_select (ccw_v128 mask, ccw_v128 a, ccw_v128 b);
  float ccw_simde_hreduce_add_f32x4 (ccw_v128 a);
  double ccw_simde_hreduce_add_f64x2 (ccw_v128 a);

  /* ---------- boundary values ----------
   *
   * The closed set of value types that may cross between Scheme and C.
   * Deliberately minimal: no pairs/vectors cross the boundary; aggregate
   * results are returned as nodes (the host models lists of nodes as
   * iterable IR collections reachable via further accessor calls, or the
   * accessor takes an index argument — see Core Accessor Set).
   */

  typedef enum
  {
    CCW_T_NIL = 0,
    CCW_T_BOOL,
    CCW_T_INT,    /* int64_t  */
    CCW_T_FLOAT,  /* double   */
    CCW_T_STRING, /* owned, NUL-terminated, UTF-8 */
    CCW_T_SYMBOL, /* owned, NUL-terminated; maps to a Scheme symbol */
    CCW_T_NODE    /* ccw_node id */
  } ccw_type;

  typedef struct
  {
    ccw_type type;
    union
    {
      bool b;
      int64_t i;
      double f;
      char *s; /* STRING and SYMBOL; owned by this ccw_val */
      ccw_node node;
    } as;
  } ccw_val;

  /* ---------- pinned ISL polyhedral bindings ----------
   *
   * These handles own ISL objects in the pinned context created by
   * ccw_isl_ctx_new_pinned().  The textual forms are canonical ISL strings;
   * callers own returned strings and must free them with free().
   */
  typedef struct ccw_isl_ctx ccw_isl_ctx;
  typedef struct ccw_isl_uset ccw_isl_uset;
  typedef struct ccw_isl_umap ccw_isl_umap;
  typedef struct ccw_isl_schedule ccw_isl_schedule;

  ccw_isl_ctx *ccw_isl_ctx_new_pinned (void);
  void ccw_isl_ctx_free (ccw_isl_ctx *ctx);
  unsigned long ccw_isl_ctx_quota (const ccw_isl_ctx *ctx);

  ccw_isl_uset *ccw_isl_uset_parse (ccw_isl_ctx *ctx, const char *text);
  char *ccw_isl_uset_serialize (const ccw_isl_uset *uset);
  void ccw_isl_uset_free (ccw_isl_uset *uset);

  ccw_isl_umap *ccw_isl_umap_parse (ccw_isl_ctx *ctx, const char *text);
  char *ccw_isl_umap_serialize (const ccw_isl_umap *umap);
  void ccw_isl_umap_free (ccw_isl_umap *umap);

  ccw_isl_schedule *ccw_isl_schedule_parse (ccw_isl_ctx *ctx,
                                            const char *text);
  char *ccw_isl_schedule_serialize (const ccw_isl_schedule *schedule);
  void ccw_isl_schedule_free (ccw_isl_schedule *schedule);

  /* Constructors (by value; string constructors copy their input). */
  ccw_val ccw_nil (void);
  ccw_val ccw_bool (bool b);
  ccw_val ccw_int (int64_t i);
  ccw_val ccw_float (double f);
  ccw_val ccw_string (const char *s); /* copies s */
  ccw_val ccw_symbol (const char *s); /* copies s */
  ccw_val ccw_node_val (ccw_node n);

  /* Frees owned string storage (if any) and resets to CCW_T_NIL.
   * Safe to call on any ccw_val, idempotent. */
  void ccw_val_clear (ccw_val *v);

  /* ---------- executor lifecycle ---------- */

  ccw_executor *ccw_executor_create (void);
  void ccw_executor_destroy (ccw_executor *ex);

  /* MUST return CCW_GLUE_ABI_VERSION for a conformant executor. Hosts
   * MUST verify this before any other call and refuse on mismatch. */
  int ccw_executor_abi_version (const ccw_executor *ex);

  /* Executor identification, e.g. "s7 10.6". Static string, do not free. */
  const char *ccw_executor_name (const ccw_executor *ex);

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

  typedef ccw_status (*ccw_accessor_fn) (void *host_ctx, ccw_ir *ir,
                                         const ccw_val *args, int nargs,
                                         ccw_val *result,
                                         char **error_message);

  /* max_arity == -1 means variadic. Re-registering a name replaces the
   * previous binding (MUST NOT be done after any kernel is loaded). */
  ccw_status ccw_glue_register (ccw_executor *ex, const char *scheme_name,
                                int min_arity, int max_arity,
                                ccw_accessor_fn fn, void *host_ctx);

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
   *     (const-int-value n)                   -> int   (raises if not int
   * const) Mutation (the ONLY mutation channel; host may interpose/log/reject)
   *     (instr-build opcode-sym operand-node ...) -> node  (detached instr)
   *     (instr-replace! old-ins new-ins)      -> nil
   *     (instr-insert-before! anchor new-ins) -> nil
   *     (instr-delete! ins)                   -> nil
   *     (const-int-build value)               -> node
   *     (syscall-build number operand-node ...) -> node
   *
   * `syscall-build` constructs the shared core `syscall` instruction. The
   * platform syscall kernels lower it to an architecture-specific trap
   * opcode. At most six ABI argument operands are accepted.
   *
   * Low-level I/O wrappers construct `io.read`, `io.write`, `io.close`, and
   * `io.open`; the platform I/O kernels lower these to `syscall` with the
   * appropriate Linux number and, where required, an `openat` dirfd.
   *     (io-read-build fd buffer count)          -> node
   *     (io-write-build fd buffer count)         -> node
   *     (io-close-build fd)                      -> node
   *     (io-open-build path flags mode)          -> node
   *
   * Profile-specific accessors (inline-cache slots, relocations, ...) are
   * host extensions following the same feature-test rule.
   */

  /* ---------- Approved extension set: Phases 1-2 ----------
   *
   * These optional accessors promote the minimal semantics in
   * docs/GLUE_EXTENSIONS_DRAFT.md. Kernels MUST feature-test them with
   * glue-has? before use.
   *
   *   General IR
   *     (node-kind node)                         -> symbol
   *     (operand-kind operand)                   -> symbol
   *     (operand-name operand)                   -> string | nil
   *     (instr-dest instruction)                 -> string | nil
   *     (instr-set-dest! instruction name)       -> nil
   *     (operand-reg-build name)                 -> node
   *     (instr-set-operand! instruction index operand) -> nil
   *   Analysis facts
   *     (analysis-put! capability subject key value) -> nil
   *   Control flow
   *     (block-succ-count block)                 -> int
   *     (block-succ-ref block index)             -> node
   *     (block-pred-count block)                 -> int
   *     (block-pred-ref block index)             -> node
   *     (block-delete! block)                     -> nil
   *     (block-merge! first second)                -> nil
   *
   * Analysis facts are host-owned attributes. `value` is one ccw_val scalar
   * and may be retrieved by host consumers under the namespaced key
   * `analysis.<capability>.<key>`.
   */

  /* ---------- kernel loading and invocation ---------- */

  /* Loads and evaluates a kernel library. Returns kernel id >= 0, or a
   * negative ccw_status. The executor MUST verify the three required
   * exports (kernel-info, kernel-capabilities, kernel-apply) at load
   * time and fail with CCW_ERR_LOAD if any is missing. */
  int ccw_kernel_load (ccw_executor *ex, const char *path,
                       char **error_message);

  /* kernel-info, marshalled. Strings are copies; caller frees each. */
  ccw_status ccw_kernel_info (ccw_executor *ex, int kernel_id, char **name,
                              char **version, char **description);

  /* Live capability enumeration (single source of truth).
   * Returned capability strings are executor-owned and valid until the
   * next call on this executor; copy if you need to keep them. */
  int ccw_kernel_capability_count (ccw_executor *ex, int kernel_id);
  const char *ccw_kernel_capability (ccw_executor *ex, int kernel_id, int idx);

  /* Invoke (kernel-apply capability ir options).
   * `options` is a NULL-terminated array of "key=value" UTF-8 strings,
   * surfaced to the Kernel as an alist of (symbol . string).
   * The executor MUST verify the capability is in the kernel's live
   * capability list before dispatching (CCW_ERR_NO_CAPABILITY).
   * On CCW_ERR_KERNEL, *error_message carries the Scheme condition text. */
  ccw_status ccw_kernel_apply (ccw_executor *ex, int kernel_id,
                               const char *capability, ccw_ir *ir,
                               const char *const *options,
                               char **error_message);

  /* Unload a kernel and release executor-side resources for it.
   * Kernel ids are never reused within one executor's lifetime. */
  ccw_status ccw_kernel_unload (ccw_executor *ex, int kernel_id);

#ifdef __cplusplus
}
#endif
#endif /* CCW_GLUESTD_H */
