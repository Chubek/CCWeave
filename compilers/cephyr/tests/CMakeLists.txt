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

cephyr_add_test(test_cephyr_profile test_cephyr_profile.c)
target_include_directories(test_cephyr_profile PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../profile)
target_link_libraries(test_cephyr_profile PRIVATE cephyr_profile)
target_compile_definitions(test_cephyr_profile PRIVATE
  CEPHYR_PROFILE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

cephyr_add_test(test_cephyr_driver_tools test_cephyr_driver_tools.c)
target_link_libraries(test_cephyr_driver_tools PRIVATE cephyr_driver)

add_test(NAME test_cephyr_cli
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_cephyr_cli.sh
          $<TARGET_FILE:cephyr>)
set_tests_properties(test_cephyr_cli PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
