# salvo-libc — freestanding C standard library for Cephyr targets.
#
# Built by the host toolchain (Cephyr's own frontend does not yet cover
# the inline-asm syscall layer; see DECISIONS.md 2026-08-19). The
# archive and crt0 land in ${CMAKE_CURRENT_BINARY_DIR}/lib, and a
# build-tree Libc.yaml is configured beside them so
# CEPHYR_STDLIB_MANIFEST can point at a populated tree.

enable_language(ASM)

set(SALVO_LIBC_SOURCES
  src/string/mem.c
  src/string/str.c
  src/string/strtok.c
  src/string/strdup.c
  src/string/strcase.c
  src/ctype/ctype.c
  src/errno/errno.c
  src/stdlib/abs.c
  src/stdlib/conv.c
  src/stdlib/qsort.c
  src/stdlib/malloc.c
  src/stdlib/exit.c
  src/stdio/file.c
  src/stdio/printf.c
  src/unistd/unistd.c
  src/time/time.c
  src/assert/assert.c
  src/start/libc_start.c)

add_library(salvoc STATIC ${SALVO_LIBC_SOURCES})
target_include_directories(salvoc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(salvoc PRIVATE
  -ffreestanding -nostdinc -fno-builtin -fno-stack-protector)
# Freestanding objects must not be pulled into the host's sanitizer
# runtime when the global ASan option is on.
if(CCWEAVE_ENABLE_ASAN)
  target_compile_options(salvoc PRIVATE -fno-sanitize=all)
endif()
set_target_properties(salvoc PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

# --- crt0 for the host architecture --------------------------------------
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SALVO_HOST_ARCH)
if(SALVO_HOST_ARCH MATCHES "^(x86_64|amd64)$")
  set(SALVO_CRT0_SOURCE src/crt/crt0_x86_64.S)
elseif(SALVO_HOST_ARCH MATCHES "^(aarch64|arm64)$")
  set(SALVO_CRT0_SOURCE src/crt/crt0_aarch64.S)
elseif(SALVO_HOST_ARCH MATCHES "^riscv64")
  set(SALVO_CRT0_SOURCE src/crt/crt0_riscv64.S)
else()
  set(SALVO_CRT0_SOURCE "")
  message(STATUS "salvo-libc: no crt0 for host '${CMAKE_SYSTEM_PROCESSOR}'; start files skipped")
endif()

if(SALVO_CRT0_SOURCE)
  add_library(salvo_crt0 OBJECT ${SALVO_CRT0_SOURCE})
  add_custom_command(TARGET salvo_crt0 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_CURRENT_BINARY_DIR}/lib
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_OBJECTS:salvo_crt0>
            ${CMAKE_CURRENT_BINARY_DIR}/lib/crt0.o)
endif()

# Build-tree manifest with resolved include paths; point
# CEPHYR_STDLIB_MANIFEST here to consume the freshly built library.
configure_file(Libc.yaml.in ${CMAKE_CURRENT_BINARY_DIR}/Libc.yaml @ONLY)

# --- freestanding self-test ----------------------------------------------
# Links only crt0 + salvoc with -nostdlib, so the salvo definitions —
# not the host libc's — are what actually run.
if(SALVO_CRT0_SOURCE)
  add_executable(salvo_libc_selftest tests/selftest.c ${SALVO_CRT0_SOURCE})
  target_include_directories(salvo_libc_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(salvo_libc_selftest PRIVATE
    -ffreestanding -nostdinc -fno-builtin -fno-stack-protector)
  target_link_libraries(salvo_libc_selftest PRIVATE salvoc)
  target_link_options(salvo_libc_selftest PRIVATE -nostdlib -static)
  if(CCWEAVE_ENABLE_ASAN)
    target_compile_options(salvo_libc_selftest PRIVATE -fno-sanitize=all)
    target_link_options(salvo_libc_selftest PRIVATE -fno-sanitize=all)
  endif()
  add_test(NAME salvo_libc_selftest COMMAND salvo_libc_selftest)
endif()
