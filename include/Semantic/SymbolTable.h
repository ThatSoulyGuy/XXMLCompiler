#pragma once
#include <string>
#include <unordered_map>
#include <set>
#include <vector>
#include <memory>
#include "../Parser/AST.h"

namespace XXML {
namespace Semantic {

enum class SymbolKind {
    Namespace,
    Class,
    Method,
    Property,
    Parameter,
    LocalVariable,
    Constructor
};

struct Symbol {
    std::string name;
    SymbolKind kind;
    std::string typeName;
    Parser::OwnershipType ownership;
    Common::SourceLocation location;
    void* astNode; // Pointer to the AST node (for class/method/etc definitions)
    std::string moduleName; // Which module this symbol belongs to
    bool isExported; // Whether this symbol is exported from its module

    Symbol(const std::string& n, SymbolKind k, const std::string& type,
           Parser::OwnershipType own, const Common::SourceLocation& loc)
        : name(n), kind(k), typeName(type), ownership(own), location(loc),
          astNode(nullptr), moduleName(""), isExported(false) {}
};

class Scope {
private:
    std::string scopeName;
    Scope* parent;
    std::unordered_map<std::string, std::unique_ptr<Symbol>> symbols;
    std::vector<std::unique_ptr<Scope>> children;

public:
    Scope(const std::string& name, Scope* parentScope)
        : scopeName(name), parent(parentScope) {}

    void define(const std::string& name, std::unique_ptr<Symbol> symbol);
    Symbol* resolve(const std::string& name);
    Symbol* resolveLocal(const std::string& name);
    Symbol* resolveQualified(const std::string& qualifiedName);

    Scope* getParent() const { return parent; }
    const std::string& getName() const { return scopeName; }

    Scope* createChildScope(const std::string& name);
    const std::unordered_map<std::string, std::unique_ptr<Symbol>>& getSymbols() const {
        return symbols;
    }
};

class SymbolTable {
private:
    std::unique_ptr<Scope> globalScope;
    Scope* currentScope;
    std::string moduleName; // Name of the module this table belongs to
    std::set<std::string> exportedSymbolNames; // Names of exported symbols
    std::unordered_map<std::string, Symbol*> importedSymbols; // Symbols imported from other modules

    // Global registry: moduleName -> SymbolTable*
    static std::unordered_map<std::string, SymbolTable*> moduleRegistry;

public:
    SymbolTable();
    SymbolTable(const std::string& modName);

    void enterScope(const std::string& name);
    void exitScope();

    void define(const std::string& name, std::unique_ptr<Symbol> symbol);
    Symbol* resolve(const std::string& name);
    Symbol* resolveQualified(const std::string& qualifiedName);

    // Module-level operations
    void setModuleName(const std::string& modName);
    const std::string& getModuleName() const { return moduleName; }

    // Mark a symbol as exported (makes it visible to other modules)
    void exportSymbol(const std::string& symbolName);

    // Import a symbol from another module
    void importSymbol(const std::string& fromModule, const std::string& symbolName);

    // Import all exported symbols from another module
    void importAllFrom(const std::string& fromModule);

    // Check if a symbol is exported
    bool isSymbolExported(const std::string& symbolName) const;

    // Get all exported symbols
    std::vector<Symbol*> getExportedSymbols() const;

    // Register this table in the global module registry
    void registerModule();

    // Look up a module's symbol table from the registry
    static SymbolTable* getModuleTable(const std::string& modName);

    // Clear the global module registry (for testing)
    static void clearRegistry();

    Scope* getCurrentScope() const { return currentScope; }
    Scope* getGlobalScope() const { return globalScope.get(); }
};

} // namespace Semantic
} // namespace XXML
