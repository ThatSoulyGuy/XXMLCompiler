#pragma once
#include <memory>
#include "../Parser/AST.h"
#include "SymbolTable.h"
#include "../Common/Error.h"

namespace XXML {

// Forward declaration
namespace Core { class CompilationContext; }

namespace Semantic {

class SemanticAnalyzer : public Parser::ASTVisitor {
private:
    SymbolTable* symbolTable_;  // Now points to context's symbol table
    Core::CompilationContext* context_;  // ✅ Use context instead of static state
    Common::ErrorReporter& errorReporter;
    std::string currentClass;
    std::string currentNamespace;
    bool enableValidation;  // Controls whether to do full validation

    // Type checking helpers
    bool isCompatibleType(const std::string& expected, const std::string& actual);
    bool isCompatibleOwnership(Parser::OwnershipType expected, Parser::OwnershipType actual);
    std::string getExpressionType(Parser::Expression* expr);
    Parser::OwnershipType getExpressionOwnership(Parser::Expression* expr);

    // Validation helpers
    void validateOwnershipSemantics(Parser::TypeRef* type, const Common::SourceLocation& loc);
    void validateMethodCall(Parser::CallExpr& node);
    void validateConstructorCall(Parser::CallExpr& node);

    // Temporary storage for expression type information
    std::unordered_map<Parser::Expression*, std::string> expressionTypes;
    std::unordered_map<Parser::Expression*, Parser::OwnershipType> expressionOwnerships;

    // Template tracking
    std::unordered_map<std::string, Parser::ClassDecl*> templateClasses;  // Template class name -> definition
    std::set<std::pair<std::string, std::vector<std::string>>> templateInstantiations;  // Set of (template_name, args)

    // Class member registry for validation
    struct MethodInfo {
        std::string returnType;
        Parser::OwnershipType returnOwnership;
        std::vector<std::pair<std::string, Parser::OwnershipType>> parameters; // (type, ownership) pairs
        bool isConstructor;
    };

    struct ClassInfo {
        std::string qualifiedName;  // Full name including namespace
        std::unordered_map<std::string, MethodInfo> methods;
        std::unordered_map<std::string, std::pair<std::string, Parser::OwnershipType>> properties; // name -> (type, ownership)
        Parser::ClassDecl* astNode;
    };

    // ✅ REMOVED STATIC STATE - now instance-based in context
    std::unordered_map<std::string, ClassInfo> classRegistry_;  // Qualified class name -> ClassInfo
    std::set<std::string> validNamespaces_;  // Track all valid namespaces

    // Helper for templates
    void recordTemplateInstantiation(const std::string& templateName, const std::vector<std::string>& args);
    bool isTemplateClass(const std::string& className);

    // Helper for class member lookup
    ClassInfo* findClass(const std::string& className);
    MethodInfo* findMethod(const std::string& className, const std::string& methodName);
    bool validateQualifiedIdentifier(const std::string& qualifiedName, const Common::SourceLocation& loc);

public:
    // Get template instantiations for code generation
    const std::set<std::pair<std::string, std::vector<std::string>>>& getTemplateInstantiations() const {
        return templateInstantiations;
    }
    const std::unordered_map<std::string, Parser::ClassDecl*>& getTemplateClasses() const {
        return templateClasses;
    }

public:
    // ✅ NEW: Accept CompilationContext for registry access
    SemanticAnalyzer(Core::CompilationContext& context, Common::ErrorReporter& reporter);

    // Legacy constructor for backwards compatibility
    SemanticAnalyzer(Common::ErrorReporter& reporter);

    void analyze(Parser::Program& program);

    // Control validation (for two-phase analysis)
    void setValidationEnabled(bool enabled) { enableValidation = enabled; }
    bool isValidationEnabled() const { return enableValidation; }

    // Visitor methods
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
};

} // namespace Semantic
} // namespace XXML
