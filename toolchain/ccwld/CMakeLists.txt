# CCWld — linker frontends, plan IR, phase pipeline, emitters, cache
# (CCWLD.md §10 layout).

# --- emitter backends (emit/) ---------------------------------------------
if(TARGET LIB_LIEF)
  set(CCWLD_LIEF_SRC emit/ccwld_emit_lief.cpp)
else()
  set(CCWLD_LIEF_SRC emit/ccwld_emit_lief_stub.c)
endif()
if(TARGET binaryen)
  set(CCWLD_BINARYEN_SRC emit/ccwld_emit_binaryen.cpp)
else()
  set(CCWLD_BINARYEN_SRC emit/ccwld_emit_binaryen_stub.c)
endif()

add_library(ccwld STATIC
  ccwld.c
  plan/ccwld_plan.c
  expr/ccwld_expr.c
  phases/ccwld_state.c
  phases/ccwld_load.c
  phases/ccwld_resolve.c
  phases/ccwld_lto.c
  phases/ccwld_gc.c
  phases/ccwld_layout.c
  phases/ccwld_relocate.c
  emit/ccwld_emit_elf.c
  ${CCWLD_LIEF_SRC}
  ${CCWLD_BINARYEN_SRC}
  cache/ccwld_cache.c
  frontend/ldscript/ccwld_ldscript.c
  lccwld/lccwld.c)
target_include_directories(ccwld PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/abi
  ${CMAKE_SOURCE_DIR}/third_party/lua)
target_link_libraries(ccwld PUBLIC ccw_lua ccw_mpc ${CMAKE_DL_LIBS})
if(TARGET LIB_LIEF)
  target_link_libraries(ccwld PUBLIC LIB_LIEF)
endif()
if(TARGET binaryen)
  target_link_libraries(ccwld PUBLIC binaryen)
endif()
target_compile_definitions(ccwld PRIVATE _POSIX_C_SOURCE=200809L)

# --- driver -----------------------------------------------------------------
# The binary hosts plugins (dlopen, RTLD_LOCAL): its symbols must stay
# visible to the loaded objects.
add_executable(ccwld-bin driver/main.c)
set_target_properties(ccwld-bin PROPERTIES
  OUTPUT_NAME ccwld
  ENABLE_EXPORTS ON)
target_link_options(ccwld-bin PRIVATE -rdynamic)
target_link_libraries(ccwld-bin PRIVATE ccwld)
target_compile_definitions(ccwld-bin PRIVATE _POSIX_C_SOURCE=200809L)

# --- in-tree reference plugin and LTO backends (§10, D-0041/D-0042) ---------
# MODULE libraries: they resolve ccwld_link_*/ccwld_lto_* symbols from
# the hosting executable at runtime and never link the static lib.
add_library(ccwld_refplugin MODULE tests/plugin/refplugin.c)
target_include_directories(ccwld_refplugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ccwld_plugin_badabi MODULE tests/plugin/badabi.c)
target_include_directories(ccwld_plugin_badabi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ccwld_plugin_badphases MODULE tests/plugin/badphases.c)
target_include_directories(ccwld_plugin_badphases PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ccwld_lto_ref MODULE tests/lto/lto_ref.c)
target_include_directories(ccwld_lto_ref PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ccwld_lto_ref PRIVATE _POSIX_C_SOURCE=200809L)
add_library(ccwld_lto_badabi MODULE tests/lto/lto_badabi.c)
target_include_directories(ccwld_lto_badabi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ccwld_lto_missing MODULE tests/lto/lto_missing.c)
target_include_directories(ccwld_lto_missing PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})

# --- test suites (§10: parity, phases, expr, lto, plugin, emit,
# determinism, negative) ----------------------------------------------------
if(CCWEAVE_BUILD_TESTS)
  enable_testing()

  function(ccwld_add_test name src)
    add_executable(${name} ${src} tests/common/testobj.c)
    target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests/common)
    target_link_libraries(${name} PRIVATE ccwld)
    target_compile_definitions(${name} PRIVATE
      _POSIX_C_SOURCE=200809L
      CCWLD_TEST_PLUGIN="$<TARGET_FILE:ccwld_refplugin>"
      CCWLD_TEST_PLUGIN_BADABI="$<TARGET_FILE:ccwld_plugin_badabi>"
      CCWLD_TEST_PLUGIN_BADPHASES="$<TARGET_FILE:ccwld_plugin_badphases>"
      CCWLD_TEST_LTO_REF="$<TARGET_FILE:ccwld_lto_ref>"
      CCWLD_TEST_LTO_BADABI="$<TARGET_FILE:ccwld_lto_badabi>"
      CCWLD_TEST_LTO_MISSING="$<TARGET_FILE:ccwld_lto_missing>")
    # tests host the same plugin surface the real driver does
    set_target_properties(${name} PROPERTIES ENABLE_EXPORTS ON)
    target_link_options(${name} PRIVATE -rdynamic)
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  ccwld_add_test(test_ccwld_parity tests/parity/test_parity.c)
  ccwld_add_test(test_ccwld_phases tests/phases/test_phases.c)
  ccwld_add_test(test_ccwld_expr tests/expr/test_expr.c)
  ccwld_add_test(test_ccwld_lto tests/lto/test_lto.c)
  ccwld_add_test(test_ccwld_plugin tests/plugin/test_plugin.c)
  ccwld_add_test(test_ccwld_emit tests/emit/test_emit.c)
  ccwld_add_test(test_ccwld_determinism tests/determinism/test_determinism.c)
  ccwld_add_test(test_ccwld_negative tests/negative/test_negative.c)
endif()
