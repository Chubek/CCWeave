# TTyPack Guide

This guide is for contributors and maintainers working on the repository.
The user-facing overview is in `README.md`; the conceptual manual is under
`docs/manual/`.

## Repository contract

TTyPack is header-only and uses C++20. The manifests are the API source of
truth:

- `manifests/TTyTk-Manifest.yaml`
- `manifests/TTyUtils-Manifest.yaml`

Each entry specifies the header, exposed symbols, allowed dependencies, and
intended behavior. Keep the layering rule absolute: TTyTk may not include
TTyUtils.

The filenames `TTyTk-Grampheme.hpp` and `TTyTk-PsuedoTerm.hpp` are public
names and must not be corrected or renamed.

## Build

Configure a normal development build:

```sh
cmake -S . -B build -DTTYPACK_BUILD_TESTS=ON
cmake --build build
```

Enable documentation separately:

```sh
cmake -S . -B build-docs -DTTYPACK_BUILD_DOCS=ON
cmake --build build-docs --target ttypack-docs
```

The project exports two interface targets:

- `TTyPack::TTyTk`
- `TTyPack::TTyUtils`

The second links the first and supplies the include directory and C++20
feature requirement.

## Validation

Compile every header standalone with the same strict baseline used by the
project:

```sh
CXX=${CXX:-g++}
for header in include/TTyTk/*.hpp include/TTyUtils/*.hpp include/*.hpp; do
    echo "== $header"
    "$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude \
        -fsyntax-only -x c++ "$header" || exit 1
done
```

Run tests through CTest:

```sh
ctest --test-dir build --output-on-failure
```

Tests are plain `main()` programs using `assert`; do not introduce a test
framework or a third-party dependency.

## Implementation order

Work in dependency order:

1. TTyTk foundation: `Core`, encoding, width, graphemes, colors, rectangles.
2. TTyTk parsing: automaton, escape codes, OSC, input, listeners.
3. TTyTk state/render: cells, damage, scrollback, tabs, selection, snapshots,
   layout, BiDi, renderer, themes.
4. TTyTk OS: winsize, modes, signals, jobs, PTYs, flow control, serial, IPC.
5. TTyTk databases/tools: terminfo, termcap, capabilities, buffers,
   clipboard, search, prompts, images, recording, multiplexing, and tools.
6. TTyUtils text engine.
7. TTyUtils UI core.
8. TTyUtils widgets and viewers.
9. TTyUtils transports and integration.

Do not add a higher-level dependency to make an earlier phase easier.

## Parser work

Consult the authoritative files in `.agents/documents/` before changing
escape handling, terminal modes, terminfo/termcap parsing, Kitty protocols,
or terminal-specific behavior. Use xterm control sequences as the primary
reference where documents overlap. Add focused tests for split sequences,
malformed input, recovery, and compatibility behavior.

## Documentation work

When adding or changing a module:

1. update the manifest entry and exposed symbol list;
2. keep the header's dependencies within `depends_on` plus the standard
   library;
3. update the appropriate manual chapter and `docs/manual/10-api-map.md`;
4. update the umbrella header in alphabetical order;
5. add or update a matching `tests/<header-name>.test.cpp`;
6. run standalone header checks, tests, and Doxygen.

Keep API comments close to declarations and keep the manual focused on
concepts, workflows, invariants, and examples.
