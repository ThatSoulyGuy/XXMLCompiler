#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"
#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Parser/AST.h"
#include <memory>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Main orchestrator for modular code generation
 *
 * Coordinates expression, statement, and declaration codegen modules.
 * Provides a unified interface for code generation that can be used
 * by LLVMBackend to delegate IR generation.
 */
class ModularCodegen {
public:
    explicit ModularCodegen(Core::CompilationContext* compCtx = nullptr);
    ~ModularCodegen();

    // === Main Entry Points ===

    /// Generate IR for an expression, returns the result value
    LLVMIR::AnyValue generateExpr(Parser::Expression* expr);

    /// Generate IR for a statement
    void generateStmt(Parser::Statement* stmt);

    /// Generate IR for a declaration (class, method, etc.)
    void generateDecl(Parser::ASTNode* decl);

    /// Generate IR for a complete program/module (ASTNode version)
    void generateProgram(const std::vector<std::unique_ptr<Parser::ASTNode>>& nodes);

    /// Generate IR for a complete program/module (Declaration version)
    void generateProgramDecls(const std::vector<std::unique_ptr<Parser::Declaration>>& decls);

    // === Context Access ===

    CodegenContext& context() { return ctx_; }
    const CodegenContext& context() const { return ctx_; }

    LLVMIR::Module& module() { return ctx_.module(); }
    LLVMIR::IRBuilder& builder() { return ctx_.builder(); }

    // === Scope Management ===

    void pushScope() { ctx_.pushScope(); }
    void popScope() { ctx_.popScope(); }

    // === Class Context ===

    void setCurrentClass(std::string_view name) { ctx_.setCurrentClassName(name); }
    void setCurrentNamespace(std::string_view ns) { ctx_.setCurrentNamespace(ns); }
    void setCurrentFunction(LLVMIR::Function* func) { ctx_.setCurrentFunction(func); }
    void setReturnType(std::string_view type) { ctx_.setCurrentReturnType(type); }

    // === Semantic Analyzer ===

    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) {
        ctx_.setSemanticAnalyzer(analyzer);
    }

    // === IR Output ===

    /// Get the generated LLVM IR as a string
    std::string getIR() const;

private:
    CodegenContext ctx_;
    std::unique_ptr<ExprCodegen> exprCodegen_;
    std::unique_ptr<StmtCodegen> stmtCodegen_;
    std::unique_ptr<DeclCodegen> declCodegen_;

    void initializeCodegens();
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
