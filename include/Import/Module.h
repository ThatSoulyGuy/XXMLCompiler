#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../Parser/AST.h"
#include "../Semantic/SymbolTable.h"

namespace XXML {
namespace Import {

// Represents a single XXML source file/module
class Module {
public:
    std::string moduleName;          // e.g., "Language::Core::String"
    std::string filePath;            // e.g., "Language/Core/String.XXML"
    std::string fileContent;         // Source code

    // Parsed representation
    std::unique_ptr<Parser::Program> ast;

    // Symbols exported by this module
    std::unique_ptr<Semantic::SymbolTable> exportedSymbols;

    // Module dependencies (other modules this one imports)
    std::vector<std::string> imports;

    // Compilation state
    bool isParsed;
    bool isAnalyzed;
    bool isCompiled;

    // Constructor
    Module(const std::string& name, const std::string& path);

    // Load source code from file
    bool loadFromFile();

    // Get a readable string representation
    std::string toString() const;
};

} // namespace Import
} // namespace XXML
