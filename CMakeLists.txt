# CCWeave — global build specification (spec §8).
cmake_minimum_required(VERSION 3.16)
project(ccweave C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra)

option(CCWEAVE_ENABLE_ASAN "Build with AddressSanitizer/LeakSanitizer" OFF)
option(CCWEAVE_ENABLE_TREESITTER "Build Swaff Tree-sitter frontends" ON)
option(CCWEAVE_BUILD_DOCS "Build Doxygen documentation" ON)

if(CCWEAVE_ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

set(CCWEAVE_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(CCWEAVE_SWAFF_GRAMMARS ${CMAKE_CURRENT_SOURCE_DIR}/swaff/grammars)

# --- vendored dependencies (pinned in third_party/VERSIONS.lock) ---
# Vendored sources are never edited, so they are compiled without our
# warning and sanitizer flags; sanitizers still cover all CCWeave code.
function(ccw_vendored_target target)
  target_compile_options(${target} PRIVATE -w)
  if(CCWEAVE_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fno-sanitize=undefined)
  endif()
endfunction()

add_library(ccw_mpc STATIC ${CCWEAVE_THIRD_PARTY}/mpc/mpc.c)
target_include_directories(ccw_mpc PUBLIC ${CCWEAVE_THIRD_PARTY}/mpc)
ccw_vendored_target(ccw_mpc)

add_library(ccw_s7 STATIC ${CCWEAVE_THIRD_PARTY}/s7/s7.c)
target_include_directories(ccw_s7 PUBLIC ${CCWEAVE_THIRD_PARTY}/s7)
# s7.c includes "mus-config.h" and uses struct timezone, so it needs the
# GNU dialect. Vendored source is never edited, so we adjust the target.
target_include_directories(ccw_s7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/executors/s7)
set_target_properties(ccw_s7 PROPERTIES C_EXTENSIONS ON)
target_compile_definitions(ccw_s7 PUBLIC WITH_C_LOADER=0 WITH_SYSTEM_EXTRAS=0)
ccw_vendored_target(ccw_s7)
find_library(CCW_M_LIB m)
if(CCW_M_LIB)
  target_link_libraries(ccw_s7 PUBLIC ${CCW_M_LIB})
endif()
find_package(Threads REQUIRED)
target_link_libraries(ccw_s7 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- vendored Tree-sitter runtime and C grammar ---------------------------
# Build the checked-in generated sources directly. In particular, do not
# add_subdirectory(swaff/grammars/tree-sitter-c): its upstream CMake project
# may regenerate parser.c with a host `tree-sitter` CLI, violating the
# repository's no-generation/no-fetch-at-build-time rule.
if(CCWEAVE_ENABLE_TREESITTER)
  add_library(ccw_tree_sitter STATIC
    ${CCWEAVE_THIRD_PARTY}/tree-sitter/lib/src/lib.c)
  target_include_directories(ccw_tree_sitter
    PUBLIC ${CCWEAVE_THIRD_PARTY}/tree-sitter/lib/include
    PRIVATE ${CCWEAVE_THIRD_PARTY}/tree-sitter/lib/src)
  target_compile_definitions(ccw_tree_sitter PRIVATE
    _POSIX_C_SOURCE=200112L
    _DEFAULT_SOURCE
    _DARWIN_C_SOURCE)
  set_target_properties(ccw_tree_sitter PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter)

  add_library(ccw_tree_sitter_c STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-c/src/parser.c)
  target_include_directories(ccw_tree_sitter_c
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-c/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-c/src)
  set_target_properties(ccw_tree_sitter_c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_c)

  # --- vendored Tree-sitter Lua grammar -------------------------------
  add_library(ccw_tree_sitter_lua STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-lua/src/parser.c
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-lua/src/scanner.c)
  target_include_directories(ccw_tree_sitter_lua
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-lua/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-lua/src)
  set_target_properties(ccw_tree_sitter_lua PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_lua)

  # --- vendored Tree-sitter OCaml grammar (three sub-languages) ----------
  # ocaml (implementation files: .ml)
  add_library(ccw_tree_sitter_ocaml STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/ocaml/src/parser.c
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/ocaml/src/scanner.c)
  target_include_directories(ccw_tree_sitter_ocaml
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/ocaml/src
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/common)
  set_target_properties(ccw_tree_sitter_ocaml PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_ocaml)

  # ocaml-interface (interface files: .mli)
  add_library(ccw_tree_sitter_ocaml_interface STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/interface/src/parser.c
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/interface/src/scanner.c)
  target_include_directories(ccw_tree_sitter_ocaml_interface
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/interface/src
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/common)
  set_target_properties(ccw_tree_sitter_ocaml_interface PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_ocaml_interface)

  # ocaml-type (type expressions)
  add_library(ccw_tree_sitter_ocaml_type STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/type/src/parser.c
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/type/src/scanner.c)
  target_include_directories(ccw_tree_sitter_ocaml_type
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/grammars/type/src
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-ocaml/common)
  set_target_properties(ccw_tree_sitter_ocaml_type PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_ocaml_type)
endif()

add_subdirectory(glue)
add_subdirectory(ir)
add_subdirectory(executors/s7)
add_subdirectory(oeuph)
add_subdirectory(sched)
add_subdirectory(kliche)
add_subdirectory(swaff)
add_subdirectory(compilers/cephyr)
add_subdirectory(docs)
add_subdirectory(tools/ccw-manifest)

enable_testing()
add_subdirectory(tests)
add_subdirectory(compilers/cephyr/tests)
