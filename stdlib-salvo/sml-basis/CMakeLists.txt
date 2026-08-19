# SML Basis subset for Parthia: SML source plus a small native/FFI companion.
add_library(sml_basis_native SHARED sml_basis_native.c)
target_include_directories(sml_basis_native PUBLIC
  ${CMAKE_SOURCE_DIR}/interpreters/sml-parthia/include
  ${CMAKE_SOURCE_DIR}/swaff)
target_include_directories(sml_basis_native PRIVATE
  ${CMAKE_SOURCE_DIR}/sched ${CMAKE_SOURCE_DIR}/ir
  ${CMAKE_SOURCE_DIR}/oeuph)
set_target_properties(sml_basis_native PROPERTIES OUTPUT_NAME sml_basis)

add_library(sml_basis INTERFACE)
target_include_directories(sml_basis INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR})
