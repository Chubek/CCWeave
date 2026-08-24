# Glue ABI support plus typed bridges to vendored kernel services.
add_library(ccw_glue STATIC
  ccw_val.c
  bridge/isl-bridge.c
  bridge/simde-bridge.c
  bridge/utf8proc-bridge.c)
target_include_directories(ccw_glue
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CCWEAVE_THIRD_PARTY}/simd-everywhere)
target_link_libraries(ccw_glue PUBLIC ccw_klib ccw_isl utf8proc)
target_compile_options(ccw_glue PRIVATE -Wno-psabi)

# hipSYCL GPU-accelerated backend (optional; kernels feature-test with gpu-has?)
option(CCWEAVE_ENABLE_HIPSYCL "Build hipSYCL GPU-accelerated Glue backend" OFF)
if(CCWEAVE_ENABLE_HIPSYCL)
  find_package(hipSYCL QUIET)
  if(hipSYCL_FOUND)
    add_library(ccw_hipsycl_backend STATIC bridge/hipsycl-bridge.cpp)
    target_include_directories(ccw_hipsycl_backend
      PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ccw_hipsycl_backend PUBLIC ccw_glue ccw_ir ccw_klib
                          hipSYCL::hipSYCL)
    target_compile_definitions(ccw_hipsycl_backend PUBLIC CCWEAVE_HAS_HIPSYCL=1)
  endif()
endif()

# Host accessor layer (Core Accessor Set); depends on the IR it exposes.
add_library(ccw_host_glue STATIC ccw_host_accessors.c)
target_include_directories(ccw_host_glue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccw_host_glue PUBLIC ccw_glue ccw_ir ccw_klib)
