#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

void DeclCodegen::generate(Parser::ASTNode* decl) {
    if (!decl) return;

    // Dispatch based on declaration type
    if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl)) {
        visitClass(classDecl);
    } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(decl)) {
        visitNativeStruct(nativeDecl);
    } else if (auto* ctorDecl = dynamic_cast<Parser::ConstructorDecl*>(decl)) {
        visitConstructor(ctorDecl);
    } else if (auto* dtorDecl = dynamic_cast<Parser::DestructorDecl*>(decl)) {
        visitDestructor(dtorDecl);
    } else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl)) {
        visitMethod(methodDecl);
    } else if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(decl)) {
        visitProperty(propDecl);
    } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl)) {
        visitEnumeration(enumDecl);
    } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl)) {
        visitNamespace(nsDecl);
    } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(decl)) {
        visitEntrypoint(entryDecl);
    }
}

void DeclCodegen::generateFunctionBody(const std::vector<std::unique_ptr<Parser::Statement>>& body) {
    for (const auto& stmt : body) {
        stmtCodegen_.generate(stmt.get());
    }
}

// Default implementations

void DeclCodegen::visitClass(Parser::ClassDecl*) {}
void DeclCodegen::visitNativeStruct(Parser::NativeStructureDecl*) {}
void DeclCodegen::visitConstructor(Parser::ConstructorDecl*) {}
void DeclCodegen::visitDestructor(Parser::DestructorDecl*) {}
void DeclCodegen::visitMethod(Parser::MethodDecl*) {}
void DeclCodegen::visitProperty(Parser::PropertyDecl*) {}
void DeclCodegen::visitEnumeration(Parser::EnumerationDecl*) {}
void DeclCodegen::visitNamespace(Parser::NamespaceDecl*) {}
void DeclCodegen::visitEntrypoint(Parser::EntrypointDecl*) {}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
