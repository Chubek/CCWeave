# Decisions

## 2026-08-16 — Capability kernels preserve IR without required extensions

The Core Accessor Set exposes instruction navigation and structural edits, but
does not define portable accessors for control-flow edges, analysis-fact
storage, target machine nodes, ABI metadata, or debug emission.  The latter
30 manifested kernels therefore validate their capability and options alist,
then return the unchanged IR handle.  Their advertised contracts become
behavioral when the corresponding profile-specific host accessors are
standardized.
