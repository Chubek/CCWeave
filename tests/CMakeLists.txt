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

ccw_add_test(test_isl test_isl.c)
target_link_libraries(test_isl PRIVATE ccw_glue)

ccw_add_test(test_functional_kernels test_functional_kernels.c)
target_link_libraries(test_functional_kernels PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_functional_kernels PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_functional_kernels_more test_functional_kernels_more.c)
target_link_libraries(test_functional_kernels_more
  PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_functional_kernels_more PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_functional_kernels_final test_functional_kernels_final.c)
target_link_libraries(test_functional_kernels_final
  PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_functional_kernels_final PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_new_kernels test_new_kernels.c)
target_link_libraries(test_new_kernels PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_new_kernels PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_syscall_kernels test_syscall_kernels.c)
target_link_libraries(test_syscall_kernels PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_syscall_kernels PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_simd_kernels test_simd_kernels.c)
target_link_libraries(test_simd_kernels PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_simd_kernels PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_gpu_kernels test_gpu_kernels.c)
target_link_libraries(test_gpu_kernels PRIVATE ccw_host_glue ccw_executor_s7)
target_compile_definitions(test_gpu_kernels PRIVATE
  CCW_KERNEL_DIR="${CMAKE_SOURCE_DIR}/kernels")

ccw_add_test(test_oeuph test_oeuph.c)
target_link_libraries(test_oeuph PRIVATE ccw_oeuph)
target_compile_definitions(test_oeuph PRIVATE
  CCW_REWRITE_SALVO_DIR="${CMAKE_SOURCE_DIR}/rewrite-salvo")

ccw_add_test(test_sched test_sched.c)
target_link_libraries(test_sched PRIVATE ccw_sched)
target_compile_definitions(test_sched PRIVATE
  CCW_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests"
  CCW_SCHED_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sched-basic.lua")

ccw_add_test(test_kliche_swaff test_kliche_swaff.c)
target_link_libraries(test_kliche_swaff PRIVATE ccw_kliche ccw_swaff)

ccw_add_test(test_ccwld test_ccwld.c)
target_link_libraries(test_ccwld PRIVATE ccwld)
target_compile_definitions(test_ccwld PRIVATE
  CCWLD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

ccw_add_test(test_ccwld_lua test_ccwld_lua.c)
target_link_libraries(test_ccwld_lua PRIVATE ccwld)
target_compile_definitions(test_ccwld_lua PRIVATE
  CCWLD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

ccw_add_test(test_ccwld_emit test_ccwld_emit.c)
target_link_libraries(test_ccwld_emit PRIVATE ccwld)

if(CCWEAVE_ENABLE_TREESITTER)
  ccw_add_test(test_swaff_c test_swaff_c.c)
  target_link_libraries(test_swaff_c PRIVATE ccw_swaff)

  ccw_add_test(test_swaff_lua test_swaff_lua.c)
  target_link_libraries(test_swaff_lua PRIVATE ccw_swaff)

  ccw_add_test(test_swaff_ocaml test_swaff_ocaml.c)
  target_link_libraries(test_swaff_ocaml PRIVATE ccw_swaff)

  ccw_add_test(test_swaff_sml test_swaff_sml.c)
  target_link_libraries(test_swaff_sml PRIVATE ccw_swaff)

  ccw_add_test(test_swaff_delphi test_swaff_delphi.c)
  target_link_libraries(test_swaff_delphi PRIVATE ccw_swaff)
endif()

# Manifests must regenerate identically and fail on drift (§4.2).
add_test(NAME test_manifest_check
         COMMAND ${CMAKE_COMMAND} -E env sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_manifest_check.sh
                 $<TARGET_FILE:ccw-manifest> ${CMAKE_SOURCE_DIR})

# CI gate: the committed manifests must match the live kernels.
add_test(NAME test_manifests_committed
         COMMAND ccw-manifest --root ${CMAKE_SOURCE_DIR} --check)
