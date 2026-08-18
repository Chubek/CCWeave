# Cephyr C compiler — §8, §10.
# First consumer of the CCWeave infrastructure.

# --- configuration parsers (vendored; no build-time downloads) ------------
set(CEPHYR_CYAML_SOURCES
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/mem.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/free.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/load.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/save.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/copy.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/util.c
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/src/utf8.c)
add_library(cephyr_cyaml STATIC ${CEPHYR_CYAML_SOURCES})
target_include_directories(cephyr_cyaml PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/include)
target_compile_definitions(cephyr_cyaml PRIVATE
  VERSION_MAJOR=2 VERSION_MINOR=0 VERSION_PATCH=0 VERSION_DEVEL=1)
find_library(CEPHYR_YAML_LIBRARY yaml REQUIRED)
target_link_libraries(cephyr_cyaml PUBLIC ${CEPHYR_YAML_LIBRARY})
ccw_vendored_target(cephyr_cyaml)

add_library(cephyr_toml STATIC ${CMAKE_SOURCE_DIR}/third_party/tomlc99/toml.c)
target_include_directories(cephyr_toml PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/tomlc99)
ccw_vendored_target(cephyr_toml)

add_library(cephyr_profile STATIC profile/cephyr_profile.c)
target_include_directories(cephyr_profile PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/profile
  ${CMAKE_SOURCE_DIR}/third_party/libcyaml/include
  ${CMAKE_SOURCE_DIR}/third_party/tomlc99
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_profile PUBLIC cephyr_cyaml cephyr_toml ccw_klib)

# --- ucpp preprocessor library (vendored, built as STATIC) ---
set(CEPHYR_UCPP_SOURCES
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/cpp.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/lexer.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/macro.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/eval.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/assert.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/mem.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/nhash.c
  ${CMAKE_SOURCE_DIR}/third_party/ucpp/hash.c)
add_library(cephyr_ucpp STATIC ${CEPHYR_UCPP_SOURCES})
target_include_directories(cephyr_ucpp PUBLIC ${CMAKE_SOURCE_DIR}/third_party/ucpp)
target_compile_definitions(cephyr_ucpp PRIVATE PRAGMA_TOKENIZE)
set_target_properties(cephyr_ucpp PROPERTIES C_EXTENSIONS ON)
ccw_vendored_target(cephyr_ucpp)

# --- cpp wrapper ---
add_library(cephyr_cpp STATIC cpp/cephyr_cpp.c)
target_include_directories(cephyr_cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp
  ${CMAKE_SOURCE_DIR}/third_party/ucpp
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_cpp PUBLIC cephyr_ucpp ccw_klib)

# --- sema ---
add_library(cephyr_sema STATIC sema/cephyr_sema.c sema/cephyr_ast.c)
target_include_directories(cephyr_sema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_sema PUBLIC ccw_klib)

# --- lower ---
add_library(cephyr_lower STATIC lower/cephyr_lower.c)
target_include_directories(cephyr_lower PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lower
  ${CMAKE_SOURCE_DIR}/kliche ${CMAKE_SOURCE_DIR}/ir
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_lower PUBLIC ccw_ir cephyr_sema ccw_klib)

# --- stdmodule ---
add_library(cephyr_stdmodule STATIC stdmodule/cephyr_stdmodule.c)
target_include_directories(cephyr_stdmodule PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/stdmodule)

# --- driver ---
add_library(cephyr_driver STATIC driver/cephyr_driver.c)
target_include_directories(cephyr_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/driver
  ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_CURRENT_SOURCE_DIR}/lower ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/stdmodule
  ${CMAKE_CURRENT_SOURCE_DIR}/profile
  ${CMAKE_SOURCE_DIR}/swaff ${CMAKE_SOURCE_DIR}/ir ${CMAKE_SOURCE_DIR}/sched)
target_include_directories(cephyr_driver PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_driver PUBLIC cephyr_cpp cephyr_sema cephyr_lower
  cephyr_stdmodule cephyr_profile ccw_ir ccw_swaff ccw_sched ccw_klib)
add_dependencies(cephyr_driver ccwas)
target_compile_definitions(cephyr_driver PRIVATE
  CEPHYR_DEFAULT_ASSEMBLER_PATH="$<TARGET_FILE:ccwas>"
  CEPHYR_DEFAULT_LINKER_PATH="$<TARGET_FILE:ccwld-bin>")
add_dependencies(cephyr_driver ccwld-bin)

# --- cephyr executable ---
add_executable(cephyr driver/main.c)
target_link_libraries(cephyr PRIVATE cephyr_driver)
