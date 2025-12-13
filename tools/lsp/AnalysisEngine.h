// AnalysisEngine.h - XXML Semantic Analysis Integration
// XXML Language Server Protocol Implementation

#ifndef XXML_LSP_ANALYSIS_ENGINE_H
#define XXML_LSP_ANALYSIS_ENGINE_H

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include "Protocol.h"

// XXML compiler includes
#include "Parser/AST.h"
#include "Parser/Parser.h"
#include "Lexer/Lexer.h"
#include "Semantic/SemanticAnalyzer.h"
#include "Semantic/SymbolTable.h"
#include "Core/CompilationContext.h"
#include "Core/TypeContext.h"
#include "Common/Error.h"
#include "Common/SourceLocation.h"

namespace xxml::lsp {

// Result of analyzing a document
struct AnalysisResult {
    // AST from parsing
    std::unique_ptr<XXML::Parser::Program> ast;

    // Compilation context (owns symbol table, etc.)
    std::unique_ptr<XXML::Core::CompilationContext> context;

    // Semantic analyzer (for type queries)
    std::unique_ptr<XXML::Semantic::SemanticAnalyzer> analyzer;

    // Error reporter
    std::unique_ptr<XXML::Common::ErrorReporter> errorReporter;

    // Collected diagnostics
    std::vector<Diagnostic> diagnostics;

    // Analysis success status
    bool parseSuccess = false;
    bool semanticSuccess = false;

    // Source content (for position calculations)
    std::string sourceContent;
    std::vector<size_t> lineOffsets;

    // Pre-computed line offsets
    void computeLineOffsets();
    std::pair<int, int> offsetToPosition(size_t offset) const;
    size_t positionToOffset(int line, int character) const;
};

// Ownership information for visualization
struct OwnershipInfo {
    Range range;
    std::string kind;  // "owned", "reference", "copy"
    std::string typeName;
    std::string hoverMessage;
};

// Variable information for scope tracking
struct VariableInfo {
    std::string name;
    std::string typeName;
    XXML::Parser::OwnershipType ownership;
    bool isParameter;
    bool isProperty;
    int declarationLine;
};

// Context for completion at cursor position
struct CompletionContext {
    XXML::Parser::ClassDecl* currentClass = nullptr;
    XXML::Parser::MethodDecl* currentMethod = nullptr;
    XXML::Parser::ConstructorDecl* currentConstructor = nullptr;
    bool inEntrypoint = false;

    // Trigger context
    bool afterDot = false;
    bool afterDoubleColon = false;
    std::string precedingIdentifier;   // Identifier before . or ::
    std::string precedingTypeName;     // Resolved type of preceding expression

    // Semantic position context (mutually exclusive)
    bool inTypePosition = false;           // After Types, Returns, Instantiate (before As)
    bool inVariableNamePosition = false;   // After "As <" - no completions
    bool inExpressionPosition = false;     // After "=", in Run, in If condition, etc.
    bool inStatementPosition = false;      // At start of statement in method/entrypoint body
    bool inMemberDeclarationPosition = false; // Inside class, outside method body
    bool inTopLevelPosition = false;       // At file top level

    std::vector<VariableInfo> variablesInScope;
};

// Symbol information for navigation
struct SymbolLocationInfo {
    std::string name;
    std::string qualifiedName;
    std::string typeName;
    std::string kind;  // "class", "method", "property", "variable"
    std::string ownershipModifier;
    Location location;
};

// Analysis engine wraps XXML compiler for LSP use
class AnalysisEngine {
public:
    AnalysisEngine();
    ~AnalysisEngine();

    // Analyze a document and cache the result
    const AnalysisResult* analyze(const std::string& uri, const std::string& content);

    // Get cached analysis result for a document
    const AnalysisResult* getAnalysis(const std::string& uri) const;

    // Clear cached analysis for a document
    void clearAnalysis(const std::string& uri);

    // Clear all cached analyses
    void clearAll();

    // Find symbol at position in document
    std::optional<SymbolLocationInfo> findSymbolAtPosition(
        const std::string& uri, int line, int character) const;

    // Find definition of symbol at position
    std::optional<Location> findDefinition(
        const std::string& uri, int line, int character) const;

    // Find all references to symbol at position
    std::vector<Location> findReferences(
        const std::string& uri, int line, int character) const;

    // Get ownership information for decorations
    std::vector<OwnershipInfo> getOwnershipInfo(const std::string& uri) const;

    // Get hover information for position
    std::optional<std::string> getHoverInfo(
        const std::string& uri, int line, int character) const;

    // Get completion items for position
    std::vector<CompletionItem> getCompletions(
        const std::string& uri, int line, int character,
        const std::string& precedingText) const;

    // Get document symbols for outline
    std::vector<DocumentSymbol> getDocumentSymbols(const std::string& uri) const;

    // Set standard library path
    void setStdlibPath(const std::string& path) { stdlibPath_ = path; }

    // Set workspace root for resolving relative imports
    void setWorkspaceRoot(const std::string& path) { workspaceRoot_ = path; }

