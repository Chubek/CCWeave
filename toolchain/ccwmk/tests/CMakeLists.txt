add_executable(test_ccwmk_vars test_vars.c)
target_link_libraries(test_ccwmk_vars PRIVATE ccwmk)
target_compile_definitions(test_ccwmk_vars PRIVATE _POSIX_C_SOURCE=200809L)
add_test(NAME ccwmk-vars COMMAND test_ccwmk_vars)
