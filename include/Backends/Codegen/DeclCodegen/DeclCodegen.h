#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"
#include "Parser/AST.h"

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Base class for declaration code generation
 *
 * Handles class, method, constructor, and other declaration code generation.
 */
class DeclCodegen {
public:
    DeclCodegen(CodegenContext& ctx, ExprCodegen& exprCodegen, StmtCodegen& stmtCodegen)
        : ctx_(ctx), exprCodegen_(exprCodegen), stmtCodegen_(stmtCodegen) {}
    virtual ~DeclCodegen() = default;

    // Main dispatch
    void generate(Parser::ASTNode* decl);

    // === Declaration Visitors ===

    // ClassCodegen.cpp
    virtual void visitClass(Parser::ClassDecl* decl);
    virtual void visitNativeStruct(Parser::NativeStructureDecl* decl);

    // ConstructorCodegen.cpp
    virtual void visitConstructor(Parser::ConstructorDecl* decl);

    // DestructorCodegen.cpp
    virtual void visitDestructor(Parser::DestructorDecl* decl);

    // MethodCodegen.cpp
    virtual void visitMethod(Parser::MethodDecl* decl);

    // PropertyCodegen.cpp
    virtual void visitProperty(Parser::PropertyDecl* decl);

    // EnumCodegen.cpp
    virtual void visitEnumeration(Parser::EnumerationDecl* decl);

    // Other declarations
    virtual void visitNamespace(Parser::NamespaceDecl* decl);
    virtual void visitEntrypoint(Parser::EntrypointDecl* decl);
    virtual void visitAnnotationDecl(Parser::AnnotationDecl* decl);

protected:
    CodegenContext& ctx_;
    ExprCodegen& exprCodegen_;
    StmtCodegen& stmtCodegen_;

    // Generate body statements for a function
    void generateFunctionBody(const std::vector<std::unique_ptr<Parser::Statement>>& body);

private:
    // Annotation collection helpers
    void collectRetainedAnnotations(Parser::ClassDecl* decl);
    void collectRetainedAnnotations(Parser::MethodDecl* decl, const std::string& className);
    void collectRetainedAnnotations(Parser::PropertyDecl* decl, const std::string& className);
    AnnotationArgValue evaluateAnnotationArg(Parser::Expression* expr);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
