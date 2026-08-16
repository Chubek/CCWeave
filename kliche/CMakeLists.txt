# Kliche: paradigm stereotypes over core Weave IR construction calls.
add_library(ccw_kliche STATIC
  ccw_kliche_common.c
  functional/ccw_kliche_functional.c
  imperative/ccw_kliche_imperative.c
  oop/ccw_kliche_oop.c
)
target_include_directories(ccw_kliche PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_kliche PUBLIC ccw_ir)
