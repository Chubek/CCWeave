add_executable(test_moonix_runtime test_moonix_runtime.c)
target_include_directories(test_moonix_runtime PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_SOURCE_DIR}/interpreters/moonix/runtime
  )
target_link_libraries(test_moonix_runtime PRIVATE moonix_lib)
add_test(NAME moonix-runtime COMMAND test_moonix_runtime)

add_executable(test_moonix_frontend test_moonix_frontend.c)
target_include_directories(test_moonix_frontend PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_SOURCE_DIR}/interpreters/moonix/runtime
  ${CMAKE_SOURCE_DIR}/ir)
target_link_libraries(test_moonix_frontend PRIVATE moonix_lib)
add_test(NAME moonix-frontend COMMAND test_moonix_frontend)

add_executable(test_moonix_sched test_moonix_sched.c)
target_include_directories(test_moonix_sched PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_SOURCE_DIR}/sched)
target_compile_definitions(test_moonix_sched PRIVATE
  MOONIX_SCHED_DIR="${CMAKE_SOURCE_DIR}/interpreters/moonix/sched"
  CCW_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests")
target_link_libraries(test_moonix_sched PRIVATE ccw_sched)
add_test(NAME moonix-sched COMMAND test_moonix_sched)

add_test(NAME moonix-cli
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_moonix_cli.sh $<TARGET_FILE:moonix>)
