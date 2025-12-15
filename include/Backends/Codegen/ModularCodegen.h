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
#include <set>

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

    // === Function Declarations ===

    /// Generate forward declarations for runtime module functions
    /// Uses GlobalBuilder to create function declarations in the Module
    void generateFunctionDeclarations(Parser::Program& program);

    /// Track a function as declared (to avoid duplicates)
    void markFunctionDeclared(const std::string& funcName);

    /// Check if a function has already been declared
    bool isFunctionDeclared(const std::string& funcName) const;

    // === Processor Mode ===

    /// Generate annotation processor entry points
    /// Creates __xxml_processor_annotation_name() and __xxml_processor_process()
    void generateProcessorEntryPoints(Parser::Program& program,
                                      const std::string& annotationName);

    // === Derive Mode ===

    /// Generate derive entry points
    /// Creates __xxml_derive_name(), __xxml_derive_canDerive(), __xxml_derive_generate()
    void generateDeriveEntryPoints(Parser::Program& program,
                                   const std::string& deriveName);

private:
    CodegenContext ctx_;
    std::unique_ptr<ExprCodegen> exprCodegen_;
    std::unique_ptr<StmtCodegen> stmtCodegen_;
    std::unique_ptr<DeclCodegen> declCodegen_;

    // Track declared functions to avoid duplicates
    std::set<std::string> declaredFunctions_;
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
