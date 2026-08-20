# SML-Parthia examples: SML '97 source corpus.
set(SML_PARTHIA_EXAMPLES
  01_hello.sml 02_arithmetic.sml 03_lists.sml 04_functions.sml
  05_pattern_matching.sml 06_options.sml 07_tuples.sml 08_records.sml
  09_datatypes.sml 10_case.sml 11_recursion.sml 12_tail_recursion.sml
  13_higher_order.sml 14_currying.sml 15_modules.sml 16_signatures.sml
  17_functors.sml 18_exceptions.sml 19_refs.sml 20_arrays.sml
  21_fold.sml 22_map_filter.sml 23_string_ops.sml 24_basis_native.sml
  25_mutual_recursion.sml)

add_custom_target(sml_parthia_examples SOURCES ${SML_PARTHIA_EXAMPLES})
add_library(sml_parthia_examples_basis INTERFACE)
target_include_directories(sml_parthia_examples_basis INTERFACE
  ${CMAKE_SOURCE_DIR}/stdlib-salvo/sml-basis)
set_property(TARGET sml_parthia_examples_basis PROPERTY
  CCW_SML_BASIS_DIR "${CMAKE_SOURCE_DIR}/stdlib-salvo/sml-basis")
add_dependencies(sml_parthia_examples sml_parthia_examples_basis)
