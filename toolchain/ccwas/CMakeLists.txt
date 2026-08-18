add_library(ccw_lccwas STATIC lccwas/lccwas.c)
target_include_directories(ccw_lccwas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lccwas ${CMAKE_SOURCE_DIR}/third_party/lua)
target_link_libraries(ccw_lccwas PUBLIC ccw_lua ccw_klib)

add_library(ccwas_api STATIC ccwas.c)
target_include_directories(ccwas_api PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/lccwas
  ${CMAKE_CURRENT_SOURCE_DIR}/parse
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_CURRENT_SOURCE_DIR}/obj
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(ccwas_api PUBLIC ccw_lccwas ccw_parse ccw_obj)

set(CCWAS_POSIX_TARGETS ccw_lccwas ccwas_api)

# --- ccwas sema library (symbol table, expression evaluator, ISA validation) ---
add_library(ccw_sema STATIC
  sema/ccw_symtab.c
)
list(APPEND CCWAS_POSIX_TARGETS ccw_sema)
target_include_directories(ccw_sema PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_SOURCE_DIR}/third_party/klib
)
target_link_libraries(ccw_sema PUBLIC ccw_klib)

# --- ccwas parse library (assembly parser) ---
add_library(ccw_parse STATIC
  parse/ccw_parse.c
)
list(APPEND CCWAS_POSIX_TARGETS ccw_parse)
target_include_directories(ccw_parse PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/parse
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_CURRENT_SOURCE_DIR}/encode
  ${CMAKE_SOURCE_DIR}/third_party/klib
)
target_link_libraries(ccw_parse PUBLIC ccw_sema ccw_encode)
target_link_libraries(ccw_parse PUBLIC ccw_klib)

# --- ccwas encode library (per-target encoders) ---
add_library(ccw_encode STATIC
  encode/ccw_encode_x86.c
)
list(APPEND CCWAS_POSIX_TARGETS ccw_encode)
target_include_directories(ccw_encode PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/encode
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_SOURCE_DIR}/third_party/klib
)
target_link_libraries(ccw_encode PUBLIC ccw_sema)
target_link_libraries(ccw_encode PUBLIC ccw_klib)

# --- ccwas obj library (object writer) ---
add_library(ccw_obj STATIC
  obj/ccw_obj_elf.c
)
list(APPEND CCWAS_POSIX_TARGETS ccw_obj)
target_include_directories(ccw_obj PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/obj
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_SOURCE_DIR}/third_party/klib
)
target_link_libraries(ccw_obj PUBLIC ccw_sema)
target_link_libraries(ccw_obj PUBLIC ccw_klib)

# --- ccwas driver ---
add_executable(ccwas driver/main.c)
list(APPEND CCWAS_POSIX_TARGETS ccwas)
target_include_directories(ccwas PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lccwas
  ${CMAKE_CURRENT_SOURCE_DIR}/sema
  ${CMAKE_CURRENT_SOURCE_DIR}/parse
  ${CMAKE_CURRENT_SOURCE_DIR}/encode
  ${CMAKE_CURRENT_SOURCE_DIR}/obj
  ${CMAKE_SOURCE_DIR}/third_party/klib
)
target_link_libraries(ccwas PRIVATE ccw_lccwas ccw_sema ccw_parse ccw_encode ccw_obj)

foreach(target IN LISTS CCWAS_POSIX_TARGETS)
  target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
endforeach()

enable_testing()
add_test(NAME test_ccwas_basic
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ccwas.sh $<TARGET_FILE:ccwas>)
add_test(NAME test_ccwas_comprehensive
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ccwas_comprehensive.sh
          $<TARGET_FILE:ccwas>)
