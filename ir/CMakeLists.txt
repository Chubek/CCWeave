# Weave IR: core, text serialization, validation, and the two profiles.
add_library(ccw_ir STATIC
  ccw_ir.c
  ccw_ir_print.c
  ccw_ir_parse.c
  ccw_ir_validate.c
  tilly/ccw_tilly.c
  on1x/ccw_on1x.c
)
target_include_directories(ccw_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_ir PUBLIC ccw_glue PRIVATE ccw_mpc)
