# CCWeave — global build specification (spec §8).
cmake_minimum_required(VERSION 3.16)
project(ccweave C CXX)

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

# --- vendored ISL ---------------------------------------------------------
find_path(CCWEAVE_GMP_INCLUDE_DIR gmp.h REQUIRED)
find_library(CCWEAVE_GMP_LIBRARY gmp REQUIRED)
set(CCWEAVE_ISL_DIR ${CCWEAVE_THIRD_PARTY}/isl)
set(CCWEAVE_ISL_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/isl-config)
set(CCWEAVE_ISL_SOURCES
  ${CCWEAVE_ISL_DIR}/isl_gmp.c
  ${CCWEAVE_ISL_DIR}/isl_aff.c ${CCWEAVE_ISL_DIR}/isl_aff_map.c
  ${CCWEAVE_ISL_DIR}/isl_affine_hull.c ${CCWEAVE_ISL_DIR}/isl_arg.c
  ${CCWEAVE_ISL_DIR}/isl_ast.c ${CCWEAVE_ISL_DIR}/isl_ast_build.c
  ${CCWEAVE_ISL_DIR}/isl_ast_build_expr.c ${CCWEAVE_ISL_DIR}/isl_ast_codegen.c
  ${CCWEAVE_ISL_DIR}/isl_ast_graft.c ${CCWEAVE_ISL_DIR}/basis_reduction_tab.c
  ${CCWEAVE_ISL_DIR}/isl_bernstein.c ${CCWEAVE_ISL_DIR}/isl_blk.c
  ${CCWEAVE_ISL_DIR}/isl_bound.c ${CCWEAVE_ISL_DIR}/isl_box.c
  ${CCWEAVE_ISL_DIR}/isl_coalesce.c ${CCWEAVE_ISL_DIR}/isl_constraint.c
  ${CCWEAVE_ISL_DIR}/isl_convex_hull.c ${CCWEAVE_ISL_DIR}/isl_ctx.c
  ${CCWEAVE_ISL_DIR}/isl_deprecated.c ${CCWEAVE_ISL_DIR}/isl_dim_map.c
  ${CCWEAVE_ISL_DIR}/isl_equalities.c ${CCWEAVE_ISL_DIR}/isl_factorization.c
  ${CCWEAVE_ISL_DIR}/isl_farkas.c ${CCWEAVE_ISL_DIR}/isl_ffs.c
  ${CCWEAVE_ISL_DIR}/isl_flow.c ${CCWEAVE_ISL_DIR}/isl_fold.c
  ${CCWEAVE_ISL_DIR}/isl_hash.c ${CCWEAVE_ISL_DIR}/isl_id_to_ast_expr.c
  ${CCWEAVE_ISL_DIR}/isl_id_to_id.c ${CCWEAVE_ISL_DIR}/isl_id_to_pw_aff.c
  ${CCWEAVE_ISL_DIR}/isl_ilp.c ${CCWEAVE_ISL_DIR}/isl_input.c
  ${CCWEAVE_ISL_DIR}/isl_local.c ${CCWEAVE_ISL_DIR}/isl_local_space.c
  ${CCWEAVE_ISL_DIR}/isl_lp.c ${CCWEAVE_ISL_DIR}/isl_map.c
  ${CCWEAVE_ISL_DIR}/isl_map_list.c ${CCWEAVE_ISL_DIR}/isl_map_simplify.c
  ${CCWEAVE_ISL_DIR}/isl_map_subtract.c ${CCWEAVE_ISL_DIR}/isl_map_to_basic_set.c
  ${CCWEAVE_ISL_DIR}/isl_mat.c ${CCWEAVE_ISL_DIR}/isl_morph.c
  ${CCWEAVE_ISL_DIR}/isl_id.c ${CCWEAVE_ISL_DIR}/isl_obj.c
  ${CCWEAVE_ISL_DIR}/isl_options.c ${CCWEAVE_ISL_DIR}/isl_output.c
  ${CCWEAVE_ISL_DIR}/isl_point.c ${CCWEAVE_ISL_DIR}/isl_polynomial.c
  ${CCWEAVE_ISL_DIR}/isl_printer.c ${CCWEAVE_ISL_DIR}/print.c
  ${CCWEAVE_ISL_DIR}/isl_range.c ${CCWEAVE_ISL_DIR}/isl_reordering.c
  ${CCWEAVE_ISL_DIR}/isl_sample.c ${CCWEAVE_ISL_DIR}/isl_scan.c
  ${CCWEAVE_ISL_DIR}/isl_schedule.c ${CCWEAVE_ISL_DIR}/isl_schedule_band.c
  ${CCWEAVE_ISL_DIR}/isl_schedule_node.c ${CCWEAVE_ISL_DIR}/isl_schedule_read.c
  ${CCWEAVE_ISL_DIR}/isl_schedule_tree.c ${CCWEAVE_ISL_DIR}/isl_schedule_constraints.c
  ${CCWEAVE_ISL_DIR}/isl_scheduler.c ${CCWEAVE_ISL_DIR}/isl_scheduler_clustering.c
  ${CCWEAVE_ISL_DIR}/isl_scheduler_scc.c ${CCWEAVE_ISL_DIR}/isl_set_list.c
  ${CCWEAVE_ISL_DIR}/isl_sort.c ${CCWEAVE_ISL_DIR}/isl_space.c
  ${CCWEAVE_ISL_DIR}/isl_stream.c ${CCWEAVE_ISL_DIR}/isl_seq.c
  ${CCWEAVE_ISL_DIR}/isl_set_to_ast_graft_list.c ${CCWEAVE_ISL_DIR}/isl_stride.c
  ${CCWEAVE_ISL_DIR}/isl_tab.c ${CCWEAVE_ISL_DIR}/isl_tab_pip.c
  ${CCWEAVE_ISL_DIR}/isl_tarjan.c ${CCWEAVE_ISL_DIR}/isl_transitive_closure.c
  ${CCWEAVE_ISL_DIR}/isl_union_map.c ${CCWEAVE_ISL_DIR}/isl_val.c
  ${CCWEAVE_ISL_DIR}/isl_vec.c ${CCWEAVE_ISL_DIR}/isl_version.c
  ${CCWEAVE_ISL_DIR}/isl_vertices.c)
