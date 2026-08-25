set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(llama QUIET)

if (NOT llama_FOUND)
	set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
	set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

# llama.cpp lives outside this directory, so an explicit binary dir is required
	add_subdirectory(
	    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/llama.cpp
	    ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp
	)
endif()

add_executable(mkprojmap mkprojmap.cpp)
target_link_libraries(mkprojmap PRIVATE llama)

