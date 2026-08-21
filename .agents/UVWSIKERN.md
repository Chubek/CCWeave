# UVWASIKERN.md — WASI Kernel Tier from `third_party/uvwasi`

**Status:** Proposed · **Manifest target:** `Kernel.yaml` only (per D-0050) · **Decision records:** D-0057 – D-0060

## 1. Purpose and scope

`third_party/uvwasi` is a C implementation of the `wasi_snapshot_preview1` ABI backed by libuv. CCWeave derives two things from it, and deliberately nothing more:

1. **The ABI contract.** uvwasi's headers are the pinned, authoritative definition of the preview1 surface — syscall names, argument layouts, `errno` values, and the rights model. Kernels treat this as a vendored spec, the same way the SIMD tier treats `third_party/simd-everywhere` as the authoritative portable-op surface.
2. **A host runtime for native targets.** On `x86_64`/`aarch64`/`riscv64`, the same IR-level WASI ops lower to direct `uvwasi_*` libcalls, so one program compiles for `wasm32` (imports) or native (uvwasi-backed) from identical mid-level IR.

Out of scope: a WASM *interpreter*, JIT hosting of foreign modules, and preview2/component-model — the tier is preview1-only until a follow-up decision record says otherwise.

## 2. Source layout
```
third_party/uvwasi/            # vendored, pinned (see §6)
stdlib-salvo/libc/wasi/
  GlueWASI.h                   # portable WASI-op surface; mirrors GlueSTD.h's role
  abi-preview1.h               # generated from uvwasi headers; the frozen op table
  io.c                         # fd_read/fd_write/fd_seek/fd_close wrappers
  fs.c                         # path_open, fd_prestat_*, path_filestat_*
  env.c                        # args_get, environ_get, clock_time_get, random_get
  host-uvwasi.c                # native-target bodies: thin calls into uvwasi_*
```

`GlueWASI.h` is the only surface kernels and `stdlib-salvo` code may name; neither raw uvwasi symbols nor raw import strings appear in IR. Every op is a node `(wasi-op $op $args)` where `$op` is drawn from the closed table in `abi-preview1.h`.

## 3. `Kernel.yaml` — three new kernels (house style, v0.1.0)

Analysis kernel, in the analysis cluster:

```yaml
  - path: kernels/wasi-abi-analyze.scm
    library: (ccweave kernel wasi-abi-analyze)
    name: wasi-abi-analyze
    version: 0.1.0
    description: Publishes facts classifying io, filesystem, clock, and entropy operations against the wasi_snapshot_preview1 op table, including required rights per fd.
    capabilities:
      - analysis.wasi-abi
```

Lowering kernel, next to the other `lower.*` kernels, following the confirmed `lower.atomics`/`lower.atomics-libcall` dual-strategy convention (Capabilities.yaml lines 68 ff.):

```yaml
  - path: kernels/wasi-lower.scm
    library: (ccweave kernel wasi-lower)
    name: wasi-lower
    version: 0.1.0
    description: Lowers wasi-op nodes to wasm32 preview1 imports or to uvwasi host libcalls, selected at compile time by target.
    capabilities:
      - lower.wasi
      - lower.wasi-import
      - lower.wasi-uvwasi
```

Rights/sandbox kernel, alongside the existing sanitizer:

```yaml
  - path: kernels/wasi-rights-check.scm
    library: (ccweave kernel wasi-rights-check)
    name: wasi-rights-check
    version: 0.1.0
    description: Inserts compile-time-provable rights assertions and linear-memory bounds checks ahead of wasi-op lowering; emits negative facts with reason codes for unprovable sites.
    capabilities:
      - sanitize.wasi-rights
```

`codegen-wasm32` (lines 119–125) is **not** modified; the WASI tier runs entirely before instruction selection, and the existing scalar wasm32 backend consumes the already-lowered import calls.

## 4. `Capabilities.yaml` insertions

- `analysis.*` group (starts line 18) — append after `analysis.use-def-chain` (alphabetical):

  ```yaml
    analysis.wasi-abi:
      - kernels/wasi-abi-analyze.scm
  ```

- `lower.*` group (starts line 68) — append after `lower.stack-allocation`:

  ```yaml
    lower.wasi:
      - kernels/wasi-lower.scm
    lower.wasi-import:
      - kernels/wasi-lower.scm
    lower.wasi-uvwasi:
      - kernels/wasi-lower.scm
  ```

- `sanitize.*` group (line 155) — after `sanitize.ubsan`:

  ```yaml
    sanitize.wasi-rights:
      - kernels/wasi-rights-check.scm
  ```

## 5. `Stdrewrite.yaml` — three rules, confirmed field set

The file's 41 existing rules all use exactly `name`/`description`/`trigger`/`target`/`gating` with bare s-expressions; these follow suit.

```yaml
- name: wasi.io-
to-abi-op
  description: "Route high-level stdlib-salvo io ops to formal preview1 abi-op nodes"
  trigger: (stdlib-io-call $op $args)
  target: (wasi-op $op $args)
  gating: true

- name: wasi.host-to-uvwasi
  description: "Route wasi-op nodes to host libcalls when target is native and uvwasi is active"
  trigger: (wasi-op $op $args)
  target: (lower-wasi-uvwasi $op $args)
  gating: true

- name: wasi.target-to-import
  description: "Route wasi-op nodes to preview1 target imports when target is wasm32"
  trigger: (wasi-op $op $args)
  target: (lower-wasi-import $op $args)
  gating: true
```
The second and third rules are gated by target flags; they are mutually exclusive.

## 6. Decision records

- **D-0057** — The WASI tier target is exclusively `Kernel.yaml` and `wasm_snapshot_preview1`.
- **D-0058** — The `third_party/uvwasi` integration is frozen to revision `21c1724` (or similar pinned tag); all WASI syscall declarations must be generated from its headers.
- **D-0059** — Every compiler run targeting WASM/WASI compiles a provenance `.note.ccw` section, recording `wasi-revision: preview1` and the specific host `uvwasi` version hash, ensuring byte-for-byte build reproducibility.
- **D-0060** — If compile-time analysis in `wasi-rights-check` detects a static violation of rights (e.g. attempting to open an absolute path outside the sandbox configuration), compilation halts with an error; it cannot be deferred to runtime.

---

### Verification and CI Double-Run Gate

All WASI-tier implementations are gated by the same double-run CI pipeline as the SIMD tier: compiling the standard library (`stdlib-salvo/libc/wasi/`) twice with different build-paths and verifying byte-identical binary outputs. The host native backend targets (`x86_64`, `aarch64`, `riscv64`) are validated against their target-specific `codegen-*` kernels, ensuring identical ABI layouts between host-fallback and WASM-native execution.
