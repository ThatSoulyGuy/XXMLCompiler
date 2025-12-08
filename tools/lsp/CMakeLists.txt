# CMakeLists.txt - XXML Language Server
# Build configuration for xxml-lsp executable

cmake_minimum_required(VERSION 3.16)

# LSP Server executable
add_executable(xxml-lsp
    main.cpp
    LSPServer.cpp
    JsonRpc.cpp
    DocumentManager.cpp
    AnalysisEngine.cpp
)

# Include directories
target_include_directories(xxml-lsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Link against XXML compiler library for full semantic analysis
target_link_libraries(xxml-lsp PRIVATE XXMLLib)

# C++23 standard (match main compiler)
target_compile_features(xxml-lsp PRIVATE cxx_std_23)

# Platform-specific settings
if(WIN32)
    # Windows-specific flags
    target_compile_definitions(xxml-lsp PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Install
install(TARGETS xxml-lsp
    RUNTIME DESTINATION bin
)
