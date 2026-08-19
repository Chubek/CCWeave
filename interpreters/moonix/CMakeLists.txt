# Moonix Lua runtime — .agents/MOONIX.md §8.

add_library(moonix_lib STATIC
  runtime/moonix.c
  runtime/jit.c
  frontend/moonix_frontend.c
  stdlib/moonix_stdlib.c)
set_target_properties(moonix_lib PROPERTIES OUTPUT_NAME moonix)
target_include_directories(moonix_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/frontend
  ${CMAKE_CURRENT_SOURCE_DIR}/stdlib
  ${CMAKE_SOURCE_DIR}/swaff
  ${CMAKE_SOURCE_DIR}/ir
  ${CMAKE_SOURCE_DIR}/sched)
target_include_directories(moonix_lib PRIVATE
  ${CMAKE_SOURCE_DIR}/glue
  ${CMAKE_SOURCE_DIR}/third_party/dyncall/dyncall)
target_compile_definitions(moonix_lib PRIVATE
  MOONIX_DEFAULT_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests"
  MOONIX_DEFAULT_SCHED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sched")
target_link_libraries(moonix_lib PUBLIC ccw_swaff ccw_sched ccw_ir ccw_klib
  ccw_dynalo dyncall_s)
if(CMAKE_DL_LIBS)
  target_link_libraries(moonix_lib PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(moonix
  cli/main.c
  cli/repl.c)
target_link_libraries(moonix PRIVATE moonix_lib ccw_linenoise)
