add_library(ccwmk STATIC
  src/ast.c
  src/fetch.c
  src/exec.c
  src/grammar.c
  src/graph.c
  src/lua.c
  src/parse.c
  src/plugin.c
  src/vars.c
  src/scan-c.c
  src/lua/lccwmk.c)
target_include_directories(ccwmk PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
  ${CMAKE_SOURCE_DIR}/third_party/mpc
  ${CMAKE_SOURCE_DIR}/third_party/lua
  ${CMAKE_SOURCE_DIR}/third_party/ucpp)
target_link_libraries(ccwmk PUBLIC ccw_mpc ccw_lua)
target_compile_definitions(ccwmk PRIVATE _POSIX_C_SOURCE=200809L)

add_executable(ccwmk-bin ccw-ccwmk.c)
set_target_properties(ccwmk-bin PROPERTIES OUTPUT_NAME ccwmk ENABLE_EXPORTS ON)
target_link_libraries(ccwmk-bin PRIVATE ccwmk)
target_compile_definitions(ccwmk-bin PRIVATE _POSIX_C_SOURCE=200809L)
