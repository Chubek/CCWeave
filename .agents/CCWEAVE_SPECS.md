# CCWeave Specification, v0.1

Keywords MUST, SHOULD, MAY are used per RFC 2119.

## 1. Overview and Layering

CCWeave is a modular compiler infrastructure organized as five layers plus one auxiliary engine:
```
Swaff        (frontend orchestration, Tree-sitter based)
Kliche       (paradigm stereotypes: functional, imperative, OOP)
Weave IR     (single IR core; two profiles: Tilly and On1x)
Glue         (C ABI bridging Kernels to the host, GlueSTD.h)
Kernels      (R7RS Scheme libraries describing compilation logic)

Oeuph        (equality-saturation rewrite engine; operates on Weave IR)
```

Each layer depends only on the layer directly beneath it. Oeuph depends on Weave IR and Glue but is not in the vertical stack.

## 2. Kernels

### 2.1 Definition

A Kernel is an R7RS Scheme **library** (in the `define-library` sense) stored in a `.scm` file. Kernels are executable Scheme code; the earlier phrase "declarative by intention" is retired. The precise property is: **a Kernel defines transformations without committing to a pipeline position.** Scheduling is the host's responsibility, never the Kernel's.

Kernel source is NEVER embedded in YAML. YAML files are metadata only (see §4).

### 2.2 Structure

A Kernel MUST export:

```scheme
(define-library (ccweave kernel regalloc-linear)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    ;; (kernel-info) -> alist: name, version, description
    ;; (kernel-capabilities) -> list of capability symbols
    ;; (kernel-apply capability ir-handle options) -> ir-handle | error-object
    ...))
```

- `kernel-info` MUST return an association list containing at least `name` (symbol), `version` (semver string), and `description` (string).
- `kernel-capabilities` MUST return a list of capability identifiers (§4.1). This list is the **single source of truth** for what the Kernel provides.
- `kernel-apply` is the sole entry point. It receives a capability identifier, an opaque IR handle (§5.4), and an options alist. It MUST return either a (possibly identical) IR handle or a Scheme error object. It MUST NOT mutate global state.

### 2.3 Calling convention

The Glue invokes a Kernel as follows:

1. Load the `.scm` file into the embedded Scheme engine.
2. Call `kernel-capabilities`; verify the requested capability is present.
3. Call `(kernel-apply cap ir-handle options)`.
4. Marshal the result back across the C ABI (§3).

Kernels MUST be pure with respect to the IR handle: all mutation goes through Glue-provided accessor procedures, so the host can interpose, log, or reject edits.

## 3. Glue Standard (`GlueSTD.h`)

### 3.1 Scope

`GlueSTD.h` is a single C header defining the ABI between the host and a **Kernel Executor**. An executor wraps one embedded Scheme engine. S7 is the reference executor; Racket (embedded) and CHICKEN executors MAY be substituted. Conformance means implementing every function in this header with the documented semantics.

### 3.2 Interface (normative sketch)

```c
/* GlueSTD.h — CCWeave Glue Standard, ABI version 1 */

#define CCW_GLUE_ABI_VERSION 1

typedef struct ccw_executor ccw_executor;   /* opaque */
typedef struct ccw_ir       ccw_ir;         /* opaque IR handle, owned by host */

typedef enum {
    CCW_OK = 0,
    CCW_ERR_LOAD,          /* kernel file failed to load/parse */
    CCW_ERR_NO_CAPABILITY, /* kernel does not provide requested capability */
    CCW_ERR_KERNEL,        /* kernel-apply raised an error */
    CCW_ERR_ABI            /* executor/host ABI version mismatch */
} ccw_status;

ccw_executor *ccw_executor_create(void);
void          ccw_executor_destroy(ccw_executor *ex);
int           ccw_executor_abi_version(const ccw_executor *ex);

/* Loads a kernel; returns a kernel id >= 0, or negative ccw_status. */
int ccw_kernel_load(ccw_executor *ex, const char *path);

/* Capability enumeration: source of truth, read from live kernel. */
int ccw_kernel_capability_count(ccw_executor *ex, int kernel_id);
const char *ccw_kernel_capability(ccw_executor *ex, int kernel_id, int idx);

/* Invocation. options is a NULL-terminated array of "key=value" strings. */
ccw_status ccw_kernel_apply(ccw_executor *ex, int kernel_id,
                            const char *capability,
                            ccw_ir *ir, const char *const *options,
                            char **error_message /* out, host frees */);
```

