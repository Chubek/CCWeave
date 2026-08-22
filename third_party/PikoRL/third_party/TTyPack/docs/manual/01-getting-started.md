\page manual_getting_started Getting Started

# Getting Started

## Requirements

TTyPack requires:

- a C++20 compiler;
- CMake 3.20 or newer for the supplied build;
- a POSIX environment (Linux or macOS) for the OS-facing headers;
- Doxygen 1.9 or newer only when generating API documentation.

The libraries have no third-party link dependencies. TTyTk headers may be
included independently; TTyUtils headers may include both bundles.

## Add the package with CMake

From a checkout:

```cmake
add_subdirectory(ttypack)

add_executable(my_terminal main.cpp)
target_link_libraries(my_terminal PRIVATE TTyPack::TTyUtils)
target_compile_features(my_terminal PRIVATE cxx_std_20)
```

Use `TTyPack::TTyTk` when an application only needs the low-level terminal
layer:

```cmake
target_link_libraries(my_terminal PRIVATE TTyPack::TTyTk)
```

The targets are interface libraries. Linking them supplies the include path
and the C++20 requirement; there is no runtime library to deploy.

## Include a bundle

Umbrella headers are convenient for applications:

```cpp
#include <TTyTk.hpp>
#include <TTyUtils.hpp>
```

For faster rebuilds and clearer dependencies, include individual headers:

```cpp
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
```

The spelling of `TTyTk-Grampheme.hpp` and `TTyTk-PsuedoTerm.hpp` is
intentional and is part of the public package layout.

## Configure and test

```sh
cmake -S . -B build -DTTYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Build documentation when Doxygen is installed:

```sh
cmake -S . -B build -DTTYPACK_BUILD_DOCS=ON
cmake --build build --target ttypack-docs
```

The generated HTML is written below `build/docs/generated/html`.

## First result

The foundation header exposes the common result and byte types:

```cpp
#include <TTyTk/TTyTk-Core.hpp>

int main() {
    constexpr auto release = ttytk::version();
    ttytk::Result<int> value{42};
    return value && release[0] >= 0 ? 0 : 1;
}
```

Use the manifest and the API pages to move from this foundation to parsing,
rendering, or the higher-level application toolkit.
