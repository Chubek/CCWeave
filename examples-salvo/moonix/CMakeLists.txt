# Moonix examples: Lua 5.5 source corpus.
set(MOONIX_EXAMPLES
  01_hello.lua 02_arithmetic.lua 03_tables.lua 04_functions.lua
  05_closures.lua 06_metatables.lua 07_iteration.lua 08_recursion.lua
  09_varargs.lua 10_strings.lua 11_pattern_matching.lua 12_math.lua
  13_bitwise.lua 14_floor_division.lua 15_pcall.lua 16_error.lua
  17_coroutine_t0.lua 18_goto_t0.lua 19_table_sort.lua 20_object.lua
  21_memoize.lua 22_tail_recursion.lua 23_multiple_returns.lua
  24_local_attributes.lua 25_module.lua)

add_custom_target(moonix_examples SOURCES ${MOONIX_EXAMPLES})
