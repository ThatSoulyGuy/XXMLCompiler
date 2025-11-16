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
        std::string linkerPath = findMSVCLinker();
        return !linkerPath.empty();
    }

private:
    // Find MSVC link.exe, avoiding Unix /usr/bin/link
    static std::string findMSVCLinker() {
        using namespace Utils;

        // First try finding link.exe in PATH
        std::string linkerPath = ProcessUtils::findInPath("link.exe");

        // Check if it's actually MSVC link.exe and not Unix link
        // Unix link is usually /usr/bin/link, MSVC is usually in "Microsoft Visual Studio"
        if (!linkerPath.empty()) {
            // On Windows with Git Bash, /usr/bin/link might be found first
            // Check if it contains "Microsoft Visual Studio" or ends with link.exe
            if (linkerPath.find("Microsoft Visual Studio") != std::string::npos ||
                linkerPath.find("\\link.exe") != std::string::npos ||
                linkerPath.find("/link.exe") != std::string::npos) {
                // Looks like MSVC link
                return linkerPath;
            }

            // Found Unix link, not what we want - continue searching
        }

        // Try common specific paths that we know exist
        std::vector<std::string> fullPaths = {
            "D:\\VisualStudio\\Installs\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\link.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\link.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\link.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\link.exe"
        };

        for (const auto& fullPath : fullPaths) {
            if (ProcessUtils::fileExists(fullPath)) {
                return fullPath;
            }
        }

        // Try common Visual Studio installation base paths
        std::vector<std::string> vsPaths = {
            "D:\\VisualStudio\\Installs\\2022\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Tools\\MSVC",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Tools\\MSVC"
        };

        for (const auto& vsPath : vsPaths) {
            if (!ProcessUtils::fileExists(vsPath)) {
                continue;
            }

            // Try common version numbers
            std::vector<std::string> versions = {"14.44.35207", "14.44.35219", "14.43", "14.42", "14.41", "14.40"};
            for (const auto& ver : versions) {
                std::string linkPath = vsPath + "\\" + ver + "\\bin\\Hostx64\\x64\\link.exe";
                if (ProcessUtils::fileExists(linkPath)) {
                    return linkPath;
                }
            }
        }

        return "";
    }

public:

    std::string objectFileExtension() const override {
        return ".obj";
    }

    std::string executableExtension() const override {
        return ".exe";
    }

    LinkResult link(const LinkConfig& config) override {
        using namespace Utils;
        LinkResult result;

        // Find link.exe (use our custom finder to avoid Unix link)
        std::string linkerPath = findMSVCLinker();
        if (linkerPath.empty()) {
            result.error = "Error: MSVC link.exe not found. Please install Visual Studio or run from Developer Command Prompt.";
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
