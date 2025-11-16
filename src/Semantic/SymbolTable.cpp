#include "../../include/Semantic/SymbolTable.h"
#include <sstream>
#include <iostream>

namespace XXML {
namespace Semantic {

// Initialize static member
std::unordered_map<std::string, SymbolTable*> SymbolTable::moduleRegistry;

// Scope implementation
void Scope::define(const std::string& name, std::unique_ptr<Symbol> symbol) {
    symbols[name] = std::move(symbol);
}

Symbol* Scope::resolve(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second.get();
    }

    if (parent) {
        return parent->resolve(name);
    }

    return nullptr;
}

Symbol* Scope::resolveLocal(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second.get();
    }
    return nullptr;
}

Symbol* Scope::resolveQualified(const std::string& qualifiedName) {
    // Parse qualified name (e.g., "RenderStar::Default::MyClass")
    std::string remaining = qualifiedName;
    Scope* currentScope = this;

    size_t pos = 0;
    while ((pos = remaining.find("::")) != std::string::npos) {
        std::string segment = remaining.substr(0, pos);
        remaining = remaining.substr(pos + 2);

        Symbol* sym = currentScope->resolveLocal(segment);
        if (!sym) return nullptr;

        // Find the child scope for this symbol
        bool found = false;
        for (auto& child : currentScope->children) {
            if (child->getName() == segment) {
                currentScope = child.get();
                found = true;
                break;
            }
        }

        if (!found) return nullptr;
    }

    return currentScope->resolveLocal(remaining);
}

Scope* Scope::createChildScope(const std::string& name) {
    auto child = std::make_unique<Scope>(name, this);
    Scope* childPtr = child.get();
    children.push_back(std::move(child));
    return childPtr;
}

// SymbolTable implementation
SymbolTable::SymbolTable() : moduleName("") {
    globalScope = std::make_unique<Scope>("global", nullptr);
    currentScope = globalScope.get();
}

SymbolTable::SymbolTable(const std::string& modName) : moduleName(modName) {
    globalScope = std::make_unique<Scope>("global", nullptr);
    currentScope = globalScope.get();
}

void SymbolTable::enterScope(const std::string& name) {
    currentScope = currentScope->createChildScope(name);
}

void SymbolTable::exitScope() {
    if (currentScope->getParent()) {
        currentScope = currentScope->getParent();
    }
}

void SymbolTable::define(const std::string& name, std::unique_ptr<Symbol> symbol) {
    currentScope->define(name, std::move(symbol));
}

Symbol* SymbolTable::resolve(const std::string& name) {
    // First try local resolution
    Symbol* localSym = currentScope->resolve(name);
    if (localSym) return localSym;

    // Then try imported symbols
    auto it = importedSymbols.find(name);
    if (it != importedSymbols.end()) {
        return it->second;
    }

    return nullptr;
}

Symbol* SymbolTable::resolveQualified(const std::string& qualifiedName) {
    return globalScope->resolveQualified(qualifiedName);
}

// Module-level operations
void SymbolTable::setModuleName(const std::string& modName) {
    moduleName = modName;
}

void SymbolTable::exportSymbol(const std::string& symbolName) {
    Symbol* sym = globalScope->resolveLocal(symbolName);
    if (sym) {
        sym->isExported = true;
        sym->moduleName = moduleName;
        exportedSymbolNames.insert(symbolName);
    }
}

void SymbolTable::importSymbol(const std::string& fromModule, const std::string& symbolName) {
    SymbolTable* otherModule = getModuleTable(fromModule);
    if (!otherModule) {
        std::cerr << "Warning: Module '" << fromModule << "' not found in registry" << std::endl;
        return;
    }

    Symbol* sym = otherModule->getGlobalScope()->resolveLocal(symbolName);
    if (sym && sym->isExported) {
        importedSymbols[symbolName] = sym;
    } else {
        std::cerr << "Warning: Symbol '" << symbolName << "' not exported from module '"
                  << fromModule << "'" << std::endl;
    }
}

void SymbolTable::importAllFrom(const std::string& fromModule) {
    // First try exact module match
    SymbolTable* otherModule = getModuleTable(fromModule);
    if (otherModule) {
        for (const auto& symbolName : otherModule->exportedSymbolNames) {
            Symbol* sym = otherModule->getGlobalScope()->resolveLocal(symbolName);
            if (sym && sym->isExported) {
                importedSymbols[symbolName] = sym;
            }
        }
        return;
    }

    // If exact match not found, try importing from all modules that match the namespace
    // For example, "Language::Core" should import from "Language::Core::Integer", "Language::Core::String", etc.
    bool foundAnyModule = false;
    for (const auto& [moduleName, moduleTable] : moduleRegistry) {
        // Check if this module is in the requested namespace
        // Module is in namespace if it starts with "namespace::" or equals the namespace
        if (moduleName == fromModule ||
            (moduleName.size() > fromModule.size() &&
             moduleName.substr(0, fromModule.size()) == fromModule &&
             moduleName[fromModule.size()] == ':')) {

            foundAnyModule = true;
            for (const auto& symbolName : moduleTable->exportedSymbolNames) {
                Symbol* sym = moduleTable->getGlobalScope()->resolveLocal(symbolName);
                if (sym && sym->isExported) {
                    importedSymbols[symbolName] = sym;
                }
            }
        }
    }

    if (!foundAnyModule) {
        std::cerr << "Warning: Module '" << fromModule << "' not found in registry" << std::endl;
    }
}

bool SymbolTable::isSymbolExported(const std::string& symbolName) const {
    return exportedSymbolNames.find(symbolName) != exportedSymbolNames.end();
}

std::vector<Symbol*> SymbolTable::getExportedSymbols() const {
    std::vector<Symbol*> result;
    for (const auto& symbolName : exportedSymbolNames) {
        Symbol* sym = globalScope->resolveLocal(symbolName);
        if (sym) {
            result.push_back(sym);
        }
    }
    return result;
}

void SymbolTable::registerModule() {
    if (!moduleName.empty()) {
        moduleRegistry[moduleName] = this;
    }
}

SymbolTable* SymbolTable::getModuleTable(const std::string& modName) {
    auto it = moduleRegistry.find(modName);
    if (it != moduleRegistry.end()) {
        return it->second;
    }
    return nullptr;
}

void SymbolTable::clearRegistry() {
    moduleRegistry.clear();
}

} // namespace Semantic
} // namespace XXML
