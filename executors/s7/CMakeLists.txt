# S7 reference executor: implements GlueSTD and consumes its bridge re-exports.
add_library(ccw_executor_s7 STATIC ccw_s7_executor.c)
target_include_directories(ccw_executor_s7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_executor_s7 PUBLIC ccw_glue ccw_klib PRIVATE ccw_s7)
