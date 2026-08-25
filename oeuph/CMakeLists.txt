# Oeuph: equality-saturation engine over the canonical in-memory IR.
add_library(ccw_oeuph STATIC
  ccw_oeuph.c
  ccw_oeuph_pattern.c
  ccw_oeuph_ruleset_load.c
  ccw_sema.c
)
target_include_directories(ccw_oeuph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_oeuph PUBLIC ccw_ir ccw_klib ccw_sfsexp)
