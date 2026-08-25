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

# Install the basis as one directory that both halves share: the installed
# sml-parthia launcher appends it to the comma-separated SML_PARTHIA_PATH,
# where `use "Basis.sml"` and bare native-library names are searched.
install(FILES Basis.sml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ccweave/sml-basis)
install(TARGETS sml_basis_native
        LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/ccweave/sml-basis
        RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/ccweave/sml-basis)
