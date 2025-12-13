// main.cpp - XXML Language Server Entry Point
// XXML Language Server Protocol Implementation

#include "LSPServer.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    std::vector<std::string> includePaths;

    // Parse command-line arguments
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
                      << "  --version, -v        Show version information\n"
                      << "  --help, -h           Show this help message\n"
                      << "  -I <dir>             Add directory to import search path\n"
                      << "  --include-dir=<dir>  Same as -I <dir>\n\n"
                      << "The language server communicates via JSON-RPC over stdio.\n"
                      << "It is designed to be launched by an IDE/editor.\n";
            return 0;
        }
        if (arg == "-I" && i + 1 < argc) {
            // -I <dir> - add import search path (with space)
            includePaths.push_back(argv[i + 1]);
            ++i;  // Skip the next argument
        } else if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            // -I<dir> or -I"<dir>" - add import search path (no space)
            std::string dir = arg.substr(2);
            // Remove surrounding quotes if present
            if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') {
                dir = dir.substr(1, dir.size() - 2);
            }
            if (!dir.empty()) {
                includePaths.push_back(dir);
            }
        } else if (arg.rfind("--include-dir=", 0) == 0) {
            // --include-dir=<dir> - add import search path
            std::string dir = arg.substr(14);
            // Remove surrounding quotes if present
            if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') {
                dir = dir.substr(1, dir.size() - 2);
            }
            if (!dir.empty()) {
                includePaths.push_back(dir);
            }
        }
    }

    try {
        xxml::lsp::LSPServer server;

        // Pass include paths to server
        for (const auto& path : includePaths) {
            server.addIncludePath(path);
        }

        return server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
