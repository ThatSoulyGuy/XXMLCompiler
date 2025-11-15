#pragma once
#include <memory>
#include "../Parser/AST.h"
#include "SymbolTable.h"
#include "../Common/Error.h"
#include "../Core/TypeContext.h"

namespace XXML {

// Forward declaration
namespace Core { class CompilationContext; }

namespace Semantic {

class SemanticAnalyzer : public Parser::ASTVisitor {
private:
    SymbolTable* symbolTable_;  // Now points to context's symbol table
    Core::CompilationContext* context_;  // ✅ Use context instead of static state
    Common::ErrorReporter& errorReporter;
    Core::TypeContext typeContext_;  // Type information for code generation
    std::string currentClass;
    std::string currentNamespace;
    bool enableValidation;  // Controls whether to do full validation
    bool inTemplateDefinition;  // True when analyzing template class/method definition
    std::set<std::string> templateTypeParameters;  // Template parameters in current scope

    // Type checking helpers
    bool isCompatibleType(const std::string& expected, const std::string& actual);
    bool isCompatibleOwnership(Parser::OwnershipType expected, Parser::OwnershipType actual);
    std::string getExpressionType(Parser::Expression* expr);
    Parser::OwnershipType getExpressionOwnership(Parser::Expression* expr);

    // TypeContext population helpers
    void registerExpressionType(Parser::Expression* expr,
                                const std::string& xxmlType,
                                Parser::OwnershipType ownership);
    void registerVariableType(const std::string& varName,
                             const std::string& xxmlType,
                             Parser::OwnershipType ownership);
    std::string convertXXMLTypeToCpp(const std::string& xxmlType,
                                    Parser::OwnershipType ownership);

    // Validation helpers
    void validateOwnershipSemantics(Parser::TypeRef* type, const Common::SourceLocation& loc);
    void validateMethodCall(Parser::CallExpr& node);
    void validateConstructorCall(Parser::CallExpr& node);
    bool isTemporaryExpression(Parser::Expression* expr);  // Check if expression is a temporary (rvalue)

    // Temporary storage for expression type information
    std::unordered_map<Parser::Expression*, std::string> expressionTypes;
    std::unordered_map<Parser::Expression*, Parser::OwnershipType> expressionOwnerships;

    // Template tracking
    struct TemplateInstantiation {
        std::string templateName;
        std::vector<Parser::TemplateArgument> arguments;  // Can be type or value arguments
        std::vector<int64_t> evaluatedValues;  // Evaluated constant values for non-type parameters

        bool operator<(const TemplateInstantiation& other) const {
            if (templateName != other.templateName) return templateName < other.templateName;
            if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
            // Compare arguments (simplified - just compare types for now)
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (arguments[i].kind != other.arguments[i].kind) return arguments[i].kind < other.arguments[i].kind;
                if (arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                    if (arguments[i].typeArg != other.arguments[i].typeArg)
                        return arguments[i].typeArg < other.arguments[i].typeArg;
                } else if (i < evaluatedValues.size() && i < other.evaluatedValues.size()) {
                    if (evaluatedValues[i] != other.evaluatedValues[i])
                        return evaluatedValues[i] < other.evaluatedValues[i];
                }
            }
            return false;
        }
    };

    struct MethodTemplateInstantiation {
        std::string className;  // Class containing the method
        std::string methodName;  // Template method name
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;

        bool operator<(const MethodTemplateInstantiation& other) const {
            if (className != other.className) return className < other.className;
            if (methodName != other.methodName) return methodName < other.methodName;
            if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (arguments[i].kind != other.arguments[i].kind) return arguments[i].kind < other.arguments[i].kind;
                if (arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                    if (arguments[i].typeArg != other.arguments[i].typeArg)
                        return arguments[i].typeArg < other.arguments[i].typeArg;
                } else if (i < evaluatedValues.size() && i < other.evaluatedValues.size()) {
                    if (evaluatedValues[i] != other.evaluatedValues[i])
                        return evaluatedValues[i] < other.evaluatedValues[i];
                }
            }
            return false;
        }
    };

