#pragma once

#include "../Core/IBackend.h"
#include "../Parser/AST.h"
#include <sstream>
#include <unordered_map>

namespace XXML {

namespace Core { class CompilationContext; }

namespace Backends {

/**
 * @brief LLVM IR code generation backend
 *
 * Generates LLVM Intermediate Representation for the XXML AST.
 * This enables:
 * - Better optimization (LLVM's optimization passes)
 * - Multiple target architectures (x86, ARM, WebAssembly, etc.)
 * - JIT compilation
 * - Link-time optimization
 *
 * Example LLVM IR output:
 * @code
 * define i64 @add(i64 %a, i64 %b) {
 *   %result = add i64 %a, %b
 *   ret i64 %result
 * }
 * @endcode
 */
class LLVMBackend : public Core::BackendBase, public Parser::ASTVisitor {
public:
    explicit LLVMBackend(Core::CompilationContext* context = nullptr);
    ~LLVMBackend() override = default;

    // IBackend interface
    std::string targetName() const override { return "LLVM IR"; }
    Core::BackendTarget targetType() const override { return Core::BackendTarget::LLVM_IR; }
    std::string version() const override { return "1.0.0 (LLVM 17)"; }

    bool supportsFeature(std::string_view feature) const override;

    void initialize(Core::CompilationContext& context) override;

    std::string generate(Parser::Program& program) override;
    std::string generateHeader(Parser::Program& program) override;
    std::string generateImplementation(Parser::Program& program) override;

    std::string generatePreamble() override;
    std::vector<std::string> getRequiredIncludes() const override;
    std::vector<std::string> getRequiredLibraries() const override;

    std::string convertType(std::string_view xxmlType) const override;
    std::string convertOwnership(std::string_view type,
                                std::string_view ownershipIndicator) const override;

    // ASTVisitor interface - simplified for IR generation
    void visit(Parser::Program& node) override;
    void visit(Parser::ImportDecl& node) override;
    void visit(Parser::NamespaceDecl& node) override;
    void visit(Parser::ClassDecl& node) override;
    void visit(Parser::AccessSection& node) override;
    void visit(Parser::PropertyDecl& node) override;
    void visit(Parser::ConstructorDecl& node) override;
    void visit(Parser::MethodDecl& node) override;
    void visit(Parser::ParameterDecl& node) override;
    void visit(Parser::EntrypointDecl& node) override;

    void visit(Parser::InstantiateStmt& node) override;
    void visit(Parser::RunStmt& node) override;
    void visit(Parser::ForStmt& node) override;
    void visit(Parser::ExitStmt& node) override;
    void visit(Parser::ReturnStmt& node) override;
    void visit(Parser::IfStmt& node) override;
    void visit(Parser::WhileStmt& node) override;
    void visit(Parser::BreakStmt& node) override;
    void visit(Parser::ContinueStmt& node) override;

    void visit(Parser::IntegerLiteralExpr& node) override;
    void visit(Parser::StringLiteralExpr& node) override;
    void visit(Parser::BoolLiteralExpr& node) override;
    void visit(Parser::ThisExpr& node) override;
    void visit(Parser::IdentifierExpr& node) override;
    void visit(Parser::ReferenceExpr& node) override;
    void visit(Parser::MemberAccessExpr& node) override;
    void visit(Parser::CallExpr& node) override;
    void visit(Parser::BinaryExpr& node) override;
    void visit(Parser::TypeRef& node) override;

private:
    std::stringstream output_;
    std::unordered_map<std::string, std::string> valueMap_;  // XXML var -> LLVM register
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    std::string allocateRegister();
    std::string allocateLabel(std::string_view prefix);
    void emitLine(const std::string& line);

    /// Map XXML types to LLVM types using TypeRegistry
    std::string getLLVMType(const std::string& xxmlType);

    /// Generate LLVM instruction for binary operation using OperatorRegistry
    std::string generateBinaryOp(const std::string& op,
                                const std::string& lhs,
                                const std::string& rhs,
                                const std::string& type);
};

} // namespace Backends
} // namespace XXML
