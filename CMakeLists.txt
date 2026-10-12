# CMake build for CaDiCaL's library, as STP embeds it.
#
# CaDiCaL ships a shell `configure` that writes a makefile. Driving that means
# a POSIX shell, sed and make have to exist and be reachable, and CMake's
# ExternalProject invokes its steps through cmd.exe on Windows, which can run
# none of them -- "inappropriate file type or format" is what a native Windows
# build gets for its trouble.
#
# Ninety-odd source files and one generated header is little enough that
# describing the build here is easier than driving that one, and it builds
# CaDiCaL the way the rest of an embedding CMake build is built: with the
# compiler, launcher and toolchain file the parent is already using.
#
# Only the library is built and installed, with cadical.hpp under
# <include>/cadical/. `configure` remains the way to build the command-line
# solver and the model-based tester.
cmake_minimum_required(VERSION 3.18)
project(cadical C CXX)

include(CheckSymbolExists)
include(GNUInstallDirs)

# The release this is, for version.cpp. Read from the VERSION file that every
# release carries; a parent that knows better may say so.
if(NOT CADICAL_VERSION)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CADICAL_VERSION
         LIMIT_COUNT 1)
endif()
# The source revision, which `configure` reads from git. A source archive has
# no repository to ask, so a parent that fetched one by commit passes it in.
if(NOT CADICAL_IDENTIFIER)
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --show-prefix
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _cadical_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _cadical_git ERROR_QUIET)
        # A non-empty prefix is a source tree inside someone else's checkout,
        # whose HEAD says nothing about this one.
        if(_cadical_git EQUAL 0 AND _cadical_prefix STREQUAL "")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE CADICAL_IDENTIFIER
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()
    if(NOT CADICAL_IDENTIFIER)
        set(CADICAL_IDENTIFIER "unknown")
    endif()
endif()

# Everything except the two programs. `cadical` is the command-line solver and
# `mobical` its model-based tester; an embedder links neither, and mobical does
# not compile under MinGW at all.
file(GLOB CADICAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER CADICAL_SOURCES EXCLUDE REGEX "/(cadical|mobical)\\.cpp$")

# Kitten, the sub-solver CaDiCaL embeds for its sweeping and vivification, is
# C rather than C++ -- src/kitten.c, which the makefile compiles with $(CC).
# Miss it and the C++ half links with four undefined kitten_ symbols.
file(GLOB CADICAL_C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
list(APPEND CADICAL_SOURCES ${CADICAL_C_SOURCES})

# src/version.cpp includes build.hpp, which `configure` would have written.
# Generated here from what CMake already knows, rather than defining NBUILD and
# letting CaDiCaL guess.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/build.hpp"
"#define VERSION \"${CADICAL_VERSION}\"\n"
"#define IDENTIFIER \"${CADICAL_IDENTIFIER}\"\n"
"#define SHORTID \"${CADICAL_IDENTIFIER}\"\n"
"#define COMPILER \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\"\n"
"#define FLAGS \"${CMAKE_CXX_FLAGS}\"\n"
"#define DATE \"built with CMake\"\n")

add_library(cadical STATIC ${CADICAL_SOURCES})
target_include_directories(cadical PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(cadical PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(cadical PRIVATE cxx_std_11)
# What `configure` does without -g or -c: a library to link, not to debug
# CaDiCaL's own invariants in.
target_compile_definitions(cadical PRIVATE NDEBUG)

# The two probes `configure` makes. Without them CaDiCaL calls functions that
# a platform may not have -- getc_unlocked is absent on Windows, and closefrom
# on rather more.
check_symbol_exists(getc_unlocked "cstdio" HAVE_GETC_UNLOCKED)
if(NOT HAVE_GETC_UNLOCKED)
    target_compile_definitions(cadical PRIVATE NUNLOCKED)
endif()
check_symbol_exists(closefrom "fcntl.h" HAVE_CLOSEFROM)
if(NOT HAVE_CLOSEFROM)
    target_compile_definitions(cadical PRIVATE NCLOSEFROM)
endif()

install(TARGETS cadical ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/cadical.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cadical")
