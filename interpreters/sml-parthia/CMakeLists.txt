# Parthia Standard ML compiler/interpreter façade (§2, §8).

add_library(sml_parthia STATIC
  src/sml_parthia.c)
target_include_directories(sml_parthia PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/swaff)
target_link_libraries(sml_parthia PUBLIC ccw_swaff ccw_klib)
target_link_libraries(sml_parthia PUBLIC ccw_sched)

add_executable(sml-parthia
  cli/main.c
  cli/repl.c)
target_link_libraries(sml-parthia PRIVATE sml_parthia ccw_linenoise)

add_executable(test_sml_parthia
  tests/test_sml_parthia.c)
target_include_directories(test_sml_parthia PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sml_parthia PRIVATE sml_parthia)

add_executable(test_sml_parthia_sched
  tests/test_sml_parthia_sched.c)
target_include_directories(test_sml_parthia_sched PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/sched)
target_compile_definitions(test_sml_parthia_sched PRIVATE
  CCW_SML_PARTHIA_SCHED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sched"
  CCW_SML_PARTHIA_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests")
target_link_libraries(test_sml_parthia_sched PRIVATE sml_parthia)
