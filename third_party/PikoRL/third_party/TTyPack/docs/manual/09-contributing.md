\page manual_contributing Building and Contributing

# Building and Contributing

## Source of truth

Before changing a header, read its entry in the corresponding manifest:

- `manifests/TTyTk-Manifest.yaml`
- `manifests/TTyUtils-Manifest.yaml`

The entry defines the public symbols, allowed dependencies, and intended
behavior. Preserve the fixed spellings `Grampheme` and `PsuedoTerm`.

## Implementation order

Implement in dependency order. TTyTk progresses from `Core` through text,
parsing, state, rendering, OS integration, and databases. TTyUtils progresses
from the text engine through the event loop, UI, widgets, viewers, and
transports. The chapter \ref manual_api_map groups those phases.

## Standalone header checks

Every header must compile on its own:

```sh
CXX=${CXX:-g++}
for header in include/TTyTk/*.hpp include/TTyUtils/*.hpp include/*.hpp; do
    "$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude \
        -fsyntax-only -x c++ "$header" || exit 1
done
```

This catches accidental transitive dependencies and missing standard headers.

## Tests

Tests are plain C++ programs with `assert`, one file per header where
practical. Parser and database behavior should have tests, especially for
incremental input, malformed data, and compatibility formats.

Build and run them through CMake:

```sh
cmake -S . -B build -DTTYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Do not add a test framework or a third-party dependency.

## Protocol references

Escape handling must follow the reference documents in `.agents/documents/`.
Use the xterm control-sequence document for xterm behavior, ECMA-48 for
general control semantics, and the terminal-specific documents only for their
extensions. Add a short source comment when a parser rule is non-obvious.

## Documentation changes

Keep the manual conceptual and the header comments API-specific. If a new
module is added, update its manifest entry, API-map chapter, umbrella header,
and the relevant manual chapter. Run the Doxygen target when possible.
