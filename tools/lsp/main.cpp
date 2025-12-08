// main.cpp - XXML Language Server Entry Point
// XXML Language Server Protocol Implementation

#include "LSPServer.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Check for version flag
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "xxml-lsp 0.1.0" << std::endl;
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "XXML Language Server\n\n"
                      << "Usage: xxml-lsp [options]\n\n"
                      << "Options:\n"
                      << "  --version, -v    Show version information\n"
                      << "  --help, -h       Show this help message\n\n"
                      << "The language server communicates via JSON-RPC over stdio.\n"
                      << "It is designed to be launched by an IDE/editor.\n";
            return 0;
        }
    }

    try {
        xxml::lsp::LSPServer server;
        return server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
