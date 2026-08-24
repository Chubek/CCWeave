# Parthia Standard ML compiler/interpreter façade (§2, §8).

add_library(sml_parthia STATIC
  src/sml_parthia.c
  src/parthia_sexp.c
  src/parthia_compile.c
  src/parthia_eval.c
  src/parthia_basis.c)
target_include_directories(sml_parthia PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/swaff)
target_include_directories(sml_parthia PRIVATE
  ${CMAKE_SOURCE_DIR}/glue
  ${CMAKE_SOURCE_DIR}/third_party/sfsexp/src
  ${CMAKE_SOURCE_DIR}/third_party/dyncall/dyncall)
target_link_libraries(sml_parthia PUBLIC ccw_swaff ccw_klib ccw_sfsexp
  ccw_oeuph ccw_dynalo dyncall_s)
target_compile_definitions(sml_parthia PRIVATE
  SML_PARTHIA_SEMA_SALVO_DIR="${CMAKE_SOURCE_DIR}/sema-salvo")
target_link_libraries(sml_parthia PUBLIC ccw_sched)
if(CMAKE_DL_LIBS)
  target_link_libraries(sml_parthia PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(sml-parthia
  cli/main.cpp
  cli/repl.cpp)
target_include_directories(sml-parthia PRIVATE
  ${CCWEAVE_THIRD_PARTY}/PikoRL/third_party/QaMRpp/include
  ${CCWEAVE_THIRD_PARTY}/PikoRL/third_party/QaMRpp/plugins
  ${CCWEAVE_THIRD_PARTY}/PikoRL/third_party/TTyPack/include
  ${CCWEAVE_THIRD_PARTY}/PikoRL/third_party/QaMRpp/third_party/SerdeTk)
target_compile_features(sml-parthia PRIVATE cxx_std_17)
set_target_properties(sml-parthia PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
target_link_libraries(sml-parthia PRIVATE sml_parthia)

add_executable(test_sml_parthia
  tests/test_sml_parthia.c)
target_include_directories(test_sml_parthia PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sml_parthia PRIVATE sml_parthia)
add_dependencies(test_sml_parthia sml_basis_native)
target_compile_definitions(test_sml_parthia PRIVATE
  CCW_SML_BASIS_NATIVE_PATH="${CMAKE_BINARY_DIR}/stdlib-salvo/sml-basis/libsml_basis.so")

add_executable(test_sml_parthia_sched
  tests/test_sml_parthia_sched.c)
target_include_directories(test_sml_parthia_sched PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/sched)
target_compile_definitions(test_sml_parthia_sched PRIVATE
  CCW_SML_PARTHIA_SCHED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sched"
  CCW_SML_PARTHIA_MANIFEST_DIR="${CMAKE_SOURCE_DIR}/manifests")
target_link_libraries(test_sml_parthia_sched PRIVATE sml_parthia)

# Installation: the real binary lives in libexec; bin/ receives a launcher
# that appends the installed SML Basis Salvo directory (installed by
# stdlib-salvo/sml-basis) to the comma-separated SML_PARTHIA_PATH.
configure_file(cli/sml-parthia.in
               "${CMAKE_CURRENT_BINARY_DIR}/sml-parthia-launcher"
               @ONLY)
install(TARGETS sml-parthia
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/ccweave)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/sml-parthia-launcher"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME sml-parthia)
