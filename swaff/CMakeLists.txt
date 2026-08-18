# Swaff: swappable frontends. Only adapters may reference Tree-sitter.
add_library(ccw_swaff STATIC
  adapters/ccw_swaff_c.c
  adapters/ccw_swaff_lua.c
  adapters/ccw_swaff_ocaml.c
  adapters/ccw_swaff_sml.c
)
target_include_directories(ccw_swaff PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_swaff PUBLIC ccw_ir ccw_kliche ccw_klib)

if(CCWEAVE_ENABLE_TREESITTER)
  target_compile_definitions(ccw_swaff PRIVATE CCWEAVE_WITH_TREESITTER=1)
  target_link_libraries(ccw_swaff PRIVATE
    ccw_tree_sitter
    ccw_tree_sitter_c
    ccw_tree_sitter_lua
    ccw_tree_sitter_ocaml
    ccw_tree_sitter_sml)
endif()
