cmake_minimum_required(VERSION 3.20)

project(
    TTyPack
    VERSION 0.1.0
    DESCRIPTION "Header-only C++20 terminal libraries"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(
    TTYPACK_BUILD_TESTS
    "Build the standalone TTyPack test programs"
    "${BUILD_TESTING}"
)
option(
    TTYPACK_BUILD_DOCS
    "Add the Doxygen documentation target"
    OFF
)

add_library(TTyTk INTERFACE)
add_library(TTyPack::TTyTk ALIAS TTyTk)

target_compile_features(TTyTk INTERFACE cxx_std_20)
target_include_directories(
    TTyTk
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(TTyUtils INTERFACE)
add_library(TTyPack::TTyUtils ALIAS TTyUtils)

target_compile_features(TTyUtils INTERFACE cxx_std_20)
target_link_libraries(TTyUtils INTERFACE TTyTk)
target_include_directories(
    TTyUtils
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(TTYPACK_BUILD_TESTS)
    enable_testing()

    file(
        GLOB TEST_SOURCES
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/tests/*.test.cpp"
    )

    foreach(test_source IN LISTS TEST_SOURCES)
        get_filename_component(test_file "${test_source}" NAME)
        string(
            REGEX REPLACE
            "\\.test\\.cpp$"
            ""
            test_name
            "${test_file}"
        )
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" test_target "${test_name}")

        add_executable("${test_target}" "${test_source}")
        target_compile_features("${test_target}" PRIVATE cxx_std_20)
        target_compile_options(
            "${test_target}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )

        if(test_name MATCHES "^TTyUtils-")
            target_link_libraries("${test_target}" PRIVATE TTyUtils)
        else()
            target_link_libraries("${test_target}" PRIVATE TTyTk)
        endif()

        add_test(NAME "${test_target}" COMMAND "${test_target}")
    endforeach()
endif()

if(TTYPACK_BUILD_DOCS)
    add_subdirectory(docs)
endif()

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    FILES
        manifests/TTyTk-Manifest.yaml
        manifests/TTyUtils-Manifest.yaml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ttypack/manifests"
)
install(
    FILES
        README.md
        GUIDE.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(
    TARGETS TTyTk TTyUtils
    EXPORT TTyPackTargets
)
install(
    EXPORT TTyPackTargets
    FILE TTyPackTargets.cmake
    NAMESPACE TTyPack::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TTyPack"
)
