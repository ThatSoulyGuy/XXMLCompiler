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

    // Add the executable directory itself for auto-scanning
    addSearchPath(exeDir);

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

void ImportResolver::addSourceFileDirectory(const std::string& sourceFilePath) {
    // Extract directory from source file path
    fs::path filePath(sourceFilePath);
    if (filePath.has_parent_path()) {
        std::string dirPath = filePath.parent_path().string();
        addSearchPath(dirPath);
        std::cout << "✓ Added source directory to search paths: " << dirPath << "\n";
    }
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
            // Extract just the filename without extension to construct module name
            // e.g., for importPath="GLFW" and file="GLFW.XXML", module name should be "GLFW::GLFW"
            // for importPath="Language::Core" and file="String.XXML", module name should be "Language::Core::String"
            fs::path fp(filePath);
            std::string filename = fp.stem().string();  // Get filename without extension
            std::string moduleName = importPath + "::" + filename;

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

    // Discover all XXML files in all search paths
    std::cout << "Auto-discovering XXML files in search paths...\n";

    for (const auto& searchPath : searchPaths) {
        std::cout << "  Scanning: " << searchPath << "\n";

        // Find all XXML files in this directory (non-recursive for search paths)
        auto files = findXXMLFilesInDirectory(searchPath);

        for (const auto& filePath : files) {
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
                std::cerr << "    Warning: Failed to load module file: " << filePath << "\n";
                continue;
            }

            std::cout << "    Found: " << filePath << " -> " << moduleName << "\n";

            Module* modulePtr = module.get();
            moduleCache[moduleName] = std::move(module);
            modules.push_back(modulePtr);
        }
    }

    return modules;
}

// ============================================================================
// NEW: File discovery and STL tracking methods
// ============================================================================

std::string ImportResolver::normalizeFilePath(const std::string& filePath) const {
    std::string normalized = filePath;
    // Convert backslashes to forward slashes
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    // Convert to lowercase for case-insensitive comparison on Windows
#ifdef _WIN32
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
#endif
    return normalized;
}

std::string ImportResolver::getParentDirectory(const std::string& filePath) const {
    fs::path path(filePath);
    if (path.has_parent_path()) {
        return path.parent_path().string();
    }
    return ".";
}

