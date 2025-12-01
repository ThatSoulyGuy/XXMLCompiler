#include "Backends/Codegen/ModularCodegen.h"
#include "Backends/LLVMIR/Emitter.h"

namespace XXML {
namespace Backends {
namespace Codegen {

ModularCodegen::ModularCodegen(Core::CompilationContext* compCtx)
    : ctx_(compCtx) {
    initializeCodegens();
}

ModularCodegen::~ModularCodegen() = default;

void ModularCodegen::initializeCodegens() {
    // Create the codegen modules
    exprCodegen_ = std::make_unique<ExprCodegen>(ctx_);
    stmtCodegen_ = std::make_unique<StmtCodegen>(ctx_, *exprCodegen_);
    declCodegen_ = std::make_unique<DeclCodegen>(ctx_, *exprCodegen_, *stmtCodegen_);
}

LLVMIR::AnyValue ModularCodegen::generateExpr(Parser::Expression* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(builder().getNullPtr());
    }
    return exprCodegen_->generate(expr);
}

void ModularCodegen::generateStmt(Parser::Statement* stmt) {
    if (stmt) {
        stmtCodegen_->generate(stmt);
    }
}

void ModularCodegen::generateDecl(Parser::ASTNode* decl) {
    if (decl) {
        declCodegen_->generate(decl);
    }
}

void ModularCodegen::generateProgram(const std::vector<std::unique_ptr<Parser::ASTNode>>& nodes) {
    for (const auto& node : nodes) {
        // Handle top-level declarations
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(node.get())) {
            generateDecl(classDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(node.get())) {
            generateDecl(nsDecl);
        } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(node.get())) {
            generateDecl(entryDecl);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(node.get())) {
            generateDecl(enumDecl);
        } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(node.get())) {
            generateDecl(nativeDecl);
        }
    }
}

void ModularCodegen::generateProgramDecls(const std::vector<std::unique_ptr<Parser::Declaration>>& decls) {
    for (const auto& decl : decls) {
        // Handle top-level declarations
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            generateDecl(classDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            generateDecl(nsDecl);
        } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(decl.get())) {
            generateDecl(entryDecl);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            generateDecl(enumDecl);
        } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            generateDecl(nativeDecl);
        }
    }
}

std::string ModularCodegen::getIR() const {
    LLVMIR::LLVMEmitter emitter(ctx_.module());
    return emitter.emit();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
