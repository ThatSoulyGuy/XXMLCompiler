#pragma once
#include <string>
#include <vector>
#include <map>
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

    // Convert namespace path to directory path
    // e.g., "Language::Core" -> "Language/Core"
    std::string namespaceToPath(const std::string& namespacePath) const;

    // Find all XXML files in a directory
    std::vector<std::string> findXXMLFilesInDirectory(const std::string& dirPath) const;

    // Find all XXML files in a directory recursively
    std::vector<std::string> findXXMLFilesRecursive(const std::string& dirPath) const;

    // Extract module name from file path
    // e.g., "Language/Core/String.XXML" -> "Language::Core::String"
    std::string extractModuleName(const std::string& filePath) const;

public:
    // Constructor with default search paths
    ImportResolver();

    // Discover all XXML files in current directory and subdirectories
    std::vector<Module*> discoverAllModules();

    // Add a search path (e.g., "Language/", "./lib/")
    void addSearchPath(const std::string& path);

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
