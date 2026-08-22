# TTyPack

TTyPack is a pair of dependency-free, header-only C++20 libraries for
terminal software:

- **TTyTk** provides the synchronous terminal substrate: UTF-8 and grapheme
  handling, VT parsing, cell buffers, rendering, terminal capabilities,
  PTYs, and transport primitives.
- **TTyUtils** builds on TTyTk with a text engine, coroutine-aware application
  framework, widgets, viewers, configuration, and remote-session helpers.

The libraries are intentionally layered. TTyTk never includes TTyUtils;
applications can use the lower layer directly when they need a small,
poll-driven terminal component.

## Start here

- \ref manual_getting_started — add TTyPack to a CMake project.
- \ref manual_architecture — understand the layering and data flow.
- \ref manual_terminal — parse input and render terminal cells.
- \ref manual_text — build editor and text-processing features.
- \ref manual_ui — compose applications and widgets.
- \ref manual_integration — use viewers, shells, and transports.
- \ref manual_api_map — find a header by capability.
- \ref manual_contributing — build, test, document, and extend TTyPack.

## Package status

TTyPack is organized as a manifest-driven implementation. The manifests in
`manifests/` are the API source of truth: every public symbol, dependency, and
header path should be checked against them before changing an implementation.
Some modules are deliberately experimental, especially networking, image
backends, and peer-to-peer session features.

## Design principles

1. Keep TTyTk synchronous and non-blocking; callers own polling and scheduling.
2. Keep TTyUtils policy-rich, composable, and coroutine-friendly.
3. Preserve UTF-8 internally and use grapheme/display-cell semantics at the
   terminal boundary.
4. Prefer value-or-error results for runtime failures; do not throw for I/O or
   malformed input.
5. Keep platform-specific code explicit and POSIX-first.
