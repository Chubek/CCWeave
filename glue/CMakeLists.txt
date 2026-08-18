# Glue: the ABI header plus the ccw_val boundary-value support library.
add_library(ccw_glue STATIC ccw_val.c ccw_isl.c)
target_include_directories(ccw_glue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_glue PUBLIC ccw_klib ccw_isl)

# Host accessor layer (Core Accessor Set); depends on the IR it exposes.
add_library(ccw_host_glue STATIC ccw_host_accessors.c)
target_include_directories(ccw_host_glue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_host_glue PUBLIC ccw_glue ccw_ir ccw_klib)