    // Add an include path for import resolution
    void addIncludePath(const std::string& path) { includePaths_.push_back(path); }

    // Get configured paths (for debugging)
    const std::string& getStdlibPath() const { return stdlibPath_; }
    const std::string& getWorkspaceRoot() const { return workspaceRoot_; }
    const std::vector<std::string>& getIncludePaths() const { return includePaths_; }

private:
    // Convert XXML compiler error to LSP diagnostic
    Diagnostic convertError(const XXML::Common::Error& error, const AnalysisResult& result) const;

    // Convert XXML SourceLocation to LSP Location
    Location convertSourceLocation(const std::string& uri,
                                   const XXML::Common::SourceLocation& loc,
                                   const AnalysisResult& result) const;

    // Find AST node at position
    XXML::Parser::ASTNode* findNodeAtPosition(XXML::Parser::Program* ast,
                                               int line, int character,
                                               const AnalysisResult& result) const;

    // Build completion context from cursor position and source text
    CompletionContext buildCompletionContext(const AnalysisResult* result,
                                              int line, int character,
                                              const std::string& precedingText) const;

    // Collect variables in scope at cursor position
    void collectVariablesInScope(CompletionContext& ctx,
                                  const AnalysisResult* result,
                                  int line, int character) const;

    // Resolve the type of an expression (for member completions)
    std::string resolveExpressionType(const std::string& exprText,
                                       const CompletionContext& ctx,
                                       const AnalysisResult* result) const;

    // Get completions for member access (after .)
    std::vector<CompletionItem> getMemberCompletions(const CompletionContext& ctx,
                                                      const AnalysisResult* result) const;

    // Get completions for static access (after ::)
    std::vector<CompletionItem> getStaticCompletions(const CompletionContext& ctx,
                                                      const AnalysisResult* result) const;

    // Get type completions (in type position)
    std::vector<CompletionItem> getTypeCompletions(const CompletionContext& ctx,
                                                    const AnalysisResult* result) const;

    // Get general completions (keywords, variables, types)
    std::vector<CompletionItem> getGeneralCompletions(const CompletionContext& ctx,
                                                       const AnalysisResult* result) const;

    // Get expression completions (variables, class names for construction)
    std::vector<CompletionItem> getExpressionCompletions(const CompletionContext& ctx,
                                                          const AnalysisResult* result) const;

    // Get statement completions (statement keywords at start of line)
    std::vector<CompletionItem> getStatementCompletions(const CompletionContext& ctx,
                                                         const AnalysisResult* result) const;

    // Get member declaration completions (Method, Property, Constructor inside class)
    std::vector<CompletionItem> getMemberDeclarationCompletions(const CompletionContext& ctx,
                                                                 const AnalysisResult* result) const;

    // Get top-level declaration completions (Class, Struct, Entrypoint)
    std::vector<CompletionItem> getTopLevelCompletions(const CompletionContext& ctx,
                                                        const AnalysisResult* result) const;

    // Helper: strip ownership suffix from type name
    static std::string stripOwnership(const std::string& typeName);

    // Helper: strip generic parameters from type name
    static std::string stripGenerics(const std::string& typeName);

    // Find and parse a type's source file to get its methods/properties
    // Returns completions for the type, or empty if not found
    std::vector<CompletionItem> parseTypeForCompletions(const std::string& typeName) const;

    // Parse a type's source file for static access (::) - includes Constructor
    std::vector<CompletionItem> parseTypeForStaticCompletions(const std::string& typeName) const;

    // Find the source file for a type name in the stdlib
    std::string findTypeSourceFile(const std::string& typeName) const;

    // Find namespace directory and list its contents (classes and sub-namespaces)
    std::vector<CompletionItem> getNamespaceContents(const std::string& namespacePath) const;

    // Collect symbols from AST for document outline
    void collectDocumentSymbols(std::vector<DocumentSymbol>& symbols,
                                 XXML::Parser::Program* ast,
                                 const std::string& uri,
                                 const AnalysisResult& result) const;

    // Collect ownership information from AST
    void collectOwnershipInfo(std::vector<OwnershipInfo>& info,
                               XXML::Parser::Program* ast,
                               const AnalysisResult& result) const;

    // Get type string for hover
    std::string getTypeString(const XXML::Semantic::Symbol* symbol) const;

    // Cached analysis results per document
    std::unordered_map<std::string, std::unique_ptr<AnalysisResult>> cache_;

    // Cache for parsed type completions (type name -> completions)
    mutable std::unordered_map<std::string, std::vector<CompletionItem>> typeCompletionCache_;

    // Standard library path
    std::string stdlibPath_;

    // Workspace root path
    std::string workspaceRoot_;

    // User-specified import search paths (from -I flags)
    std::vector<std::string> includePaths_;

    // Resolve an import path to a file path
    // sourceDir is the directory containing the current source file
    std::string resolveImportPath(const std::string& importPath, const std::string& sourceDir) const;
};

} // namespace xxml::lsp

#endif // XXML_LSP_ANALYSIS_ENGINE_H
