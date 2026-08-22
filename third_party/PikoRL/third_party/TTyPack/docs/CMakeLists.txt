find_package(Doxygen 1.9 REQUIRED dot)

set(TTYPACK_DOXYFILE "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
    "${TTYPACK_DOXYFILE}"
    @ONLY
)

add_custom_target(ttypack-docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${TTYPACK_DOXYFILE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/html/index.html"
    COMMENT "Generating TTyPack API documentation"
    VERBATIM
)

set(TTYPACK_DOCS_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/html"
)

install(
    DIRECTORY "${TTYPACK_DOCS_OUTPUT}/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/html"
    OPTIONAL
)
