# CCWeave — global build specification (spec §8).
cmake_minimum_required(VERSION 3.16)
project(ccweave C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra)

option(CCWEAVE_ENABLE_ASAN "Build with AddressSanitizer/LeakSanitizer" OFF)
option(CCWEAVE_ENABLE_TREESITTER "Build Swaff Tree-sitter frontends" OFF)
option(CCWEAVE_BUILD_DOCS "Build Doxygen documentation" ON)

if(CCWEAVE_ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

set(CCWEAVE_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- vendored dependencies (pinned in third_party/VERSIONS.lock) ---
# Vendored sources are never edited, so they are compiled without our
# warning and sanitizer flags; sanitizers still cover all CCWeave code.
function(ccw_vendored_target target)
  target_compile_options(${target} PRIVATE -w)
  if(CCWEAVE_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fno-sanitize=undefined)
  endif()
endfunction()

add_library(ccw_mpc STATIC ${CCWEAVE_THIRD_PARTY}/mpc/mpc.c)
target_include_directories(ccw_mpc PUBLIC ${CCWEAVE_THIRD_PARTY}/mpc)
ccw_vendored_target(ccw_mpc)

add_library(ccw_s7 STATIC ${CCWEAVE_THIRD_PARTY}/s7/s7.c)
target_include_directories(ccw_s7 PUBLIC ${CCWEAVE_THIRD_PARTY}/s7)
# s7.c includes "mus-config.h" and uses struct timezone, so it needs the
# GNU dialect. Vendored source is never edited, so we adjust the target.
target_include_directories(ccw_s7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/executors/s7)
set_target_properties(ccw_s7 PROPERTIES C_EXTENSIONS ON)
target_compile_definitions(ccw_s7 PUBLIC WITH_C_LOADER=0 WITH_SYSTEM_EXTRAS=0)
ccw_vendored_target(ccw_s7)
find_library(CCW_M_LIB m)
if(CCW_M_LIB)
  target_link_libraries(ccw_s7 PUBLIC ${CCW_M_LIB})
endif()
find_package(Threads REQUIRED)
target_link_libraries(ccw_s7 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_subdirectory(glue)
add_subdirectory(ir)
add_subdirectory(executors/s7)
add_subdirectory(oeuph)
add_subdirectory(kliche)
add_subdirectory(swaff)
add_subdirectory(docs)
add_subdirectory(tools/ccw-manifest)

enable_testing()
add_subdirectory(tests)
