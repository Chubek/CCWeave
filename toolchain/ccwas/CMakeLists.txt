add_library(ccw_lccwas STATIC lccwas/lccwas.c)
target_include_directories(ccw_lccwas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lccwas ${CMAKE_SOURCE_DIR}/third_party/lua)
target_link_libraries(ccw_lccwas PUBLIC ccw_lua)
add_executable(ccwas driver/main.c)
target_include_directories(ccwas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lccwas)
target_link_libraries(ccwas PRIVATE ccw_lccwas)
enable_testing()
add_test(NAME test_ccwas_basic
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ccwas.sh $<TARGET_FILE:ccwas>)
