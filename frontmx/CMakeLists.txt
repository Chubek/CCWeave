add_library(ccw_frontmx STATIC
  ccw-frontmx.cpp
  src/sppf.c src/gss.c src/parser.c src/grammar.c src/ast.c
  src/attr.c src/diamb.c src/rewrite.c src/generate.c)
set_source_files_properties(
  src/sppf.c src/gss.c src/parser.c src/grammar.c src/ast.c src/attr.c
  src/diamb.c src/rewrite.c src/generate.c
  PROPERTIES LANGUAGE CXX)
target_include_directories(ccw_frontmx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ccw_frontmx PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/sfsexp/src
  ${CMAKE_SOURCE_DIR}/third_party/re2)
target_link_libraries(ccw_frontmx PUBLIC ccw_sfsexp)
find_library(CCW_RE2_LIBRARY NAMES re2 REQUIRED)
target_link_libraries(ccw_frontmx PRIVATE ${CCW_RE2_LIBRARY})