std::string ImportResolver::extractModuleNameFromBase(const std::string& filePath, const std::string& baseDir) const {
    // Get the relative path from baseDir
    fs::path fullPath = fs::absolute(filePath);
    fs::path basePath = fs::absolute(baseDir);

    std::string relativePath;
    try {
        relativePath = fs::relative(fullPath, basePath).string();
    } catch (...) {
        // If relative path fails, use filename only
        relativePath = fs::path(filePath).filename().string();
    }

    // Remove .XXML extension
    if (relativePath.length() > 5 && relativePath.substr(relativePath.length() - 5) == ".XXML") {
        relativePath = relativePath.substr(0, relativePath.length() - 5);
    }

    // Replace path separators with ::
    std::string moduleName = relativePath;
    std::replace(moduleName.begin(), moduleName.end(), '/', ':');
    std::replace(moduleName.begin(), moduleName.end(), '\\', ':');

    // Fix single colons to double colons
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

void ImportResolver::initializeWithCompilerPath(const std::string& compilerExePath) {
    // Get compiler directory
    compilerDir = getExecutableDirectory();
    compilerLanguagePath = compilerDir + "/Language";

    std::cout << "Scanning compiler directory: " << compilerDir << "\n";

    // Recursively scan compiler directory for all XXML files
    compilerDirFiles = findXXMLFilesRecursive(compilerDir);

    // Filter out build directories and track STL files
    std::vector<std::string> filteredFiles;
    for (const auto& file : compilerDirFiles) {
        // Skip build directories
        if (file.find("build/") != std::string::npos ||
            file.find("build\\") != std::string::npos ||
            file.find("x64/") != std::string::npos ||
            file.find("x64\\") != std::string::npos ||
            file.find("Debug/") != std::string::npos ||
            file.find("Debug\\") != std::string::npos ||
            file.find("Release/") != std::string::npos ||
            file.find("Release\\") != std::string::npos) {
            continue;
        }

        filteredFiles.push_back(file);

        // Track STL files (files in Language/ folder)
        if (file.find("/Language/") != std::string::npos ||
            file.find("\\Language\\") != std::string::npos) {
            stlFilePaths.insert(normalizeFilePath(file));
        }
    }
    compilerDirFiles = filteredFiles;

    std::cout << "  Found " << compilerDirFiles.size() << " XXML files in compiler directory\n";

    // Add compiler Language path to search paths if it exists
    if (fs::exists(compilerLanguagePath) && fs::is_directory(compilerLanguagePath)) {
        addSearchPath(compilerLanguagePath);
        std::cout << "  ✓ Found standard library at: " << compilerLanguagePath << "\n";
    }
}

void ImportResolver::initializeWithSourceFile(const std::string& sourceFilePath) {
    // Get source file directory
    sourceDir = getParentDirectory(sourceFilePath);
    sourceLanguagePath = sourceDir + "/Language";

    // Don't scan if source dir is same as compiler dir
    std::string normalizedSourceDir = normalizeFilePath(fs::absolute(sourceDir).string());
    std::string normalizedCompilerDir = normalizeFilePath(fs::absolute(compilerDir).string());

    if (normalizedSourceDir == normalizedCompilerDir) {
        std::cout << "Source directory is same as compiler directory, skipping duplicate scan\n";
        return;
    }

    std::cout << "Scanning source directory: " << sourceDir << "\n";

    // Recursively scan source directory for all XXML files
    sourceDirFiles = findXXMLFilesRecursive(sourceDir);

    // Filter out build directories and track STL files
    std::vector<std::string> filteredFiles;
    for (const auto& file : sourceDirFiles) {
        // Skip build directories
        if (file.find("build/") != std::string::npos ||
            file.find("build\\") != std::string::npos ||
            file.find("x64/") != std::string::npos ||
            file.find("x64\\") != std::string::npos ||
            file.find("Debug/") != std::string::npos ||
            file.find("Debug\\") != std::string::npos ||
            file.find("Release/") != std::string::npos ||
            file.find("Release\\") != std::string::npos) {
            continue;
        }

        filteredFiles.push_back(file);

        // Track STL files (files in Language/ folder)
        if (file.find("/Language/") != std::string::npos ||
            file.find("\\Language\\") != std::string::npos) {
            stlFilePaths.insert(normalizeFilePath(file));
        }
    }
    sourceDirFiles = filteredFiles;

    std::cout << "  Found " << sourceDirFiles.size() << " XXML files in source directory\n";

    // If source has its own Language folder, add it (takes priority)
    if (fs::exists(sourceLanguagePath) && fs::is_directory(sourceLanguagePath)) {
        // Insert at the beginning so it takes priority
        searchPaths.insert(searchPaths.begin(), sourceLanguagePath);
        std::cout << "  ✓ Found project Language folder at: " << sourceLanguagePath << " (takes priority)\n";
    }

    // Add source directory to search paths
    addSearchPath(sourceDir);
}

std::vector<std::string> ImportResolver::getAllFilesToCompile() {
    std::map<std::string, std::string> moduleToFile;  // moduleName -> filePath

    // First, add all compiler directory files
    for (const auto& file : compilerDirFiles) {
        std::string moduleName = extractModuleNameFromBase(file, compilerDir);
        moduleToFile[moduleName] = file;
    }

    // Then, source directory files OVERRIDE compiler files (source wins)
    for (const auto& file : sourceDirFiles) {
        std::string moduleName = extractModuleNameFromBase(file, sourceDir);
        auto it = moduleToFile.find(moduleName);
        if (it != moduleToFile.end() && it->second != file) {
            std::cout << "  Note: Source file overrides compiler file for module: " << moduleName << "\n";
        }
        moduleToFile[moduleName] = file;  // Overwrites if exists
    }

    // Convert map values to vector
    std::vector<std::string> result;
    for (const auto& [name, path] : moduleToFile) {
        result.push_back(path);
    }

    std::cout << "Total files to compile: " << result.size() << "\n";
    return result;
}

bool ImportResolver::isSTLFile(const std::string& filePath) const {
    std::string normalizedPath = normalizeFilePath(filePath);

    // Check if in explicit STL file paths set
    if (stlFilePaths.count(normalizedPath) > 0) {
        return true;
    }

    // Also check if path contains Language/ directory pattern
    // After normalization, the path is lowercased on Windows, so check lowercase
#ifdef _WIN32
    if (normalizedPath.find("/language/") != std::string::npos ||
        normalizedPath.find("language/") == 0) {
        return true;
    }
#else
    if (normalizedPath.find("/Language/") != std::string::npos ||
        normalizedPath.find("Language/") == 0) {
        return true;
    }
#endif

    return false;
}

} // namespace Import
} // namespace XXML
