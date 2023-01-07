cmake_minimum_required(VERSION 3.15)
project(zero)
include(ExternalProject)
include(CheckSymbolExists)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE INTERNAL "" FORCE)
#set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE INTERNAL "" FORCE)

# doctest
enable_testing()

find_package(Git REQUIRED)
ExternalProject_Add(
        doctest
        PREFIX ${CMAKE_BINARY_DIR}/external/doctest
        GIT_REPOSITORY https://github.com/onqtam/doctest.git
        GIT_TAG v2.4.9
        TIMEOUT 10
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
)

ExternalProject_Get_Property(doctest source_dir)
set(DOCTEST_INCLUDE_DIR ${source_dir}/ CACHE INTERNAL "Path to include folder for doctest")

include_directories(${DOCTEST_INCLUDE_DIR})

# unit tests
add_executable( testing_build
        tests/tests.cpp
        tests/test_fibers.cpp
        tests/test_jobs.cpp
        )
check_symbol_exists(posix_memalign "stdlib.h" HAVE_POSIX_MEMALIGN_IN_STDLIB)

IF(HAVE_POSIX_MEMALIGN_IN_STDLIB)
    # We need HAVE_POSIX_MEMALIGN for the ifdefs to use posix_memalign
    # We defined HAVE_POSIX_MEMALIGN_IN_STDLIB in order to avoid including in malloc.h
    add_compile_definitions(HAVE_POSIX_MEMALIGN_IN_STDLIB HAVE_POSIX_MEMALIGN)
    MESSAGE("Found posix_memalign in stdlib.h -DHAVE_POSIX_MEMALIGN_IN_STDLIB -DHAVE_POSIX_MEMALIGN")
ENDIF(HAVE_POSIX_MEMALIGN_IN_STDLIB)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_options(/EHs)
endif()

# zero headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_dependencies( testing_build doctest )
add_test( NAME all_tests COMMAND testing_build )

set_target_properties(testing_build PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED 17
    CXX_EXTENSIONS ON
)