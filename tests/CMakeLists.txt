# CCWeave test suite (AGENTS.md: every stage lands with tests).

function(ccw_add_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ccw_add_test(test_ir_roundtrip test_ir_roundtrip.c)
target_link_libraries(test_ir_roundtrip PRIVATE ccw_ir)

ccw_add_test(test_ir_profiles test_ir_profiles.c)
target_link_libraries(test_ir_profiles PRIVATE ccw_ir)

ccw_add_test(test_glue_executor test_glue_executor.c)
target_link_libraries(test_glue_executor PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_glue_executor PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels"
  CCW_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

ccw_add_test(test_oeuph test_oeuph.c)
target_link_libraries(test_oeuph PRIVATE ccw_oeuph)
target_compile_definitions(test_oeuph PRIVATE
  CCW_STDREWRITE_DIR="${CMAKE_SOURCE_DIR}/stdrewrite")

ccw_add_test(test_kliche_swaff test_kliche_swaff.c)
target_link_libraries(test_kliche_swaff PRIVATE ccw_kliche ccw_swaff)

if(CCWEAVE_ENABLE_TREESITTER)
  ccw_add_test(test_swaff_c test_swaff_c.c)
  target_link_libraries(test_swaff_c PRIVATE ccw_swaff)
endif()

# Manifests must regenerate identically and fail on drift (§4.2).
add_test(NAME test_manifest_check
         COMMAND ${CMAKE_COMMAND} -E env sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_manifest_check.sh
                 $<TARGET_FILE:ccw-manifest> ${CMAKE_SOURCE_DIR})

# CI gate: the committed manifests must match the live kernels.
add_test(NAME test_manifests_committed
         COMMAND ccw-manifest --root ${CMAKE_SOURCE_DIR} --check)
