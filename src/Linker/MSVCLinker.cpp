#include "Linker/LinkerInterface.h"
#include "Utils/ProcessUtils.h"
#include <iostream>
#include <sstream>

namespace XXML {
namespace Linker {

/**
 * MSVC linker implementation (link.exe)
 */
class MSVCLinker : public ILinker {
public:
    std::string name() const override {
        return "MSVC Linker (link.exe)";
    }

    bool isAvailable() const override {
        using namespace Utils;
        std::string linkerPath = ProcessUtils::findInPath("link.exe");
        return !linkerPath.empty();
    }

    std::string objectFileExtension() const override {
        return ".obj";
    }

    std::string executableExtension() const override {
        return ".exe";
    }

    LinkResult link(const LinkConfig& config) override {
        using namespace Utils;
        LinkResult result;

        // Find link.exe
        std::string linkerPath = ProcessUtils::findInPath("link.exe");
        if (linkerPath.empty()) {
            result.error = "Error: link.exe not found in PATH. Please run from Visual Studio Developer Command Prompt.";
            return result;
        }

        // Build arguments
        std::vector<std::string> args;

        // Output file
        args.push_back("/OUT:" + config.outputPath);

        // Subsystem
        if (config.createConsoleApp) {
            args.push_back("/SUBSYSTEM:CONSOLE");
        } else {
            args.push_back("/SUBSYSTEM:WINDOWS");
        }

        // Object files
        for (const auto& obj : config.objectFiles) {
            args.push_back(obj);
        }

        // Library paths
        for (const auto& libPath : config.libraryPaths) {
            args.push_back("/LIBPATH:" + libPath);
        }

        // Libraries
        for (const auto& lib : config.libraries) {
            // MSVC expects .lib extension
            std::string libName = lib;
            if (libName.find(".lib") == std::string::npos) {
                libName += ".lib";
            }
            args.push_back(libName);
        }

        // Default Windows libraries
        args.push_back("kernel32.lib");
        args.push_back("user32.lib");

        // Optimization/debug settings
        if (config.optimizationLevel == 0) {
            args.push_back("/DEBUG");
        } else {
            args.push_back("/OPT:REF");
            args.push_back("/OPT:ICF");
        }

        // Static vs dynamic runtime
        if (config.staticLink) {
            args.push_back("/MT");
        }

        // Additional flags
        for (const auto& flag : config.additionalFlags) {
            args.push_back(flag);
        }

        // Suppress logo
        args.push_back("/NOLOGO");

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
            std::cerr << "MSVC Linker failed with exit code " << result.exitCode << std::endl;
            if (!result.error.empty()) {
                std::cerr << result.error << std::endl;
            }
        }

        return result;
    }
};

// Factory function for MSVC linker
std::unique_ptr<ILinker> createMSVCLinker() {
    return std::make_unique<MSVCLinker>();
}

} // namespace Linker
} // namespace XXML
