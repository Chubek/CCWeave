# Delphia examples: Delphi/Object Pascal source corpus.
set(DELPHIA_EXAMPLES
  01_hello.pas 02_arithmetic.pas 03_control_flow.pas 04_functions.pas
  05_arrays.pas 06_records.pas 07_enums.pas 08_sets.pas 09_strings.pas
  10_pointers.pas 11_classes.pas 12_virtual_methods.pas 13_properties.pas
  14_exceptions.pas 15_units.pas 16_recursion.pas 17_sort.pas
  18_case.pas 19_nested_procedures.pas 20_dynamic_array.pas 21_interfaces.pas
  22_generics.pas 23_anonymous_method.pas 24_class_helper.pas 25_rtti.pas)

add_custom_target(delphia_examples SOURCES ${DELPHIA_EXAMPLES})
