#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"
#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Backends/Codegen/ReflectionCodegen/ReflectionCodegen.h"
#include "Backends/Codegen/AnnotationCodegen/AnnotationCodegen.h"
#include "Backends/Codegen/TemplateCodegen/TemplateCodegen.h"
#include "Backends/Codegen/ModuleCodegen/ModuleCodegen.h"
#include "Backends/Codegen/NativeCodegen/NativeCodegen.h"
#include "Backends/Codegen/LambdaTemplateCodegen/LambdaTemplateCodegen.h"
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

    /// Get only function definitions (for combining with existing preamble)
    std::string getFunctionsIR() const;

    /// Generate reflection and annotation metadata IR
    void generateMetadata();

    /// Get metadata IR as a string
    std::string getMetadataIR() const;

    // === Template Instantiation ===

    /// Generate all template class instantiations
    void generateTemplates(Semantic::SemanticAnalyzer& analyzer);

    // === Module Processing ===

    /// Collect reflection metadata from a module (without generating code)
    void collectReflectionMetadata(Parser::Program& program);

    /// Generate code for an imported module
    void generateImportedModule(Parser::Program& program, const std::string& moduleName = "");

    // === Native FFI Support ===

    /// Generate a native FFI thunk for a method
    void generateNativeThunk(Parser::MethodDecl& node,
                            const std::string& className,
                            const std::string& namespaceName);

    /// Get the generated native thunk IR
    std::string getNativeThunkIR() const;

    /// Get string literals needed for native thunks
    const std::vector<std::pair<std::string, std::string>>& getNativeStringLiterals() const;

    /// Reset native codegen state
    void resetNativeCodegen();

    // === Lambda Template Support ===

    /// Generate lambda template instantiations
    void generateLambdaTemplates(Semantic::SemanticAnalyzer& analyzer);

    /// Get generated lambda function definitions
    const std::vector<std::string>& getLambdaDefinitions() const;

    /// Look up a lambda template function name
    std::string getLambdaFunction(const std::string& key) const;

    /// Look up lambda info for a template instantiation
    const LambdaTemplateCodegen::LambdaInfo* getLambdaInfo(const std::string& key) const;

    /// Reset lambda template codegen state
    void resetLambdaCodegen();

private:
    CodegenContext ctx_;
    std::unique_ptr<ExprCodegen> exprCodegen_;
    std::unique_ptr<StmtCodegen> stmtCodegen_;
    std::unique_ptr<DeclCodegen> declCodegen_;
    std::unique_ptr<ReflectionCodegen> reflectionCodegen_;
    std::unique_ptr<AnnotationCodegen> annotationCodegen_;
    std::unique_ptr<TemplateCodegen> templateCodegen_;
    std::unique_ptr<ModuleCodegen> moduleCodegen_;
    std::unique_ptr<NativeCodegen> nativeCodegen_;
    std::unique_ptr<LambdaTemplateCodegen> lambdaTemplateCodegen_;

    void initializeCodegens();
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
