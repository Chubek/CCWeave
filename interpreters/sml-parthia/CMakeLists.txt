# Parthia Standard ML compiler/interpreter façade (§2, §8).

add_library(sml_parthia STATIC
  src/sml_parthia.c)
target_include_directories(sml_parthia PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/swaff)
target_link_libraries(sml_parthia PUBLIC ccw_swaff ccw_klib)

add_executable(sml-parthia
  cli/main.c)
target_link_libraries(sml-parthia PRIVATE sml_parthia)

add_executable(test_sml_parthia
  tests/test_sml_parthia.c)
target_include_directories(test_sml_parthia PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sml_parthia PRIVATE sml_parthia)
