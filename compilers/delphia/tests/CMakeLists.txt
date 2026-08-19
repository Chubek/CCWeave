add_executable(test_delphia_driver test_delphia_driver.c)
target_include_directories(test_delphia_driver PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../driver
  ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_delphia_driver PRIVATE delphia_driver)
target_compile_definitions(test_delphia_driver PRIVATE
  DELPHIA_SOURCE_DIR="${CMAKE_SOURCE_DIR}/compilers/delphia/tests")
add_test(NAME test_delphia_driver COMMAND test_delphia_driver)
