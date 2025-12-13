#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include "Module.h"

namespace XXML {
namespace Import {

// Resolves import paths to actual XXML source files
class ImportResolver {
private:
    // Search paths for finding modules (e.g., "Language/", ".")
    std::vector<std::string> searchPaths;

    // Cache of resolved modules: moduleName -> Module
    std::map<std::string, std::unique_ptr<Module>> moduleCache;

    // NEW: Track which files come from Language/ folders (STL files)
    std::set<std::string> stlFilePaths;

    // NEW: Separate discovery from import resolution
    std::vector<std::string> compilerDirFiles;    // All files from compiler dir
    std::vector<std::string> sourceDirFiles;      // All files from source dir
    std::string compilerDir;                      // Directory containing compiler executable
    std::string compilerLanguagePath;             // Compiler's Language/ folder
    std::string sourceDir;                        // Directory containing source file
    std::string sourceLanguagePath;               // Source's Language/ folder (if exists)

    // Convert namespace path to directory path
    // e.g., "Language::Core" -> "Language/Core"
    std::string namespaceToPath(const std::string& namespacePath) const;

    // Find all XXML files in a directory
    std::vector<std::string> findXXMLFilesInDirectory(const std::string& dirPath) const;

    // Find all XXML files in a directory recursively
    std::vector<std::string> findXXMLFilesRecursive(const std::string& dirPath) const;

    // Extract module name from file path relative to a base directory
    std::string extractModuleNameFromBase(const std::string& filePath, const std::string& baseDir) const;

    // Normalize a file path for comparison (forward slashes, lowercase on Windows)
    std::string normalizeFilePath(const std::string& filePath) const;

    // Get parent directory of a file path
    std::string getParentDirectory(const std::string& filePath) const;

public:
    // Constructor with default search paths
    ImportResolver();

    // NEW: Initialize with compiler executable path - scans compiler directory recursively
    void initializeWithCompilerPath(const std::string& compilerExePath);

    // NEW: Add source file and discover its directory recursively
    void initializeWithSourceFile(const std::string& sourceFilePath);

    // NEW: Get all files to compile (with conflict resolution - source wins)
    std::vector<std::string> getAllFilesToCompile();

    // NEW: Check if a file is from STL (Language/ folder)
    bool isSTLFile(const std::string& filePath) const;

    // NEW: Extract module name from file path (public version)
    std::string extractModuleName(const std::string& filePath) const;

    // Discover all XXML files in current directory and subdirectories
    std::vector<Module*> discoverAllModules();

    // Add a search path (e.g., "Language/", "./lib/")
    void addSearchPath(const std::string& path);

    // Add a priority search path (inserted at the beginning, searched first)
    // Use this for user -I paths to ensure they take precedence over stdlib
    void addPrioritySearchPath(const std::string& path);

    // Add the directory containing the source file being compiled
    void addSourceFileDirectory(const std::string& sourceFilePath);

    // Resolve an import statement to a list of modules
    // e.g., "Language::Core" -> [Language::Core::String, Language::Core::Integer, ...]
    std::vector<Module*> resolveImport(const std::string& importPath);

    // Get a module by name (returns nullptr if not found)
    Module* getModule(const std::string& moduleName);

    // Check if a module has been loaded
    bool hasModule(const std::string& moduleName) const;

    // Get all loaded modules
    std::vector<Module*> getAllModules() const;

    // Clear the module cache
    void clear();
};

} // namespace Import
} // namespace XXML
