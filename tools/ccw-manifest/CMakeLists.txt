# ccw-manifest: generates manifests/ from live kernel capabilities (§4.2).
add_executable(ccw-manifest ccw_manifest.c)
target_link_libraries(ccw-manifest PRIVATE ccw_host_glue ccw_executor_s7)
