#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "Module.h"

namespace XXML {
namespace Import {

// Builds and analyzes module dependency relationships
class DependencyGraph {
private:
    // Adjacency list: module -> list of modules it depends on
    std::map<std::string, std::vector<std::string>> dependencies;

    // All modules in the graph
    std::set<std::string> modules;

    // Helper for cycle detection (DFS-based)
    bool hasCycleUtil(const std::string& module,
                      std::set<std::string>& visited,
                      std::set<std::string>& recStack,
                      std::vector<std::string>& cycle) const;

    // Helper for topological sort (DFS-based)
    void topologicalSortUtil(const std::string& module,
                             std::set<std::string>& visited,
                             std::vector<std::string>& stack) const;

public:
    DependencyGraph();

    // Add a module to the graph
    void addModule(const std::string& moduleName);

    // Add a dependency: fromModule depends on toModule
    void addDependency(const std::string& fromModule, const std::string& toModule);

    // Check if there are any circular dependencies
    // Returns true if cycle exists, and populates the cycle vector
    bool hasCycle(std::vector<std::string>& cycle) const;

    // Perform topological sort (returns modules in dependency order)
    // Returns empty vector if graph has cycles
    std::vector<std::string> topologicalSort() const;

    // Get all dependencies of a module (direct and transitive)
    std::set<std::string> getAllDependencies(const std::string& moduleName) const;

    // Get the direct dependencies of a module
    std::vector<std::string> getDirectDependencies(const std::string& moduleName) const;

    // Print the dependency graph (for debugging)
    void print() const;

    // Clear the graph
    void clear();

    // Get all modules in the graph
    std::vector<std::string> getAllModules() const;
};

} // namespace Import
} // namespace XXML
