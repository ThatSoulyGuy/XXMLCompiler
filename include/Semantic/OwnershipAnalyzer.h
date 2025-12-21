#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include <stack>
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include "PassResults.h"
#include "SemanticError.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// OWNERSHIP ANALYZER
//
// This pass validates ownership and lifetime rules before IR generation.
// It performs:
//   1. Move tracking (no double-move, no use-after-move)
//   2. Reference lifetime validation
//   3. Lambda capture analysis
//   4. Destructor ordering
//
// After this pass completes successfully:
//   - No use-after-move violations
//   - No double-move violations
//   - References don't outlive their owners
//   - Lambda captures honor ^, &, % rules
//==============================================================================

class OwnershipAnalyzer {
public:
    OwnershipAnalyzer(Common::ErrorReporter& errorReporter);

    // Main entry points
    OwnershipAnalysisResult run(Parser::Program& program,
                                 const SemanticValidationResult& semanticResult);
    void analyzeMethod(Parser::MethodDecl* method);
    void analyzeLambda(Parser::LambdaExpr* lambda);

    // Get the result
    const OwnershipAnalysisResult& result() const { return result_; }

    // Query ownership state
    OwnershipState getVariableState(const std::string& varName) const;
    bool isVariableMoved(const std::string& varName) const;
    bool isVariableBorrowed(const std::string& varName) const;

private:
    Common::ErrorReporter& errorReporter_;
    OwnershipAnalysisResult result_;
    const SemanticValidationResult* semanticResult_ = nullptr;  // For method signature lookup

    // Variable tracking
    struct VariableInfo {
        std::string name;
        std::string typeName;
        Parser::OwnershipType ownership;
        OwnershipState state;
        int scopeLevel;
        Common::SourceLocation declLocation;
        Common::SourceLocation moveLocation;  // Where it was moved (if moved)
        std::vector<int> borrowScopes;        // Scopes where references exist
    };

    std::unordered_map<std::string, VariableInfo> variables_;
    int currentScope_ = 0;
    std::stack<std::set<std::string>> scopeVariables_;  // Variables declared in each scope

    // Scope management
    void pushScope();
    void popScope();

    // Ownership operations
    void declareVariable(const std::string& name, const std::string& typeName,
                        Parser::OwnershipType ownership, const Common::SourceLocation& loc);
    void markMoved(const std::string& varName, const Common::SourceLocation& loc);
    void markBorrowed(const std::string& varName, const std::string& refName, int borrowScope);
    void checkNotMoved(const std::string& varName, const Common::SourceLocation& loc);
    void checkBorrowsValid(int scopeLevel);

    // Analysis helpers - manual AST traversal
    void analyzeExpression(Parser::Expression* expr);
    void analyzeStatement(Parser::Statement* stmt);
    void analyzeBlock(const std::vector<std::unique_ptr<Parser::Statement>>& statements);
    void analyzeLambdaCaptures(Parser::LambdaExpr* lambda);
    void analyzeCallArguments(Parser::CallExpr* call);
    void analyzeAssignmentTarget(Parser::Expression* target);

    // Move detection
    bool isOwnedType(Parser::OwnershipType ownership) const;
    bool wouldMove(Parser::Expression* expr, Parser::OwnershipType paramOwnership);
    std::string extractVariableName(Parser::Expression* expr);

    // Error reporting
    void reportUseAfterMove(const std::string& varName, const Common::SourceLocation& useLoc,
                            const Common::SourceLocation& moveLoc);
    void reportDoubleMove(const std::string& varName, const Common::SourceLocation& secondMove,
                          const Common::SourceLocation& firstMove);
    void reportDanglingReference(const std::string& refName, const std::string& targetName,
                                 const Common::SourceLocation& loc);
    void reportInvalidCapture(const std::string& varName, const std::string& reason,
                              const Common::SourceLocation& loc);
};

} // namespace Semantic
} // namespace XXML
