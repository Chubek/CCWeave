# CCWeave documentation build — Doxygen (spec: docs are not normative but
# must be buildable from every checkout).

option(CCWEAVE_BUILD_DOCS "Build Doxygen documentation" ON)

if(NOT CCWEAVE_BUILD_DOCS)
  message(STATUS "CCWeave docs: skipped (CCWEAVE_BUILD_DOCS=OFF)")
  return()
endif()

find_package(Doxygen QUIET)

if(NOT DOXYGEN_FOUND)
  message(WARNING "CCWeave docs: Doxygen not found — 'docs' target unavailable."
                  " Install Doxygen or set CCWEAVE_BUILD_DOCS=OFF to suppress this warning.")
  return()
endif()

# --- configuration variables (substituted into Doxyfile.in) ---

set(CCWEAVE_DOCS_PROJECT_NAME  "${PROJECT_NAME}")
set(CCWEAVE_DOCS_VERSION       "${PROJECT_VERSION}")
set(CCWEAVE_DOCS_OUTPUT_DIR    "${CMAKE_CURRENT_BINARY_DIR}")
set(CCWEAVE_DOCS_SOURCE_ROOT   "${CMAKE_SOURCE_DIR}")
set(CCWEAVE_DOCS_LAYOUT_FILE   "${CMAKE_CURRENT_SOURCE_DIR}/layout.xml")

# Inputs: every source directory that contains public headers or
# documented Markdown.  Vendored code and build trees are excluded
# inside Doxyfile.in.
set(CCWEAVE_DOCS_INPUTS
    "${CMAKE_SOURCE_DIR}/glue"
    "${CMAKE_SOURCE_DIR}/ir"
    "${CMAKE_SOURCE_DIR}/executors/s7"
    "${CMAKE_SOURCE_DIR}/oeuph"
    "${CMAKE_SOURCE_DIR}/kliche"
    "${CMAKE_SOURCE_DIR}/swaff/ccw_swaff.h"
    "${CMAKE_SOURCE_DIR}/swaff/adapters"
    "${CMAKE_SOURCE_DIR}/tools"
    "${CMAKE_SOURCE_DIR}/kernels"
    "${CMAKE_SOURCE_DIR}/stdrewrite"
    "${CMAKE_SOURCE_DIR}/docs"
)
# Doxygen separates INPUT paths with whitespace, while CMake stores lists
# with semicolons. All project paths are space-free by construction.
string(REPLACE ";" " " CCWEAVE_DOCS_INPUTS "${CCWEAVE_DOCS_INPUTS}")

# Optional features
option(CCWEAVE_DOCS_GENERATE_MAN "Generate man pages alongside HTML" ON)
if(CCWEAVE_DOCS_GENERATE_MAN)
  set(CCWEAVE_DOCS_GENERATE_MAN YES)
else()
  set(CCWEAVE_DOCS_GENERATE_MAN NO)
endif()

option(CCWEAVE_DOCS_WARN_AS_ERROR "Treat Doxygen warnings as errors" OFF)
if(CCWEAVE_DOCS_WARN_AS_ERROR)
  set(CCWEAVE_DOCS_WARN_AS_ERROR YES)
else()
  set(CCWEAVE_DOCS_WARN_AS_ERROR NO)
endif()

option(CCWEAVE_DOCS_HAVE_DOT "Use Graphviz dot for diagrams" OFF)
find_package(DOT QUIET)
if(CCWEAVE_DOCS_HAVE_DOT AND DOT_FOUND)
  set(CCWEAVE_DOCS_HAVE_DOT YES)
  set(DOXYGEN_DOT_FOUND YES)
else()
  set(CCWEAVE_DOCS_HAVE_DOT NO)
endif()

# --- generate Doxyfile ---
set(DOXYFILE_IN  "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
set(DOXYFILE_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

configure_file("${DOXYFILE_IN}" "${DOXYFILE_OUT}" @ONLY)

# --- docs target ---
add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} "${DOXYFILE_OUT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating CCWeave documentation"
    VERBATIM
)

# --- install ---
if(CCWEAVE_DOCS_GENERATE_MAN)
  install(DIRECTORY "${CCWEAVE_DOCS_OUTPUT_DIR}/html"
          DESTINATION "${CMAKE_INSTALL_DOCDIR}/html"
          OPTIONAL)
  install(DIRECTORY "${CCWEAVE_DOCS_OUTPUT_DIR}/man"
          DESTINATION "${CMAKE_INSTALL_MANDIR}"
          OPTIONAL)
else()
  install(DIRECTORY "${CCWEAVE_DOCS_OUTPUT_DIR}/html"
          DESTINATION "${CMAKE_INSTALL_DOCDIR}/html"
          OPTIONAL)
endif()
