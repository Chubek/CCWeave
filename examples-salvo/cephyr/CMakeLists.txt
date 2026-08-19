# Cephyr examples: source corpus for the ISO C17 frontend.
set(CEPHYR_EXAMPLES
  01_hello.c 02_arithmetic.c 03_control_flow.c 04_functions.c
  05_arrays.c 06_strings.c 07_structs.c 08_unions.c 09_enums.c
  10_pointers.c 11_function_pointers.c 12_recursion.c 13_fib_loop.c
  14_sort.c 15_search.c 16_bitwise.c 17_switch.c 18_designated_init.c
  19_variadic.c 20_assertions.c 21_errno.c 22_time.c 23_simd.c
  24_generic.c 25_static_inline.c)

add_custom_target(cephyr_examples SOURCES ${CEPHYR_EXAMPLES})
add_library(cephyr_examples_stdlib INTERFACE)
target_include_directories(cephyr_examples_stdlib INTERFACE
  ${CMAKE_SOURCE_DIR}/stdlib-salvo/libc/include)
add_dependencies(cephyr_examples cephyr_examples_stdlib)
