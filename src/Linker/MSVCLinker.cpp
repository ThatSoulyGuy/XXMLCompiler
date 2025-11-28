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

        // Validate it's actually MSVC link.exe and not Unix link
        if (!linkerPath.empty()) {
            // Reject Unix-style paths (Git Bash /usr/bin/link, etc.)
            // Check both forward and backslash variants since Git on Windows uses backslashes
            if (linkerPath.find("/usr/") != std::string::npos ||
                linkerPath.find("/bin/") != std::string::npos ||
                linkerPath.find("/mingw") != std::string::npos ||
                linkerPath.find("\\usr\\") != std::string::npos ||
                linkerPath.find("\\bin\\link") != std::string::npos ||
                linkerPath.find("\\Git\\") != std::string::npos ||
                linkerPath.find("\\mingw") != std::string::npos ||
                linkerPath.find("\\msys") != std::string::npos) {
                // Unix-style path, not MSVC - continue searching
                linkerPath.clear();
            }
            // Check if it looks like MSVC path
            else if (linkerPath.find("Microsoft Visual Studio") != std::string::npos ||
                     linkerPath.find("VisualStudio") != std::string::npos ||
                     linkerPath.find("MSVC") != std::string::npos ||
                     (linkerPath.find(":\\") != std::string::npos &&
                      linkerPath.find("link.exe") != std::string::npos)) {
                // Looks like MSVC link
                return linkerPath;
            } else {
                // Unknown path format, reject to be safe
                linkerPath.clear();
            }
        }

        // Try common Visual Studio installation base paths
        std::vector<std::string> vsPaths = {
            // VS 2026 Preview / non-standard installations
            "D:\\VisualStudio\\Installs\\2026\\VC\\Tools\\MSVC",
            // VS 18 (2025) locations
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC",
            // VS 2022 locations
            "D:\\VisualStudio\\Installs\\2022\\Community\\VC\\Tools\\MSVC",
            "D:\\VisualStudio\\Installs\\2022\\Professional\\VC\\Tools\\MSVC",
            "D:\\VisualStudio\\Installs\\2022\\Enterprise\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC",
            // VS 2019 locations
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Tools\\MSVC",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Tools\\MSVC",
            // Build Tools (standalone)
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC"
        };

        // Try to find any version in VS paths
        for (const auto& vsPath : vsPaths) {
            if (!ProcessUtils::fileExists(vsPath)) {
                continue;
            }

            // Try to enumerate MSVC versions dynamically
            // Common version patterns: 14.XX.XXXXX
            std::vector<std::string> versions = {
                "14.50.35717",  // VS 2026 Preview
                "14.44.35207", "14.44.35219", "14.43.35207", "14.42.34433",
                "14.41.34120", "14.40.33807", "14.39.33519", "14.38.33135",
                "14.37.32822", "14.36.32532", "14.35.32215", "14.34.31933",
                "14.33.31629", "14.32.31326", "14.31.31103", "14.30.30705",
                "14.29.30133", "14.28.29910"
            };

            for (const auto& ver : versions) {
                std::string linkPath = vsPath + "\\" + ver + "\\bin\\Hostx64\\x64\\link.exe";
                if (ProcessUtils::fileExists(linkPath)) {
                    return linkPath;
                }
                // Also try x86 hosted tools
                linkPath = vsPath + "\\" + ver + "\\bin\\Hostx86\\x64\\link.exe";
                if (ProcessUtils::fileExists(linkPath)) {
                    return linkPath;
                }
            }
        }

        return "";
    }

    // Extract library paths from link.exe path
    static std::vector<std::string> getLibraryPaths(const std::string& linkerPath) {
        using namespace Utils;
        std::vector<std::string> paths;

        // link.exe is typically at: .../VC/Tools/MSVC/<version>/bin/Hostx64/x64/link.exe
        // Libraries are at: .../VC/Tools/MSVC/<version>/lib/x64/
        // We also need Windows SDK libs

        // Find MSVC lib path from linker path
        size_t pos = linkerPath.find("\\bin\\");
        if (pos != std::string::npos) {
            std::string msvcBase = linkerPath.substr(0, pos);
            std::string msvcLibPath = msvcBase + "\\lib\\x64";
            if (ProcessUtils::fileExists(msvcLibPath)) {
                paths.push_back(msvcLibPath);
            }
        }

        // Try to find Windows SDK
        std::vector<std::string> sdkVersions = {
            "10.0.22621.0", "10.0.22000.0", "10.0.19041.0", "10.0.18362.0",
            "10.0.17763.0", "10.0.17134.0", "10.0.16299.0", "10.0.15063.0"
        };

        std::vector<std::string> sdkBases = {
            "C:\\Program Files (x86)\\Windows Kits\\10\\Lib",
            "C:\\Program Files\\Windows Kits\\10\\Lib"
        };

        for (const auto& sdkBase : sdkBases) {
            for (const auto& ver : sdkVersions) {
                std::string ucrtPath = sdkBase + "\\" + ver + "\\ucrt\\x64";
                std::string umPath = sdkBase + "\\" + ver + "\\um\\x64";
                if (ProcessUtils::fileExists(ucrtPath)) {
                    paths.push_back(ucrtPath);
                    if (ProcessUtils::fileExists(umPath)) {
                        paths.push_back(umPath);
                    }
                    goto found_sdk;
                }
            }
        }
        found_sdk:

        return paths;
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

        // DLL or executable mode
        if (config.createDLL) {
            args.push_back("/DLL");
            // DLLs don't need a subsystem specification
        } else {
            // Subsystem for executables
            if (config.createConsoleApp) {
                args.push_back("/SUBSYSTEM:CONSOLE");
            } else {
                args.push_back("/SUBSYSTEM:WINDOWS");
            }
        }

        // Object files
        for (const auto& obj : config.objectFiles) {
            args.push_back(obj);
        }

        // Add auto-detected system library paths
        auto systemLibPaths = getLibraryPaths(linkerPath);
        for (const auto& libPath : systemLibPaths) {
            args.push_back("/LIBPATH:" + libPath);
        }

        // User-specified library paths
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

        // Default Windows libraries and CRT
        args.push_back("kernel32.lib");
        args.push_back("user32.lib");
        args.push_back("ucrt.lib");           // Universal CRT
        args.push_back("msvcrt.lib");         // MSVC runtime
        args.push_back("vcruntime.lib");      // VC runtime

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

        // Machine architecture
        args.push_back("/MACHINE:X64");

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
            if (!result.output.empty()) {
                std::cerr << result.output << std::endl;
            }
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