### 3.3 Requirements

- The executor MUST check `CCW_GLUE_ABI_VERSION` at creation and fail with `CCW_ERR_ABI` on mismatch.
- IR handles are owned by the host. Kernels access them only through accessor procedures registered by the host into the Scheme environment under the `(ccweave glue)` library.
- Executors MUST be substitutable without recompiling Kernels (Kernels are engine-agnostic R7RS; engine-specific extensions are forbidden in conformant Kernels).

## 4. Capability System

### 4.1 Capability identifiers

A capability is a dotted lowercase symbol: `<domain>.<operation>[.<variant>]`, e.g. `regalloc.linear-scan`, `lower.closure-conversion`, `opt.licm`. Identifiers MUST match `[a-z0-9-]+(\.[a-z0-9-]+)+`.

### 4.2 Single source of truth and generated manifests

The **only** authoritative capability list is the return value of each Kernel's `kernel-capabilities`. The two YAML files are **generated artifacts**:

- `manifests/Kernel.yaml` — per-kernel index: path, name, version, capabilities. Generated by the tool `ccw-manifest`.
- `manifests/Capabilities.yaml` — inverted index: capability → list of providing kernels. Generated from `Kernel.yaml` by the same tool.

Both files MUST carry a generated-file header and MUST NOT be hand-edited. `ccw-manifest --check` regenerates in-memory and diffs against the on-disk files; CI MUST run this check, so drift is a build failure rather than a runtime surprise.

## 5. Weave IR: One Core, Two Profiles

### 5.1 Rationale

The earlier design specified Tilly (for compilers) and On1x (for VMs/interpreters) as separate IRs. They shared nearly all machinery. This specification replaces them with a single IR core, **Weave IR**, and two profiles. "Tilly" and "On1x" survive as profile names.

### 5.2 Core (shared, normative)

The core defines: the type system, functions/blocks/instructions, the textual syntax (parsed with MPC), the programmatic C API, the native-extension API, and Glue integration. Everything in the core is available to both profiles.

### 5.3 Profiles

- **Tilly profile (ahead-of-time)** adds: static call and relocation constructs, link-section attributes, whole-module layout directives. It forbids dynamic-dispatch metadata.
- **On1x profile (dynamic execution)** adds: first-class dynamic dispatch sites, inline-cache slots, deoptimization metadata, and safepoint annotations. These constructs are the concrete divergence that justifies the profile split; no other divergence is permitted.

A module declares its profile in its header. Kernels MAY declare (via a capability variant, e.g. `opt.licm.on1x`) that they require a profile; profile-agnostic Kernels operate on the core subset only.

### 5.4 Canonical representation

The programmatic (in-memory) form is canonical. The textual syntax is a serialization of it: parsing text yields the same in-memory structures the C API builds, and any in-memory module can be printed to text (round-trip is REQUIRED). This removes the earlier one-way asymmetry and is what makes §7 possible.

Weave IR is deliberately conventional in its instruction inventory; its distinguishing features are the Kernel/capability integration and the profile mechanism, not novel instructions.

## 6. Kliche and Swaff

### 6.1 Kliche

Kliche provides three stereotypes — `functional`, `imperative`, `oop` — each a library of Weave IR construction patterns (closure records, vtable layout, exception frames, etc.). A stereotype is a documented mapping from paradigm concepts to core-IR construction calls; it MUST NOT require a specific profile, though it MAY offer profile-specific refinements.

### 6.2 Swaff

