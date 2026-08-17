set(CCW_LUA_SOURCES
  ${CMAKE_SOURCE_DIR}/third_party/lua/lapi.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lauxlib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lbaselib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lcode.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lctype.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ldebug.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ldo.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ldump.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lfunc.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lgc.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/linit.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/llex.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lmathlib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lmem.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lobject.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lopcodes.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lparser.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lstate.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lstring.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lstrlib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ltable.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ltablib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/ltm.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lundump.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lutf8lib.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lvm.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lzio.c
  ${CMAKE_SOURCE_DIR}/third_party/lua/lcorolib.c)
add_library(ccw_lua STATIC ${CCW_LUA_SOURCES})
target_include_directories(ccw_lua PUBLIC ${CMAKE_SOURCE_DIR}/third_party/lua)
ccw_vendored_target(ccw_lua)
if(CCW_M_LIB)
  target_link_libraries(ccw_lua PUBLIC ${CCW_M_LIB})
endif()
add_library(ccw_sched STATIC sched.c sched_lua.c)
target_include_directories(ccw_sched PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/oeuph
  ${CMAKE_SOURCE_DIR}/third_party/lua
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(ccw_sched PUBLIC ccw_lua ccw_oeuph)
add_executable(ccw-sched ccw_sched.c)
target_link_libraries(ccw-sched PRIVATE ccw_sched)