    std::unordered_map<std::string, Parser::ClassDecl*> templateClasses;  // Template class name -> definition
    std::set<TemplateInstantiation> templateInstantiations;  // Set of template instantiations

    // Method template tracking (key: className::methodName -> MethodDecl*)
    std::unordered_map<std::string, Parser::MethodDecl*> templateMethods;
    std::set<MethodTemplateInstantiation> methodTemplateInstantiations;

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
    std::set<std::string> importedNamespaces_;  // Track imported namespaces for unqualified name lookup

    // Helper for templates
    void recordTemplateInstantiation(const std::string& templateName, const std::vector<Parser::TemplateArgument>& args);
    void recordMethodTemplateInstantiation(const std::string& className, const std::string& methodName, const std::vector<Parser::TemplateArgument>& args);
    int64_t evaluateConstantExpression(Parser::Expression* expr);  // Evaluate constant expressions at compile time
    bool isTemplateClass(const std::string& className);
    bool isTemplateMethod(const std::string& className, const std::string& methodName);

    // Constraint registry and validation
    struct ConstraintInfo {
        std::string name;
        std::vector<Parser::TemplateParameter> templateParams;
        std::vector<Parser::ConstraintParamBinding> paramBindings;
        std::vector<Parser::RequireStmt*> requirements;
        Parser::ConstraintDecl* astNode;
    };

    std::unordered_map<std::string, ConstraintInfo> constraintRegistry_;  // Constraint name -> info

    bool validateConstraint(const std::string& typeName, const std::vector<std::string>& constraints);
    bool validateConstraintRequirements(const std::string& typeName,
                                       const ConstraintInfo& constraint,
                                       const Common::SourceLocation& loc,
                                       const std::unordered_map<std::string, std::string>& providedSubstitutions = {});
    bool hasMethod(const std::string& className,
                  const std::string& methodName,
                  Parser::TypeRef* returnType);
    bool hasConstructor(const std::string& className,
                       const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes);
    bool evaluateTruthCondition(Parser::Expression* expr,
                               const std::unordered_map<std::string, std::string>& typeSubstitutions);
    bool isTypeCompatible(const std::string& actualType, const std::string& constraintType);

    // Helper for class member lookup
    ClassInfo* findClass(const std::string& className);
    MethodInfo* findMethod(const std::string& className, const std::string& methodName);
    bool validateQualifiedIdentifier(const std::string& qualifiedName, const Common::SourceLocation& loc);

    // Template-aware qualified name parsing
    std::string extractClassName(const std::string& qualifiedName);
    std::string extractMethodName(const std::string& qualifiedName);
    std::string buildQualifiedName(Parser::Expression* expr);

public:
    // Get template instantiations for code generation
    const std::set<TemplateInstantiation>& getTemplateInstantiations() const {
        return templateInstantiations;
    }
    const std::unordered_map<std::string, Parser::ClassDecl*>& getTemplateClasses() const {
        return templateClasses;
    }
    const std::set<MethodTemplateInstantiation>& getMethodTemplateInstantiations() const {
        return methodTemplateInstantiations;
    }

    // Get type context for code generation
    Core::TypeContext& getTypeContext() {
        return typeContext_;
    }
    const Core::TypeContext& getTypeContext() const {
        return typeContext_;
    }
    const std::unordered_map<std::string, Parser::MethodDecl*>& getTemplateMethods() const {
        return templateMethods;
    }

    // Register template classes and instantiations from other modules
    void registerTemplateClass(const std::string& name, Parser::ClassDecl* classDecl) {
        templateClasses[name] = classDecl;
    }

    void mergeTemplateInstantiation(const TemplateInstantiation& inst) {
        templateInstantiations.insert(inst);
    }

    // Public accessor for expression types (used by code generator)
    std::string getExpressionTypePublic(Parser::Expression* expr) {
        return getExpressionType(expr);
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
    void visit(Parser::ConstraintDecl& node) override;

    void visit(Parser::InstantiateStmt& node) override;
    void visit(Parser::RequireStmt& node) override;
    void visit(Parser::AssignmentStmt& node) override;
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
    void visit(Parser::TypeOfExpr& node) override;

    void visit(Parser::TypeRef& node) override;
};

} // namespace Semantic
} // namespace XXML
