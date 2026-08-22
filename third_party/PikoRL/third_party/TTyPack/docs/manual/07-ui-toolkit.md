\page manual_ui UI Toolkit

# UI Toolkit

## Application lifecycle

The core UI stack is:

```text
EventLoop → Terminal → Theme → Widget → Layout → Focus → App
```

`EventLoop` owns coroutine-aware waiting. `Terminal` owns the physical
terminal and connects input, resize, and rendering. `App` coordinates the
widget tree, focus, theme, and shutdown.

Widgets should render from state and emit intents through callbacks or
events. They should not reach directly into a PTY or global process state.

## Widget composition

`Widget` is the common drawing and event boundary. `Layout` positions children;
`Focus` manages traversal and focus scopes; `Theme` supplies semantic roles.
The basic composition vocabulary includes:

- `Panel` for bordered and padded containers;
- `Menu`, `Button`, `TextInput`, and `TextArea` for interaction;
- `ListView`, `TableView`, and `TreeView` for virtualized data;
- `ScrollBar`, `StatusBar`, and `TabBar` for navigation chrome;
- `Dialog`, `FilePicker`, and `Progress` for transient workflows.

Prefer model/delegate interfaces for large lists and trees. Keep expensive
work out of paint callbacks and update the model through the event loop.

## Focus and keyboard maps

Focus scopes make modal dialogs and nested editors predictable. `KeyMap`
parses human-readable chords, supports mode-scoped bindings, and reports
pending chord hints. Bind named actions rather than hard-coding behavior in
individual widgets; this makes user configuration and accessibility easier.

## Themes and terminal constraints

Theme roles should degrade from truecolor to indexed or basic ANSI colors.
Use semantic names such as `editor.selection` or `status.error`, not raw RGB
values scattered through widgets. Respect the terminal cell grid: layout
width is measured with TTyTk display-width utilities.

## Modal workflows

`Dialog` provides a focus scope and result-oriented interaction. `FilePicker`
builds on it for open, save, and directory selection. `Progress` supports
determinate and indeterminate work, including multiple concurrent jobs.

Coroutine-facing APIs should be awaited from the event loop; callback forms
remain useful for code that does not otherwise use coroutines.
