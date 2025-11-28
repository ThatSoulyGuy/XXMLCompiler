#include "Linker/LinkerInterface.h"
#include "Utils/ProcessUtils.h"
#include <iostream>
#include <sstream>

namespace XXML {
namespace Linker {

/**
 * GNU linker implementation (uses gcc/g++ as linker driver)
 */
class GNULinker : public ILinker {
public:
    std::string name() const override {
        return "GNU Linker (gcc/clang)";
    }

    bool isAvailable() const override {
        using namespace Utils;
        // Try to find gcc, g++, or clang
        if (!ProcessUtils::findInPath("gcc").empty()) return true;
        if (!ProcessUtils::findInPath("g++").empty()) return true;
        if (!ProcessUtils::findInPath("clang").empty()) return true;
        return false;
    }

    std::string objectFileExtension() const override {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
        return ".obj";  // Windows prefers .obj, though .o also works
#else
        return ".o";
#endif
    }

    std::string executableExtension() const override {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
        return ".exe";
#else
        return "";  // Unix executables typically have no extension
#endif
    }

    LinkResult link(const LinkConfig& config) override {
        using namespace Utils;
        LinkResult result;

        // Find a suitable linker driver (prefer gcc, then clang)
        std::string linkerPath;
        if (!ProcessUtils::findInPath("gcc").empty()) {
            linkerPath = "gcc";
        } else if (!ProcessUtils::findInPath("clang").empty()) {
            linkerPath = "clang";
        } else if (!ProcessUtils::findInPath("g++").empty()) {
            linkerPath = "g++";
        }

        if (linkerPath.empty()) {
            result.error = "Error: No suitable linker found (gcc, g++, or clang)";
            return result;
        }

        // Build arguments
        std::vector<std::string> args;

        // Shared library/DLL mode
        if (config.createDLL) {
            args.push_back("-shared");
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
            // On Windows/MinGW, also need to export all symbols or use declspec
            args.push_back("-Wl,--export-all-symbols");
#else
            // On Unix, position-independent code is required for shared libraries
            args.push_back("-fPIC");
#endif
        }

        // Object files (must come first for GNU ld)
        for (const auto& obj : config.objectFiles) {
            args.push_back(obj);
        }

        // Output file
        args.push_back("-o");
        args.push_back(config.outputPath);

        // Library paths
        for (const auto& libPath : config.libraryPaths) {
            args.push_back("-L" + libPath);
        }

        // Libraries
        for (const auto& lib : config.libraries) {
            // Check if this is a full path to a library file
            if (lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos) {
                // Full path - use it directly
                args.push_back(lib);
            } else {
                // Library name only - convert to -l flag
                std::string libName = lib;
                // Remove "lib" prefix if present
                if (libName.find("lib") == 0) {
                    libName = libName.substr(3);
                }
                // Remove extensions if present
                if (libName.find(".a") != std::string::npos) {
                    libName = libName.substr(0, libName.find(".a"));
                }
                if (libName.find(".so") != std::string::npos) {
                    libName = libName.substr(0, libName.find(".so"));
                }
                args.push_back("-l" + libName);
            }
        }

        // Platform-specific standard libraries
#if defined(__APPLE__)
        // macOS: Link against system frameworks
        args.push_back("-lSystem");
#elif defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
        // Windows/MinGW: Standard C library is implicit, no -ldl needed
        // Add basic Windows libraries and GCC runtime (for __chkstk, etc.)
        args.push_back("-lkernel32");
        args.push_back("-lmsvcrt");
        args.push_back("-static-libgcc");  // Statically link GCC runtime (provides __chkstk)
        args.push_back("-lgcc");  // Provides __chkstk and other runtime functions
        // Note: -lgcc_eh (exception handling) is not always available in MinGW
        // and not needed for C code without exceptions
#else
        // Linux/Unix: Standard C library and math/dl
        args.push_back("-lc");
        args.push_back("-lm");
        args.push_back("-ldl");
#endif

        // Static vs dynamic linking
        if (config.staticLink) {
            args.push_back("-static");
        }

        // Optimization/debug settings
        if (config.optimizationLevel == 0) {
            args.push_back("-g");  // Include debug symbols
        } else {
            args.push_back("-O" + std::to_string(config.optimizationLevel));
        }

        // Additional flags
        for (const auto& flag : config.additionalFlags) {
            args.push_back(flag);
        }

        // Print command if verbose
        if (config.verbose) {
            std::cout << "Linker command: " << linkerPath;
            for (const auto& arg : args) {
                std::cout << " " << arg;
            }
            std::cout << std::endl;
        }

        // Execute linker
        ProcessResult procResult = ProcessUtils::execute(linkerPath, args);

        result.success = procResult.success;
        result.exitCode = procResult.exitCode;
        result.output = procResult.output;
        result.error = procResult.error;
        result.outputPath = config.outputPath;

        if (!result.success) {
            std::cerr << "GNU Linker failed with exit code " << result.exitCode << std::endl;
            if (!result.error.empty()) {
                std::cerr << result.error << std::endl;
            }
        }

        return result;
    }
};

// Factory function for GNU linker
std::unique_ptr<ILinker> createGNULinker() {
    return std::make_unique<GNULinker>();
}

} // namespace Linker
} // namespace XXML
