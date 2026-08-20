# Build Integration and Header-Only Usage {#manual_02_build}

## Header-Only Model

Every ExolangTk module is a single `.h` file. You include the header, define
the implementation guard in exactly one translation unit, and compile with
any C99 toolchain. There are no libraries to link, no build-system plugins to
install, and no generated files.

## Basic Usage

The simplest program using InteropTk:

~~~c
#define ITK_PLATFORM_IMPLEMENTATION
#include "InteropTk/itk_platform.h"
#include <stdio.h>

int main(void) {
    itk_target_info info;
    itk_target_query(&info);
    printf("OS: %d  Arch: %d  Pointer bits: %u\n",
           (int)info.os, (int)info.arch, info.pointer_bits);
    return 0;
}
~~~

Compile:

~~~
cc -std=c99 -Iinclude -o example example.c
~~~

## Implementation Guards

Each module uses an implementation guard macro. You define it before the
first include of that module in exactly one `.c` file:

| Module | Guard Macro |
|--------|-------------|
| `itk_platform.h` | `ITK_PLATFORM_IMPLEMENTATION` |
| `itk_ctypes.h` | `ITK_CTYPES_IMPLEMENTATION` |
| `itk_layout.h` | `ITK_LAYOUT_IMPLEMENTATION` |
| `itk_callconv.h` | `ITK_CALLCONV_IMPLEMENTATION` |
| `itk_mangle.h` | `ITK_MANGLE_IMPLEMENTATION` |
| `itk_cdecl.h` | `ITK_CDECL_IMPLEMENTATION` |
| `itk_marshal.h` | `ITK_MARSHAL_IMPLEMENTATION` |
| `itk_cstring.h` | `ITK_CSTRING_IMPLEMENTATION` |
| `itk_error.h` | `ITK_ERROR_IMPLEMENTATION` |
| `itk_alloc.h` | `ITK_ALLOC_IMPLEMENTATION` |
| `itk_export.h` | `ITK_EXPORT_IMPLEMENTATION` |
| `ffi_loader.h` | `FFI_LOADER_IMPLEMENTATION` |
| `ffi_cif.h` | `FFI_CIF_IMPLEMENTATION` |
| `ffi_frame.h` | `FFI_FRAME_IMPLEMENTATION` |
| `ffi_call.h` | `FFI_CALL_IMPLEMENTATION` |
| `ffi_trampoline.h` | `FFI_TRAMPOLINE_IMPLEMENTATION` |
| `ffi_closure.h` | `FFI_CLOSURE_IMPLEMENTATION` |
| `ffi_library.h` | `FFI_LIBRARY_IMPLEMENTATION` |
| `dtk_sym.h` | `DTK_SYM_IMPLEMENTATION` |
| `dtk_unwind.h` | `DTK_UNWIND_IMPLEMENTATION` |
| `dtk_stack.h` | `DTK_STACK_IMPLEMENTATION` |
| `dtk_breakpoint.h` | `DTK_BREAKPOINT_IMPLEMENTATION` |
| `etk_dynload.h` | `ETK_DYNLOAD_IMPLEMENTATION` |
| `etk_version.h` | `ETK_VERSION_IMPLEMENTATION` |
| `etk_registry.h` | `ETK_REGISTRY_IMPLEMENTATION` |
| `etk_api.h` | `ETK_API_IMPLEMENTATION` |

Only one translation unit in a program may define a given guard. Defining it
in multiple `.c` files causes ODR violations.

## Convenience Umbrella Headers

Each subsystem provides an umbrella header that includes every module:

| Umbrella | Includes |
|----------|----------|
| `InteropTk.h` | All `include/InteropTk/itk_*.h` |
| `FFItk.h` | All `include/FFItk/ffi_*.h` |
| `DebugTk.h` | All `include/DebugTk/dtk_*.h` |
| `ExtensionTk.h` | All `include/ExtensionTk/etk_*.h` |

You can define multiple implementation guards in a single file:

~~~c
#define ITK_PLATFORM_IMPLEMENTATION
#define ITK_CTYPES_IMPLEMENTATION
#define ITK_LAYOUT_IMPLEMENTATION
#include "InteropTk.h"
~~~

## Function Qualifier Overrides

By default, `ITK_DEF`, `FFI_DEF`, `DTK_DEF`, and `ETK_DEF` expand to
`static`. If you prefer a dedicated implementation translation unit, define
the qualifier before the first include:

~~~c
#define ITK_DEF extern
#define ITK_PLATFORM_IMPLEMENTATION
#include "InteropTk/itk_platform.h"
~~~

All translation units in the program must use the same definition.

## CMake Integration

ExolangTk provides a CMake interface target:

~~~cmake
add_subdirectory(path/to/ExolangTk)
target_link_libraries(myapp PRIVATE ExolangTk)
~~~

This adds the include directory and sets the C99 standard. The
`EXOLANGTK_BUILD_TESTS` and `EXOLANGTK_BUILD_DOCS` options control smoke
tests and Doxygen documentation respectively:

~~~
cmake -S . -B build -DEXOLANGTK_BUILD_DOCS=ON -DEXOLANGTK_BUILD_TESTS=ON
cmake --build build
~~~

## Compiler Requirements

- **C99 compiler** with `<stdint.h>` and `<stddef.h>`.
- No `<stdbool.h>` required; each subsystem defines its own boolean type.
- No VLAs, no compiler extensions.
- Tested with GCC, Clang, and MSVC.

## Include Path

Set `-I` to the `include/` directory of the repository. All headers are
referenced relative to that root:

~~~c
#include "InteropTk/itk_platform.h"
#include "FFItk/ffi_loader.h"
#include "DebugTk/dtk_sym.h"
#include "ExtensionTk/etk_dynload.h"
~~~
