cmake_minimum_required(VERSION 3.16)

project(ExolangTk VERSION 0.1.0 LANGUAGES C)

option(EXOLANGTK_BUILD_TESTS "Build ExolangTk smoke tests" OFF)
option(EXOLANGTK_BUILD_DOCS "Build Doxygen documentation" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(ExolangTk INTERFACE)
target_include_directories(ExolangTk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

add_subdirectory(include)

if(EXOLANGTK_BUILD_DOCS)
    add_subdirectory(docs)
endif()

if(EXOLANGTK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS ExolangTk EXPORT ExolangTkTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ExolangTkTargets
    FILE ExolangTkTargets.cmake
    NAMESPACE ExolangTk::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ExolangTk)
