#include "../../include/Import/DependencyGraph.h"
#include <iostream>
#include <algorithm>

namespace XXML {
namespace Import {

DependencyGraph::DependencyGraph() {}

void DependencyGraph::addModule(const std::string& moduleName) {
    modules.insert(moduleName);
    if (dependencies.find(moduleName) == dependencies.end()) {
        dependencies[moduleName] = std::vector<std::string>();
    }
}

void DependencyGraph::addDependency(const std::string& fromModule, const std::string& toModule) {
    modules.insert(fromModule);
    modules.insert(toModule);
    dependencies[fromModule].push_back(toModule);
}

bool DependencyGraph::hasCycleUtil(const std::string& module,
                                    std::set<std::string>& visited,
                                    std::set<std::string>& recStack,
                                    std::vector<std::string>& cycle) const {
    visited.insert(module);
    recStack.insert(module);
    cycle.push_back(module);

    auto it = dependencies.find(module);
    if (it != dependencies.end()) {
        for (const auto& dep : it->second) {
            if (recStack.find(dep) != recStack.end()) {
                // Found cycle
                cycle.push_back(dep);
                return true;
            }

            if (visited.find(dep) == visited.end()) {
                if (hasCycleUtil(dep, visited, recStack, cycle)) {
                    return true;
                }
            }
        }
    }

    cycle.pop_back();
    recStack.erase(module);
    return false;
}

bool DependencyGraph::hasCycle(std::vector<std::string>& cycle) const {
    std::set<std::string> visited;
    std::set<std::string> recStack;

    for (const auto& module : modules) {
        if (visited.find(module) == visited.end()) {
            cycle.clear();
            if (hasCycleUtil(module, visited, recStack, cycle)) {
                return true;
            }
        }
    }

    return false;
}

void DependencyGraph::topologicalSortUtil(const std::string& module,
                                          std::set<std::string>& visited,
                                          std::vector<std::string>& stack) const {
    visited.insert(module);

    auto it = dependencies.find(module);
    if (it != dependencies.end()) {
        for (const auto& dep : it->second) {
            if (visited.find(dep) == visited.end()) {
                topologicalSortUtil(dep, visited, stack);
            }
        }
    }

    stack.push_back(module);
}

std::vector<std::string> DependencyGraph::topologicalSort() const {
    // Check for cycles first
    std::vector<std::string> cycle;
    if (hasCycle(cycle)) {
        std::cerr << "Error: Circular dependency detected: ";
        for (size_t i = 0; i < cycle.size(); ++i) {
            if (i > 0) std::cerr << " -> ";
            std::cerr << cycle[i];
        }
        std::cerr << std::endl;
        return std::vector<std::string>();
    }

    std::set<std::string> visited;
    std::vector<std::string> stack;

    for (const auto& module : modules) {
        if (visited.find(module) == visited.end()) {
            topologicalSortUtil(module, visited, stack);
        }
    }

    // Don't reverse - dependencies are already added before dependents
    // The DFS pushes dependencies onto the stack first, then the module that depends on them
    // So stack already has the correct order: dependencies before dependents
    return stack;
}

std::set<std::string> DependencyGraph::getAllDependencies(const std::string& moduleName) const {
    std::set<std::string> allDeps;
    std::vector<std::string> toVisit;
    std::set<std::string> visited;

    auto it = dependencies.find(moduleName);
    if (it != dependencies.end()) {
        toVisit = it->second;
    }

    while (!toVisit.empty()) {
        std::string current = toVisit.back();
        toVisit.pop_back();

        if (visited.find(current) != visited.end()) {
            continue;
        }

        visited.insert(current);
        allDeps.insert(current);

        auto depIt = dependencies.find(current);
        if (depIt != dependencies.end()) {
            for (const auto& dep : depIt->second) {
                toVisit.push_back(dep);
            }
        }
    }

    return allDeps;
}

std::vector<std::string> DependencyGraph::getDirectDependencies(const std::string& moduleName) const {
    auto it = dependencies.find(moduleName);
    if (it != dependencies.end()) {
        return it->second;
    }
    return std::vector<std::string>();
}

void DependencyGraph::print() const {
    std::cout << "Dependency Graph:" << std::endl;
    for (const auto& pair : dependencies) {
        std::cout << "  " << pair.first << " -> [";
        for (size_t i = 0; i < pair.second.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << pair.second[i];
        }
        std::cout << "]" << std::endl;
    }
}

void DependencyGraph::clear() {
    dependencies.clear();
    modules.clear();
}

std::vector<std::string> DependencyGraph::getAllModules() const {
    return std::vector<std::string>(modules.begin(), modules.end());
}

} // namespace Import
} // namespace XXML