add_library(ccw_isl STATIC ${CCWEAVE_ISL_SOURCES})
target_include_directories(ccw_isl SYSTEM PUBLIC
  ${CCWEAVE_ISL_CONFIG_DIR} ${CCWEAVE_ISL_DIR}
  ${CCWEAVE_ISL_DIR}/include ${CCWEAVE_GMP_INCLUDE_DIR})
target_compile_definitions(ccw_isl PRIVATE USE_GMP_FOR_MP _GNU_SOURCE)
target_link_libraries(ccw_isl PUBLIC ${CCWEAVE_GMP_LIBRARY})

# --- vendored dependencies (pinned in third_party/VERSIONS.lock) ---
# Vendored sources are never edited, so they are compiled without our
# warning and sanitizer flags; sanitizers still cover all CCWeave code.
function(ccw_vendored_target target)
  target_compile_options(${target} PRIVATE -w)
  if(CCWEAVE_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fno-sanitize=undefined)
  endif()
endfunction()

ccw_vendored_target(ccw_isl)

add_library(ccw_klib STATIC ${CCWEAVE_THIRD_PARTY}/klib/kstring.c)
target_include_directories(ccw_klib PUBLIC ${CCWEAVE_THIRD_PARTY}/klib)
ccw_vendored_target(ccw_klib)

add_library(ccw_mpc STATIC ${CCWEAVE_THIRD_PARTY}/mpc/mpc.c)
target_include_directories(ccw_mpc PUBLIC ${CCWEAVE_THIRD_PARTY}/mpc)
ccw_vendored_target(ccw_mpc)

add_library(ccw_linenoise STATIC
  ${CCWEAVE_THIRD_PARTY}/linenoise-ng/src/ConvertUTF.cpp
  ${CCWEAVE_THIRD_PARTY}/linenoise-ng/src/linenoise.cpp
  ${CCWEAVE_THIRD_PARTY}/linenoise-ng/src/wcwidth.cpp)
