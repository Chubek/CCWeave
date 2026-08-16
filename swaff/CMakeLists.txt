# Swaff: swappable frontends. Only adapters may reference Tree-sitter.
add_library(ccw_swaff STATIC adapters/ccw_swaff_c.c)
target_include_directories(ccw_swaff PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_swaff PUBLIC ccw_ir ccw_kliche)

if(CCWEAVE_ENABLE_TREESITTER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TREESITTER REQUIRED tree-sitter)
  pkg_check_modules(TREESITTER_C REQUIRED tree-sitter-c)
  target_compile_definitions(ccw_swaff PRIVATE CCWEAVE_WITH_TREESITTER=1)
  target_include_directories(ccw_swaff PRIVATE
    ${TREESITTER_INCLUDE_DIRS} ${TREESITTER_C_INCLUDE_DIRS})
  target_link_libraries(ccw_swaff PUBLIC
    ${TREESITTER_LIBRARIES} ${TREESITTER_C_LIBRARIES})
endif()
