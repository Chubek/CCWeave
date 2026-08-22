\page manual_text Text Engine

# Text Engine

The TTyUtils text engine is designed for editors, command lines, viewers, and
background analysis. It stores UTF-8 but exposes logical positions rather than
raw byte offsets.

## Buffer and cursor

`textbuf::Buffer` uses a piece-table model with a line index. It supports
cheap immutable snapshots for workers:

```cpp
ttyutils::textbuf::Buffer buffer{"first line\nsecond line\n"};
buffer.insert({1, 0}, "new ");
auto snapshot = buffer.snapshot();
```

`textcur::Cursor`, `Selection`, and `CursorSet` add semantic movement,
multi-cursor editing, sticky columns, and edit remapping. Use grapheme-aware
motions for user-visible cursor movement.

## Undo and history

`undo::History` is an undo tree rather than a destructive stack. Group edits
with `undo::Transaction`, expose meaningful `Checkpoint` names, and preserve
cursor state with each edit. `HistoryFile` provides persistent command or
document history through the configuration layer.

## Search, replacement, and diff

`search::Query` supports literal, regular-expression, and fuzzy matching.
Use incremental search in interactive fields and `Replacer` when edits need
per-match confirmation.

`diff` supplies line and word changes, unified patches, and three-way merge
conflicts. A UI can render `Hunk` and `Conflict` objects without reparsing a
patch string.

## Wrapping and highlighting

`wrap::soft_wrap` produces a `WrapMap` between visual rows and buffer
positions. Use it for viewports; use `wrap::reflow` only when the application
intends to change document text.

`SyntaxGrammar` loads grammar definitions and detects languages.
`SyntaxHighlight` caches per-line state so edits invalidate only the affected
region. `Theme` maps scopes to terminal styles.

## Completion and readline

`Completion` consumes search and context providers to produce candidates.
`Readline` combines a text buffer, completion, history, key maps, and a
terminal input source into a command-line editor. For a single-line widget,
TTyUtils `TextInput` is usually the better entry point; use `Readline` when
you need shell-style editing semantics.
