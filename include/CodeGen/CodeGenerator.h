#pragma once
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <set>
#include <vector>
#include "../Parser/AST.h"
#include "../Common/Error.h"

namespace XXML {
namespace Semantic {
    class SemanticAnalyzer;  // Forward declaration
}

namespace CodeGen {

class CodeGenerator : public Parser::ASTVisitor {
private:
    std::stringstream output;
    int indentLevel;
    Common::ErrorReporter& errorReporter;
    Semantic::SemanticAnalyzer* semanticAnalyzer;  // For template data

    // Helper methods
    void indent();
    void writeLine(const std::string& line);
    void write(const std::string& text);
    std::string getOwnershipType(Parser::OwnershipType ownership, const std::string& typeName);
    std::string convertType(const std::string& xxmlType);
    std::string sanitizeIdentifier(const std::string& name);

    // New helper methods for comprehensive codegen
    bool isPrimitiveType(const std::string& typeName);
    bool isBuiltinType(const std::string& typeName);
    bool isValueType(const std::string& cppType);
    std::string getParameterType(Parser::OwnershipType ownership, const std::string& typeName);
    bool isSmartPointerType(const std::string& typeName, Parser::OwnershipType ownership);

    // Code generation state
    bool inClassDefinition;
    std::string currentClassName;
    std::string currentNamespace;
    std::string currentMethodReturnType;  // Track current method's return type for template inference
    std::string currentExpectedType;  // Track expected type for initializer expressions
    bool generatingDeclarationsOnly;  // True = only method signatures, False = full implementations
    bool generatingImplementationsOnly;  // True = only method implementations (outside class)

    // Track which expressions result in smart pointers
    std::unordered_map<Parser::Expression*, bool> expressionIsSmartPointer;

    // Track which variables are smart pointers (by variable name)
    std::unordered_map<std::string, bool> variableIsSmartPointer;

    // Track ownership type of each variable (for copy vs reference parameters)
    std::unordered_map<std::string, Parser::OwnershipType> variableOwnership;

    // Track type name of each variable (to check if NativeType)
    std::unordered_map<std::string, std::string> variableTypeName;

    // Template code generation
    std::string mangleTemplateName(const std::string& templateName, const std::vector<std::string>& args);
    std::unique_ptr<Parser::ClassDecl> cloneClassDecl(Parser::ClassDecl* original);
    void substituteTypes(Parser::ClassDecl* classDecl, const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInTypeRef(Parser::TypeRef* typeRef, const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInStatement(Parser::Statement* stmt, const std::unordered_map<std::string, std::string>& typeMap);
    void substituteTypesInExpression(Parser::Expression* expr, const std::unordered_map<std::string, std::string>& typeMap);
    void generateTemplateInstantiations();

    // Method template code generation
    std::unique_ptr<Parser::MethodDecl> cloneMethodDecl(Parser::MethodDecl* original);
    void substituteTypesInMethod(Parser::MethodDecl* methodDecl, const std::unordered_map<std::string, std::string>& typeMap);

    // Wildcard support
    void generateAnyWrapperClass();
    bool needsAnyWrapper;

public:
    CodeGenerator(Common::ErrorReporter& reporter);
    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer);
    void setGeneratingDeclarationsOnly(bool declarationsOnly) { generatingDeclarationsOnly = declarationsOnly; }
    void setGeneratingImplementationsOnly(bool implementationsOnly) { generatingImplementationsOnly = implementationsOnly; }

    std::string generate(Parser::Program& program, bool includeHeaders = true);
    std::string getOutput() const;

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

    void visit(Parser::TypeRef& node) override;
};

} // namespace CodeGen
} // namespace XXML
