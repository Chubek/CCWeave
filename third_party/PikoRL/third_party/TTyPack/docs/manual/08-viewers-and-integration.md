\page manual_integration Viewers and Integration

# Viewers and Integration

## Viewers

TTyUtils includes focused viewers that share the widget, search, wrapping, and
scrolling primitives:

- `Pager` streams files and pipes, supports search, follow mode, marks, and
  horizontal scrolling;
- `HexView` presents large binary files with linked hex/ASCII panes,
  annotations, and inspection;
- `MarkdownView` renders CommonMark-style content, code highlighting, and
  terminal hyperlinks;
- `LogView` follows rotating files with filters, rules, and pause-on-scroll;
- `ImageView` negotiates Kitty, iTerm2, and Sixel backends with a cell-based
  fallback.

Use a viewer as a model-driven component. A pager should not require the
whole input stream in memory, and a log view should not block the event loop
while waiting for new data.

## Embedded terminal applications

`Terminal` combines a PTY, VT parser, cell buffer, scrollback, resize
propagation, and OSC hooks. A typical integration handles:

1. child process creation;
2. byte delivery into the emulator;
3. input events back to the child;
4. damage-driven rendering;
5. title, working-directory, clipboard, and hyperlink hooks.

Use an explicit clipboard policy for OSC 52 and an explicit URL/path policy
for click-to-open behavior.

## Configuration and notifications

`Config` provides layered configuration, typed access, validation, XDG paths,
watching, and comment-preserving write-back. `Clipboard` and `Notify` build
system integration on top of this policy layer and TTyTk primitives.

Load configuration before constructing widgets so themes, key maps, filters,
and remote policies are available during initialization. Apply live changes
through the event loop rather than mutating widget state from a file watcher
thread.

## Remote sessions

`SSH`, `Telnet`, `Serial`, `P2P`, `Roam`, `Multiplex`, and `SessionShare`
compose into increasingly complete remote workflows. Start with `NetLink`
and one protocol; add PTY and terminal emulation only when the remote side
needs an interactive screen.

Remote code should surface authentication failures, reconnect state, and
permission changes in the UI. Do not silently grant clipboard, write, or
session-sharing access.
