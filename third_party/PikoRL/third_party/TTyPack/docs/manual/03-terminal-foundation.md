\page manual_terminal Terminal Foundation

# Terminal Foundation

## Encoding and display width

Decode input at the boundary and keep the decoded representation as long as
possible:

```cpp
auto codepoints = ttytk::utf8::decode(bytes);
if (!ttytk::utf8::validate(bytes)) {
    // Report or recover from malformed input according to application policy.
}
```

Use `grapheme::segment` for cursor movement and `charwidth::of` for layout.
A grapheme can contain multiple code points and can occupy zero, one, or two
terminal cells.

## Escape sequences

`TTyTk-EscapeCodes.hpp` contains builders for CSI, OSC, DCS, SGR, cursor
movement, and screen clearing. Keep sequence construction in this layer so
callers do not concatenate control bytes inconsistently.

`TTyTk-Automaton.hpp` consumes raw bytes and dispatches printable text,
control characters, CSI, OSC, and DCS events. Feed it incrementally: terminal
input can split any sequence across reads.

```cpp
ttytk::Automaton parser;
parser.on_print = [&](std::u32string_view text) {
    // Add printable graphemes to the virtual screen.
};
parser.on_csi = [&](const auto& sequence) {
    // Apply a CSI operation to terminal state.
};
parser.feed(data.data(), data.size());
```

Reset parser state when abandoning a stream. Do not assume a read boundary
matches a protocol boundary.

## Cell buffers

`CellBuffer` is the low-level screen model. A cell contains a grapheme,
attributes, and colors; wide graphemes occupy a lead cell plus a continuation
cell. Resize and scroll operations must preserve that invariant.

Use:

- `RectOps` for rectangular copy, fill, erase, and checksum operations;
- `Scrollback` for lines evicted from the visible grid;
- `Tabstops` for HTS, TBC, CHT, and CBT behavior;
- `Selection` for click-drag, word, line, and block selections;
- `TextLayout` for width-aware wrapping and truncation;
- `BiDi` when logical text must be reordered for visual display.

## Color and themes

`ColorTools` handles RGB values, palette quantization, parsing, distance, and
luminance. `ThemeTools` adds named schemes and terminal palette application.
Keep colors semantic at the TTyUtils theme boundary, and quantize only when
the target terminal requires it.

## Rendering

A render pass compares the desired `CellBuffer` with the renderer's cached
terminal state. `Damage` tells the renderer where changes are possible;
`Renderer` emits cursor movement, mode changes, SGR, and text updates.

The normal frame sequence is:

1. update logical state;
2. mark changed cells or regions;
3. render the damaged regions;
4. flush the output transport;
5. clear the damage set.

`Snapshot` and `BufferTools` are useful for golden tests, diagnostics, and
debug output without coupling tests to terminal escape formatting.
