cmake_minimum_required(VERSION 3.20)
project(XXMLCompiler VERSION 2.0 LANGUAGES CXX)

# Set C++20 standard for modern features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options for optional features
option(XXML_ENABLE_LLVM_BACKEND "Enable LLVM IR backend" OFF)
option(XXML_USE_MODULES "Use C++20 modules (experimental)" OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Common library
set(COMMON_SOURCES
    src/Common/Error.cpp
)

add_library(XXMLCommon STATIC ${COMMON_SOURCES})
target_include_directories(XXMLCommon PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Core library (registries, interfaces, concepts)
set(CORE_SOURCES
    src/Core/TypeRegistry.cpp
    src/Core/OperatorRegistry.cpp
    src/Core/BackendRegistry.cpp
    src/Core/CompilationContext.cpp
    src/Core/TypeContext.cpp
    src/Core/CppTypeModel.cpp
)

add_library(XXMLCore STATIC ${CORE_SOURCES})
target_link_libraries(XXMLCore PUBLIC XXMLCommon)
target_include_directories(XXMLCore PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Lexer library
set(LEXER_SOURCES
    src/Lexer/Token.cpp
    src/Lexer/TokenType.cpp
    src/Lexer/Lexer.cpp
)

add_library(XXMLLexer STATIC ${LEXER_SOURCES})
target_link_libraries(XXMLLexer PUBLIC XXMLCommon)
target_include_directories(XXMLLexer PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Parser library
set(PARSER_SOURCES
    src/Parser/AST.cpp
    src/Parser/ASTClone.cpp
    src/Parser/Parser.cpp
)

add_library(XXMLParser STATIC ${PARSER_SOURCES})
target_link_libraries(XXMLParser PUBLIC XXMLLexer XXMLCommon)
target_include_directories(XXMLParser PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Semantic library
set(SEMANTIC_SOURCES
    src/Semantic/SymbolTable.cpp
    src/Semantic/SemanticAnalyzer.cpp
)

add_library(XXMLSemantic STATIC ${SEMANTIC_SOURCES})
target_link_libraries(XXMLSemantic PUBLIC XXMLParser XXMLCommon)
target_include_directories(XXMLSemantic PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Import library
set(IMPORT_SOURCES
    src/Import/Module.cpp
    src/Import/ImportResolver.cpp
    src/Import/DependencyGraph.cpp
)

add_library(XXMLImport STATIC ${IMPORT_SOURCES})
target_link_libraries(XXMLImport PUBLIC XXMLParser XXMLSemantic XXMLCommon)
target_include_directories(XXMLImport PUBLIC ${CMAKE_SOURCE_DIR}/include)

# CodeGen library
set(CODEGEN_SOURCES
    src/CodeGen/CodeGenerator.cpp
)

add_library(XXMLCodeGen STATIC ${CODEGEN_SOURCES})
target_link_libraries(XXMLCodeGen PUBLIC XXMLParser XXMLCore XXMLCommon)
target_include_directories(XXMLCodeGen PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Backends library (C++, LLVM, etc.)
set(BACKENDS_SOURCES
    src/Backends/BackendBase.cpp
    src/Backends/Cpp20Backend.cpp
)

# Add LLVM backend if enabled
if(XXML_ENABLE_LLVM_BACKEND)
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    list(APPEND BACKENDS_SOURCES src/Backends/LLVMBackend.cpp)
    add_definitions(-DXXML_LLVM_ENABLED)
endif()

add_library(XXMLBackends STATIC ${BACKENDS_SOURCES})
target_link_libraries(XXMLBackends PUBLIC XXMLCodeGen XXMLCore XXMLParser XXMLCommon)
target_include_directories(XXMLBackends PUBLIC ${CMAKE_SOURCE_DIR}/include)

if(XXML_ENABLE_LLVM_BACKEND)
    target_include_directories(XXMLBackends PUBLIC ${LLVM_INCLUDE_DIRS})
    target_link_libraries(XXMLBackends PUBLIC LLVM)
endif()

# Main compiler executable
add_executable(xxml src/main.cpp)
target_link_libraries(xxml
    XXMLBackends
    XXMLCodeGen
    XXMLSemantic
    XXMLImport
    XXMLCore
    XXMLParser
    XXMLLexer
    XXMLCommon
)

# Installation
install(TARGETS xxml DESTINATION bin)
install(DIRECTORY runtime/ DESTINATION share/xxml/runtime)

# Testing (optional)
enable_testing()

# Test compilation of Test.XXML
add_test(
    NAME TestXXMLCompilation
    COMMAND xxml ${CMAKE_SOURCE_DIR}/Test.XXML ${CMAKE_BINARY_DIR}/test_output.cpp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Print configuration
message(STATUS "=== XXML Compiler Configuration ===")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  LLVM Backend: ${XXML_ENABLE_LLVM_BACKEND}")
message(STATUS "  C++20 Modules: ${XXML_USE_MODULES}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "===================================")