Swaff orchestrates swappable frontends. A Swaff frontend is a pair:

1. a vendored Tree-sitter grammar, and
2. a **lowering adapter** that walks the Tree-sitter CST and emits calls into a Kliche stereotype.

The adapter is required and non-trivial: Tree-sitter produces concrete syntax trees oriented toward editors and error recovery, not ASTs. The adapter contract: it MUST handle Tree-sitter `ERROR`/`MISSING` nodes explicitly (reject or recover, but decide), MUST discard or normalize trivia, and MUST be the only component that sees CST node types. Nothing above or below Swaff depends on Tree-sitter.

## 7. Oeuph

### 7.1 Scope

Oeuph is an equality-saturation (e-graph) rewrite engine. It operates on the **canonical in-memory Weave IR** (§5.4). Because the textual syntax round-trips through the same representation, Oeuph works identically on IR built programmatically or parsed from text. The earlier restriction to the syntactic interface is removed.

### 7.2 What Oeuph does and does not do

Oeuph performs **semantics-preserving** rewriting: every rule asserts an equivalence, saturation grows the e-graph with equivalent forms, and extraction selects one by a cost function. Two formerly conflated uses are now distinct:

- **Optimization**: extraction under a performance/size cost model.
- **Normalization**: rewriting legal-but-undesirable constructs into canonical equivalents, under a canonicalization cost model.

**Repair of faulty code is out of scope.** Transforming incorrect code into correct code is not an equivalence and MUST NOT be expressed as an Oeuph rule. If a repair facility is added later, it will be a separate component with its own soundness story.

### 7.3 Rules and `rewrite-salvo`

Rules are pattern/action pairs written in Scheme and bound via S7 through the Glue. `rewrite-salvo` ships 250+ rule files, organized by domain. Each rule file MUST declare:

- a **ruleset** name (rules are enabled per-ruleset, never globally by default),
- for each rule, an equivalence direction (bidirectional by default) and an optional side-condition predicate.

### 7.4 Saturation control (normative)

Because rule ordering and e-graph growth are the hard part of egg-style systems, conformant Oeuph implementations MUST provide:

- **Budgets**: configurable limits on e-graph nodes, e-classes, iterations, and wall time; hitting a budget stops saturation and proceeds to extraction (saturation is best-effort, not guaranteed).
- **Determinism**: with a fixed seed and budget, saturation and extraction MUST be reproducible.
- **Conflict semantics**: rules within one ruleset are unordered (equality saturation makes ordering irrelevant for soundness); extraction, not rule priority, resolves "conflicts." Rule authors MUST NOT rely on application order.
- **Diagnostics**: per-ruleset match counts and growth statistics, so rule explosion is observable.

## 8. Repository Layout and Dependencies
```
ccweave/
  glue/GlueSTD.h            executors/ (s7/ ...)
  kernels/*.scm             manifests/  (generated; CI-checked)
  ir/                       core + tilly/ + on1x/ profiles
  kliche/                   swaff/ (grammars + adapters)
  oeuph/                    rewrite-salvo/
  third_party/              vendored deps
  tools/ccw-manifest
```

All third-party dependencies (S7, MPC, Tree-sitter, per-language grammars) are vendored into `third_party/` and MUST be pinned to exact upstream commits recorded in `third_party/VERSIONS.lock`. Upgrades are explicit commits touching the lock file.

## 9. Conformance Summary

An implementation conforms if: (1) its executor implements `GlueSTD.h` ABI v1; (2) its Kernels are engine-agnostic R7RS libraries exporting the §2.2 triple; (3) its manifests are generated and CI-checked; (4) its IR modules declare a profile and round-trip text↔memory; (5) its Oeuph honors budgets, determinism, and contains no non-equivalence rules.

---

Terminology fixes are folded in throughout ("vessel", "stereotype", "pipeline", "embeddable"), and the self-undercutting "Tilly is no different than other intermediate languages" claim is replaced by the precise statement in §5.4.

