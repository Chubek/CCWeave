\page manual_api_map API Map

# API Map

This map follows the manifests and is intended as a starting point for the
generated file and namespace reference. Names below are module names; the
header path is the stable include path.

## TTyTk

### Foundation and text

| Module | Header |
| --- | --- |
| Core | `TTyTk/TTyTk-Core.hpp` |
| Encoding | `TTyTk/TTyTk-Encoding.hpp` |
| CharWidth | `TTyTk/TTyTk-CharWidth.hpp` |
| Grampheme | `TTyTk/TTyTk-Grampheme.hpp` |
| BiDi | `TTyTk/TTyTk-BiDi.hpp` |
| TextLayout | `TTyTk/TTyTk-TextLayout.hpp` |
| ColorTools | `TTyTk/TTyTk-ColorTools.hpp` |
| ThemeTools | `TTyTk/TTyTk-ThemeTools.hpp` |

### Parsing, input, and screen state

| Module | Header |
| --- | --- |
| EscapeCodes | `TTyTk/TTyTk-EscapeCodes.hpp` |
| Automaton | `TTyTk/TTyTk-Automaton.hpp` |
| OSCTools | `TTyTk/TTyTk-OSCTools.hpp` |
| InputProto | `TTyTk/TTyTk-InputProto.hpp` |
| KeyListener | `TTyTk/TTyTk-KeyListener.hpp` |
| MouseListener | `TTyTk/TTyTk-MouseListener.hpp` |
| EventListener | `TTyTk/TTyTk-EventListener.hpp` |
| CellBuffer | `TTyTk/TTyTk-CellBuffer.hpp` |
| Damage | `TTyTk/TTyTk-Damage.hpp` |
| Scrollback | `TTyTk/TTyTk-Scrollback.hpp` |
| Tabstops | `TTyTk/TTyTk-Tabstops.hpp` |
| Selection | `TTyTk/TTyTk-Selection.hpp` |
| Snapshot | `TTyTk/TTyTk-Snapshot.hpp` |
| Renderer | `TTyTk/TTyTk-Renderer.hpp` |

### Terminal and OS integration

| Module | Header |
| --- | --- |
| TermMode | `TTyTk/TTyTk-TermMode.hpp` |
| Winsize | `TTyTk/TTyTk-Winsize.hpp` |
| SignalTools | `TTyTk/TTyTk-SignalTools.hpp` |
| JobControl | `TTyTk/TTyTk-JobControl.hpp` |
| PsuedoTerm | `TTyTk/TTyTk-PsuedoTerm.hpp` |
| FlowControl | `TTyTk/TTyTk-FlowControl.hpp` |
| SerialPort | `TTyTk/TTyTk-SerialPort.hpp` |
| IPCTools | `TTyTk/TTyTk-IPCTools.hpp` |
| MuxTools | `TTyTk/TTyTk-MuxTools.hpp` |
| VirtTools | `TTyTk/TTyTk-VirtTools.hpp` |
| EmbedTools | `TTyTk/TTyTk-EmbedTools.hpp` |
| UITools | `TTyTk/TTyTk-UITools.hpp` |

### Databases and tools

| Module | Header |
| --- | --- |
| TermInfo | `TTyTk/TTyTk-TermInfo.hpp` |
| TermCap | `TTyTk/TTyTk-TermCap.hpp` |
| CapTools | `TTyTk/TTyTk-CapTools.hpp` |
| BufferTools | `TTyTk/TTyTk-BufferTools.hpp` |
| ClipboardTools | `TTyTk/TTyTk-ClipboardTools.hpp` |
| SearchTools | `TTyTk/TTyTk-SearchTools.hpp` |
| PromptTools | `TTyTk/TTyTk-PromptTools.hpp` |
| LineEdit | `TTyTk/TTyTk-LineEdit.hpp` |
| NotifyTools | `TTyTk/TTyTk-NotifyTools.hpp` |
| ImageProto | `TTyTk/TTyTk-ImageProto.hpp` |
| RecordTools | `TTyTk/TTyTk-RecordTools.hpp` |
| PuppetTools | `TTyTk/TTyTk-PuppetTools.hpp` |
| ServerNetUtils | `TTyTk/TTyTk-ServerNetUtils.hpp` |
| P2PNetUtils | `TTyTk/TTyTk-P2PNetUtils.hpp` |
| Fuzz | `TTyTk/TTyTk-Fuzz.hpp` |

## TTyUtils

### Text engine

`TextBuffer`, `TextCursor`, `UndoHistory`, `WordWrap`, `Search`, `Diff`,
`SyntaxGrammar`, `SyntaxHighlight`, `HistoryFile`, `Completion`, and
`Readline` live under `TTyUtils/` and form the editor and command-line
foundation.

### UI core and widgets

`EventLoop`, `Terminal`, `Theme`, `Widget`, `Layout`, `Focus`, and `App` are
the UI core. The widgets are `Panel`, `Menu`, `Button`, `TextInput`,
`TextArea`, `ListView`, `TableView`, `TreeView`, `ScrollBar`, `StatusBar`,
`TabBar`, `Dialog`, `FilePicker`, and `Progress`.

### Viewers

The viewer modules are `Pager`, `HexView`, `MarkdownView`, `LogView`, and
`ImageView`.

### Transports and integration

The transport modules are `NetLink`, `Telnet`, `SSH`, `Serial`, `Multiplex`,
`SessionShare`, `P2P`, and `Roam`. System integration is provided by `Shell`,
`Clipboard`, `Notify`, `Config`, and `KeyMap`.

## Public symbol groups

The manifest `exposes` lists the exact public surface for each module. The
most common entry points are:

- `ttytk::version`, `ttytk::Result`, `ttytk::Span`, and `ttytk::Bytes`;
- `ttytk::esc::*`, `ttytk::utf8::*`, `ttytk::charwidth::*`, and
  `ttytk::grapheme::*`;
- `ttytk::Automaton`, `ttytk::CellBuffer`, `ttytk::Renderer`, and `ttytk::PTY`;
- `ttyutils::textbuf::*`, `ttyutils::textcur::*`, `ttyutils::undo::*`,
  `ttyutils::search::*`, and `ttyutils::wrap::*`;
- `ttyutils::ui::*`, `ttyutils::term::*`, `ttyutils::net::*`,
  `ttyutils::shell::*`, and `ttyutils::config::*`.

For signatures, overloads, and exact types, use the generated Files,
Namespaces, and Classes pages rather than copying declarations into this
map.
