#include "../../include/Import/ImportResolver.h"
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

namespace XXML {
namespace Import {

ImportResolver::ImportResolver() {
    // Default search paths
    addSearchPath("Language");      // For standard library
    addSearchPath(".");             // Current directory for user code
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

} // namespace Import
} // namespace XXML
