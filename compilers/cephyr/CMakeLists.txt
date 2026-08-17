# Cephyr C compiler — §8, §10.
# First consumer of the CCWeave infrastructure.

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
target_link_libraries(cephyr_cpp PUBLIC cephyr_ucpp)

# --- sema ---
add_library(cephyr_sema STATIC sema/cephyr_sema.c sema/cephyr_ast.c)
target_include_directories(cephyr_sema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_SOURCE_DIR}/third_party/klib)

# --- lower ---
add_library(cephyr_lower STATIC lower/cephyr_lower.c)
target_include_directories(cephyr_lower PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lower
  ${CMAKE_SOURCE_DIR}/kliche ${CMAKE_SOURCE_DIR}/ir
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_lower PUBLIC ccw_ir cephyr_sema)

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
  ${CMAKE_SOURCE_DIR}/swaff ${CMAKE_SOURCE_DIR}/ir ${CMAKE_SOURCE_DIR}/sched)
target_include_directories(cephyr_driver PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(cephyr_driver PUBLIC cephyr_cpp cephyr_sema cephyr_lower
  cephyr_stdmodule ccw_ir ccw_swaff ccw_sched)

# --- cephyr executable ---
add_executable(cephyr driver/main.c)
target_link_libraries(cephyr PRIVATE cephyr_driver)
