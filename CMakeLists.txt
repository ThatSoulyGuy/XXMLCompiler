cmake_minimum_required(VERSION 3.20)
project(XXMLCompiler VERSION 3.0.1 LANGUAGES CXX C)

# Set C++23 standard for modern features
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options for optional features
option(XXML_USE_MODULES "Use C++20 modules (experimental)" OFF)

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Prevent aggressive optimizations that can break code with subtle UB
    add_compile_options(-fno-strict-aliasing)

    # MinGW on Windows has issues with optimization - use O0 until UB is fixed
    # TODO: Find and fix the undefined behavior causing crashes with optimization
    if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS_RELEASE "-O0 -DNDEBUG" CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS_RELEASE "-O0 -DNDEBUG" CACHE STRING "" FORCE)
        message(STATUS "MinGW detected: Using -O0 for Release (UB workaround)")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        # For other compilers, use -O2 instead of -O3 for better stability
        string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        string(REPLACE "-O3" "-O2" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    endif()
endif()

if(MSVC)
    # MSVC specific flags
    add_compile_options(/W4 /permissive-)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)  # For generated headers

# Generate version header from template
configure_file(
    ${CMAKE_SOURCE_DIR}/include/Common/Version.h.in
    ${CMAKE_BINARY_DIR}/include/Common/Version.h
    @ONLY
)

# Auto-discover source files
file(GLOB_RECURSE XXML_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

# Auto-discover header files (for IDE support)
file(GLOB_RECURSE XXML_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/include/*.h
    ${CMAKE_SOURCE_DIR}/include/*.hpp
)

# Exclude main.cpp and test files from library sources
list(FILTER XXML_SOURCES EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER XXML_SOURCES EXCLUDE REGEX ".*IRTest\\.cpp$")

# Create the main compiler library
add_library(XXMLLib STATIC ${XXML_SOURCES} ${XXML_HEADERS})
target_include_directories(XXMLLib PUBLIC ${CMAKE_SOURCE_DIR}/include)

# XXML LLVM Runtime library (C runtime for LLVM IR generated code)
file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/runtime/*.c
)
add_library(XXMLLLVMRuntime STATIC ${RUNTIME_SOURCES})
target_include_directories(XXMLLLVMRuntime PUBLIC ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(XXMLLLVMRuntime PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# MSVC-specific runtime settings
if(MSVC)
    # Let CMake handle architecture automatically
    target_compile_options(XXMLLLVMRuntime PRIVATE /EHsc)
endif()

# Main compiler executable
add_executable(xxml ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_link_libraries(xxml PRIVATE XXMLLib)

# LSP Server (optional tooling)
option(XXML_BUILD_LSP "Build the XXML Language Server" ON)
if(XXML_BUILD_LSP)
    add_subdirectory(tools/lsp)
endif()

# Installation
install(TARGETS xxml DESTINATION bin)
install(TARGETS XXMLLib DESTINATION lib)
install(TARGETS XXMLLLVMRuntime DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/runtime/ DESTINATION share/xxml/runtime)

# Testing
enable_testing()
add_test(
    NAME TestXXMLCompilation
    COMMAND xxml ${CMAKE_SOURCE_DIR}/examples/Hello.XXML ${CMAKE_BINARY_DIR}/test_output.ll 2
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Print configuration
message(STATUS "=== XXML Compiler Configuration ===")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source files: ${CMAKE_SOURCE_DIR}/src/")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
list(LENGTH XXML_SOURCES SOURCE_COUNT)
message(STATUS "  Found ${SOURCE_COUNT} source files")
message(STATUS "===================================")
