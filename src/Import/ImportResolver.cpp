#include "../../include/Import/ImportResolver.h"
#include <filesystem>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace XXML {
namespace Import {

// Get the directory where the executable is located
static std::string getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string fullPath(buffer);
    size_t pos = fullPath.find_last_of("\\/");
    return (pos != std::string::npos) ? fullPath.substr(0, pos) : ".";
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        std::string fullPath(buffer);
        size_t pos = fullPath.find_last_of('/');
        return (pos != std::string::npos) ? fullPath.substr(0, pos) : ".";
    }
    return ".";
#endif
}

ImportResolver::ImportResolver() {
    // Find Language folder relative to executable
    std::string exeDir = getExecutableDirectory();
    std::string languagePath = exeDir + "/Language";

    // Check if Language folder exists next to executable
    if (fs::exists(languagePath) && fs::is_directory(languagePath)) {
        addSearchPath(languagePath);
        std::cout << "✓ Found standard library at: " << languagePath << "\n";
    } else {
        // Fallback: try relative to current directory
        if (fs::exists("Language") && fs::is_directory("Language")) {
            addSearchPath("Language");
            std::cout << "✓ Found standard library at: ./Language\n";
        } else {
            std::cerr << "Warning: Language folder not found (searched: "
                      << languagePath << " and ./Language)\n";
        }
    }

    // Current directory for user code
    addSearchPath(".");
}

void ImportResolver::addSearchPath(const std::string& path) {
    searchPaths.push_back(path);
}

std::string ImportResolver::namespaceToPath(const std::string& namespacePath) const {
    std::string path = namespacePath;
    // Replace "::" with "/"
    size_t pos = 0;
    while ((pos = path.find("::", pos)) != std::string::npos) {
        path.replace(pos, 2, "/");
        pos += 1;
    }
    return path;
}

std::vector<std::string> ImportResolver::findXXMLFilesInDirectory(const std::string& dirPath) const {
    std::vector<std::string> files;

    try {
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            return files;
        }

        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".XXML") {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Directory doesn't exist or can't be accessed
        return files;
    }

    return files;
}

std::vector<std::string> ImportResolver::findXXMLFilesRecursive(const std::string& dirPath) const {
    std::vector<std::string> files;

    try {
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            return files;
        }

        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".XXML") {
                    // Get the full path
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Directory doesn't exist or can't be accessed
        return files;
    }

    return files;
}

std::string ImportResolver::extractModuleName(const std::string& filePath) const {
    // Convert file path to module name
    // e.g., "Language/Core/String.XXML" -> "Language::Core::String"

    std::string moduleName = filePath;

    // Remove .XXML extension
    if (moduleName.length() > 5 && moduleName.substr(moduleName.length() - 5) == ".XXML") {
        moduleName = moduleName.substr(0, moduleName.length() - 5);
    }

    // Replace forward slashes with ::
    std::replace(moduleName.begin(), moduleName.end(), '/', ':');
    std::replace(moduleName.begin(), moduleName.end(), '\\', ':');

    // Replace single : with ::
    size_t pos = 0;
    while ((pos = moduleName.find(":", pos)) != std::string::npos) {
        if (pos + 1 < moduleName.length() && moduleName[pos + 1] != ':') {
            moduleName.insert(pos + 1, ":");
            pos += 2;
        } else {
            pos += 1;
        }
    }

    return moduleName;
}

std::vector<Module*> ImportResolver::resolveImport(const std::string& importPath) {
    std::vector<Module*> modules;

    // Convert import path to directory path
    std::string dirPath = namespaceToPath(importPath);

    // Search in all search paths
    for (const auto& searchPath : searchPaths) {
        std::string fullPath = searchPath;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
            fullPath += "/";
        }
        fullPath += dirPath;

        // Find all XXML files in this directory
        auto files = findXXMLFilesInDirectory(fullPath);

        for (const auto& filePath : files) {
            std::string moduleName = extractModuleName(filePath);

            // Check if module is already loaded
            if (hasModule(moduleName)) {
                modules.push_back(moduleCache[moduleName].get());
                continue;
            }

            // Create new module
            auto module = std::make_unique<Module>(moduleName, filePath);

            // Load source code
            if (!module->loadFromFile()) {
                std::cerr << "Warning: Failed to load module file: " << filePath << std::endl;
                continue;
            }

            Module* modulePtr = module.get();
            moduleCache[moduleName] = std::move(module);
            modules.push_back(modulePtr);
        }
    }

    return modules;
}

Module* ImportResolver::getModule(const std::string& moduleName) {
    auto it = moduleCache.find(moduleName);
    if (it != moduleCache.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool ImportResolver::hasModule(const std::string& moduleName) const {
    return moduleCache.find(moduleName) != moduleCache.end();
}

std::vector<Module*> ImportResolver::getAllModules() const {
    std::vector<Module*> modules;
    for (const auto& pair : moduleCache) {
        modules.push_back(pair.second.get());
    }
    return modules;
}

void ImportResolver::clear() {
    moduleCache.clear();
}

std::vector<Module*> ImportResolver::discoverAllModules() {
    std::vector<Module*> modules;

    // Discover all XXML files in current directory and subdirectories
    std::cout << "Auto-discovering XXML files in current directory...\n";

    // Find all XXML files recursively from current directory
    auto files = findXXMLFilesRecursive(".");

    for (const auto& filePath : files) {
        // Skip files in Language folder (they're handled separately)
        if (filePath.find("Language/") != std::string::npos ||
            filePath.find("Language\\") != std::string::npos) {
            continue;
        }

        // Skip build directories
        if (filePath.find("build/") != std::string::npos ||
            filePath.find("build\\") != std::string::npos ||
            filePath.find("x64/") != std::string::npos ||
            filePath.find("x64\\") != std::string::npos) {
            continue;
        }

        std::string moduleName = extractModuleName(filePath);

        // Check if module is already loaded
        if (hasModule(moduleName)) {
            modules.push_back(moduleCache[moduleName].get());
            continue;
        }

        // Create new module
        auto module = std::make_unique<Module>(moduleName, filePath);

        // Load source code
        if (!module->loadFromFile()) {
            std::cerr << "  Warning: Failed to load module file: " << filePath << "\n";
            continue;
        }

        std::cout << "  Discovered: " << filePath << " -> " << moduleName << "\n";

        Module* modulePtr = module.get();
        moduleCache[moduleName] = std::move(module);
        modules.push_back(modulePtr);
    }

    return modules;
}

} // namespace Import
} // namespace XXML
