#pragma once

#include "../Core/IBackend.h"
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include <sstream>
#include <unordered_map>
#include <memory>

namespace XXML {

// Forward declarations
namespace Semantic { class SemanticAnalyzer; }
namespace Core { class CompilationContext; }

namespace Backends {

/// C++20 code generation backend
/// Replaces the old CodeGenerator with registry-based type and operator handling
class Cpp20Backend : public Core::BackendBase, public Parser::ASTVisitor {
public:
    explicit Cpp20Backend(Core::CompilationContext* context = nullptr);
    ~Cpp20Backend() override = default;

    // IBackend interface implementation
    std::string targetName() const override { return "C++20"; }
    Core::BackendTarget targetType() const override { return Core::BackendTarget::Cpp20; }
    std::string version() const override { return "1.0.0"; }

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

    // ASTVisitor interface implementation
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
    void visit(Parser::AssignmentStmt& node) override;

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

    std::string getOutput() const { return output_.str(); }

    // Semantic analyzer integration (for template data)
    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) {
        semanticAnalyzer_ = analyzer;
    }

private:
    std::stringstream output_;
    Semantic::SemanticAnalyzer* semanticAnalyzer_ = nullptr;

    // Code generation state
    bool inClassDefinition_ = false;
    std::string currentClassName_;
    std::string currentNamespace_;
    bool generatingDeclarationsOnly_ = false;
    bool generatingImplementationsOnly_ = false;

    // Track which expressions result in smart pointers
    std::unordered_map<Parser::Expression*, bool> expressionIsSmartPointer_;
    std::unordered_map<std::string, bool> variableIsSmartPointer_;

    // Helper methods (now using registries instead of hardcoded logic)
    void writeLine(const std::string& line);
    void write(const std::string& text);

    /// Get ownership type using TypeRegistry instead of hardcoded checks
    std::string getOwnershipType(Parser::OwnershipType ownership, const std::string& typeName);

    /// Get parameter type using TypeRegistry
    std::string getParameterType(Parser::OwnershipType ownership, const std::string& typeName);

    /// Check if type requires smart pointer (uses TypeRegistry)
    bool requiresSmartPointer(const std::string& typeName, Parser::OwnershipType ownership) const;

    std::string sanitizeIdentifier(const std::string& name);

    // Template code generation
    std::string mangleTemplateName(const std::string& templateName,
                                  const std::vector<std::string>& args) const;
    std::unique_ptr<Parser::ClassDecl> cloneClassDecl(Parser::ClassDecl* original);
    std::unique_ptr<Parser::ClassDecl> cloneAndSubstituteClassDecl(
        Parser::ClassDecl* original,
        const std::string& newName,
        const std::unordered_map<std::string, std::string>& typeMap);

    // AST cloning helpers
    std::unique_ptr<Parser::TypeRef> cloneTypeRef(Parser::TypeRef* original,
                                                   const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::Statement> cloneStatement(Parser::Statement* stmt,
                                                       const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::Expression> cloneExpression(Parser::Expression* expr,
                                                        const std::unordered_map<std::string, std::string>& typeMap);

    void substituteTypes(Parser::ClassDecl* classDecl,
                        const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInTypeRef(Parser::TypeRef* typeRef,
                                 const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInStatement(Parser::Statement* stmt,
                                   const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInExpression(Parser::Expression* expr,
                                    const std::unordered_map<std::string, std::string>& typeMap);
    void generateTemplateInstantiations();

    // Mode setters (for header/implementation separation)
    void setGeneratingDeclarationsOnly(bool declarationsOnly) {
        generatingDeclarationsOnly_ = declarationsOnly;
    }
    void setGeneratingImplementationsOnly(bool implementationsOnly) {
        generatingImplementationsOnly_ = implementationsOnly;
    }
};

} // namespace Backends
} // namespace XXML
