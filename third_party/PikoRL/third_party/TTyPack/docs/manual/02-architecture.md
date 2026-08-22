\page manual_architecture Architecture

# Architecture

## Two layers

TTyPack separates mechanism from application policy:

```text
application
    │
TTyUtils: text engine, event loop, widgets, viewers, remote sessions
    │
TTyTk: encoding, VT protocols, cells, rendering, PTY, POSIX primitives
    │
standard library + POSIX
```

The boundary is strict. TTyTk must never include TTyUtils, while TTyUtils may
depend on either bundle. This makes it possible to use TTyTk in a small
terminal filter or embed TTyUtils in a complete interactive application.

## Data flow

A typical full-screen application has four paths:

1. **Input:** bytes arrive from a file descriptor, PTY, serial port, or
   network link.
2. **Decode:** `Automaton` recognizes control sequences; `InputProto`,
   `KeyListener`, and `MouseListener` turn terminal input into events.
3. **State:** a `CellBuffer`, text buffer, widget tree, or viewer stores the
   current logical state.
4. **Render:** damage tracking and `Renderer` produce the smallest output
   stream for the physical terminal.

TTyUtils adds an event loop around these paths. TTyTk itself remains
synchronous and poll-driven.

## Dependency discipline

The manifests are normative. A header may include its listed dependencies and
the C++ or POSIX standard library, but should not acquire an incidental
dependency merely because another utility is convenient. This keeps standalone
header compilation meaningful and prevents the low layer from growing
application policy.

## Text boundary

TTyPack uses UTF-8 for internal text. Do not use bytes as a proxy for columns:

- `utf8` decodes and validates code points;
- `grapheme` groups user-perceived characters;
- `charwidth` maps characters to terminal cells;
- `CellBuffer` stores the resulting display representation;
- `TextBuffer` and `TextCursor` preserve logical text positions.

This separation is essential for combining marks, emoji sequences, East Asian
wide characters, and bidirectional text.

## Choosing a layer

Choose TTyTk for a parser, terminal emulator core, renderer, PTY wrapper,
recording tool, or low-level transport. Choose TTyUtils when you need a
document model, an event loop, widgets, a pager, an embedded terminal, or
remote-session workflows. It is normal for a TTyUtils application to include
one or two TTyTk headers directly for terminal-specific behavior.
