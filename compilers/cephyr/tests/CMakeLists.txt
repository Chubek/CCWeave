function(cephyr_add_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sema
    ${CMAKE_CURRENT_SOURCE_DIR}/../lower
    ${CMAKE_CURRENT_SOURCE_DIR}/../stdmodule
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

cephyr_add_test(test_cephyr_cpp test_cephyr_cpp.c)
target_link_libraries(test_cephyr_cpp PRIVATE cephyr_cpp)

cephyr_add_test(test_cephyr_sema test_cephyr_sema.c)
target_link_libraries(test_cephyr_sema PRIVATE cephyr_sema)

cephyr_add_test(test_cephyr_lower test_cephyr_lower.c)
target_link_libraries(test_cephyr_lower PRIVATE cephyr_lower ccw_kliche)

cephyr_add_test(test_cephyr_stdmodule test_cephyr_stdmodule.c)
target_link_libraries(test_cephyr_stdmodule PRIVATE cephyr_stdmodule)

cephyr_add_test(test_cephyr_sched test_cephyr_sched.c)
target_link_libraries(test_cephyr_sched PRIVATE ccw_sched)
target_compile_definitions(test_cephyr_sched PRIVATE
  CCW_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests"
  CEPHYR_SCHED_DIR="${CMAKE_SOURCE_DIR}/compilers/cephyr/sched")
