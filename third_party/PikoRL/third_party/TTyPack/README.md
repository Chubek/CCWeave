# TTyPack

TTyPack is a dependency-free, header-only C++20 toolkit for terminal
software. It is split into two deliberately layered libraries:

- **TTyTk** — terminal mechanics: UTF-8, grapheme width, VT/ANSI parsing,
  cell buffers, rendering, terminal capabilities, PTYs, and POSIX helpers.
- **TTyUtils** — application facilities: text buffers, editing, coroutines,
  widgets, viewers, configuration, shells, and remote transports.

TTyTk never depends on TTyUtils. Use the lower layer for small terminal
components; use TTyUtils for complete terminal applications.

## Quick start

```sh
cmake -S . -B build -DTTYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Add the checkout to another CMake project:

```cmake
add_subdirectory(ttypack)
target_link_libraries(my_app PRIVATE TTyPack::TTyUtils)
```

Or include a focused header:

```cpp
#include <TTyTk/TTyTk-Core.hpp>

int main() {
    constexpr auto version = ttytk::version();
    ttytk::Result<int> answer{42};
    return answer && version[0] >= 0 ? 0 : 1;
}
```

## Documentation

Build the Doxygen manual and API reference with:

```sh
cmake -S . -B build -DTTYPACK_BUILD_DOCS=ON
cmake --build build --target ttypack-docs
```

Read the source documentation in [`docs/frontpage.md`](docs/frontpage.md) or
start with the [manual](docs/manual/index.md).

## Project layout

```text
include/TTyTk/       low-level terminal headers
include/TTyUtils/    high-level application headers
tests/               plain assert-based test programs
manifests/            public API and dependency declarations
docs/                 Doxygen configuration and manual chapters
```

TTyPack is POSIX-first. Linux and macOS are the supported platform targets
for OS-facing headers.