target_include_directories(ccw_linenoise PUBLIC
  ${CCWEAVE_THIRD_PARTY}/linenoise-ng/include)
set_target_properties(ccw_linenoise PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
ccw_vendored_target(ccw_linenoise)

add_library(ccw_s7 STATIC ${CCWEAVE_THIRD_PARTY}/s7/s7.c)
target_include_directories(ccw_s7 PUBLIC ${CCWEAVE_THIRD_PARTY}/s7)
# s7.c includes "mus-config.h" and uses struct timezone, so it needs the
# GNU dialect. Vendored source is never edited, so we adjust the target.
target_include_directories(ccw_s7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/executors/s7)
set_target_properties(ccw_s7 PROPERTIES C_EXTENSIONS ON)
target_compile_definitions(ccw_s7 PUBLIC WITH_C_LOADER=0 WITH_SYSTEM_EXTRAS=0)
ccw_vendored_target(ccw_s7)

# --- vendored dynamic interop ---------------------------------------------
# dynalo is header-only and supplies the C++ loader bridge; dyncall supplies
# the ABI-correct scalar call VM used by the language FFI layers.
add_library(ccw_dynalo STATIC glue/ccw_dynalo_bridge.cpp)
target_include_directories(ccw_dynalo PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/glue
  ${CCWEAVE_THIRD_PARTY}/dynalo/include)
set_target_properties(ccw_dynalo PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)
if(CMAKE_DL_LIBS)
  target_link_libraries(ccw_dynalo PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(dyncall_s STATIC
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_call.S
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_vector.c
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_struct.c
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_api.c
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_callvm.c
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_callvm_base.c
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_callf.c)
set_source_files_properties(
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall/dyncall_call.S
  PROPERTIES LANGUAGE C)
target_include_directories(dyncall_s PUBLIC
  ${CCWEAVE_THIRD_PARTY}/dyncall/dyncall)
ccw_vendored_target(dyncall_s)
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

  # --- vendored Tree-sitter Standard ML grammar ----------------------
  add_library(ccw_tree_sitter_sml STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-sml/src/parser.c
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-sml/src/scanner.c)
  target_include_directories(ccw_tree_sitter_sml
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-sml/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-sml/src)
  set_target_properties(ccw_tree_sitter_sml PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_sml)

  # --- vendored Delphi/Pascal grammar -------------------------------
  add_library(ccw_tree_sitter_pascal STATIC
    ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-pascal/src/parser.c)
  target_include_directories(ccw_tree_sitter_pascal
    PUBLIC ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-pascal/bindings/c
    PRIVATE ${CCWEAVE_SWAFF_GRAMMARS}/tree-sitter-pascal/src)
  set_target_properties(ccw_tree_sitter_pascal PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  ccw_vendored_target(ccw_tree_sitter_pascal)

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
add_subdirectory(toolchain/ccwas)
add_subdirectory(toolchain/ccwld)
add_subdirectory(compilers/cephyr)
add_subdirectory(compilers/delphia)
add_subdirectory(interpreters/moonix)
add_subdirectory(interpreters/sml-parthia)
add_subdirectory(docs)
add_subdirectory(tools/ccw-manifest)

enable_testing()
# stage 10: standard libraries for the supported compilers/interpreters
add_subdirectory(stdlib-salvo)
add_subdirectory(tests)
add_subdirectory(compilers/cephyr/tests)
add_subdirectory(compilers/delphia/tests)
add_subdirectory(interpreters/moonix/tests)
add_test(NAME sml-parthia
         COMMAND $<TARGET_FILE:test_sml_parthia>)
add_test(NAME sml-parthia-sched
         COMMAND $<TARGET_FILE:test_sml_parthia_sched>)
add_test(NAME sml-parthia-cli
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/interpreters/sml-parthia/tests/test_sml_parthia_cli.sh
                 $<TARGET_FILE:sml-parthia>)
