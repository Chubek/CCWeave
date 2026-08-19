# Delphia Delphi/Object Pascal compiler (§1–§8).

add_library(delphia_driver STATIC driver/delphia_driver.c)
target_include_directories(delphia_driver PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/driver
  ${CMAKE_SOURCE_DIR}/swaff
  ${CMAKE_SOURCE_DIR}/ir
  ${CMAKE_SOURCE_DIR}/kliche
  ${CMAKE_SOURCE_DIR}/third_party/klib)
target_link_libraries(delphia_driver PUBLIC ccw_swaff ccw_ir ccw_kliche ccw_klib)

add_executable(delphia driver/main.c)
target_link_libraries(delphia PRIVATE delphia_driver)
