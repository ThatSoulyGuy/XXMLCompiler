#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../Parser/AST.h"
#include "CompiletimeValue.h"

namespace XXML {
namespace Semantic {

// Forward declaration
class SemanticAnalyzer;

class CompiletimeInterpreter {
public:
    explicit CompiletimeInterpreter(SemanticAnalyzer& analyzer);
    
    // Evaluate any expression to a compile-time value
    // Returns nullptr if the expression cannot be evaluated at compile-time
    std::unique_ptr<CompiletimeValue> evaluate(Parser::Expression* expr);
    
    // Execute a compile-time method body
    std::unique_ptr<CompiletimeValue> executeMethod(
        const std::string& className, 
        const std::string& methodName,
        CompiletimeObject* thisObj, 
        const std::vector<CompiletimeValue*>& args);
    
    // Check if an expression can be evaluated at compile-time
    bool isCompiletimeEvaluable(Parser::Expression* expr) const;
    
    // Variable management for compile-time evaluation
    void setVariable(const std::string& name, std::unique_ptr<CompiletimeValue> value);
    CompiletimeValue* getVariable(const std::string& name);
    void clearVariables();

    // Scope management for nested evaluation
    void pushScope();
    void popScope();

    // Statement execution for compile-time evaluation
    bool executeStatement(Parser::Statement* stmt);

    // User-defined class support
    Parser::ClassDecl* findCompiletimeClass(const std::string& className);
    std::unique_ptr<CompiletimeValue> evalUserDefinedConstructor(
        Parser::ClassDecl* classDecl, const std::vector<CompiletimeValue*>& args);
    Parser::ConstructorDecl* findConstructor(Parser::ClassDecl* classDecl, size_t argCount = 0);

private:
    SemanticAnalyzer& analyzer_;
    std::unordered_map<std::string, std::unique_ptr<CompiletimeValue>> variables_;

    // Scope frame wrapper to avoid MSVC template instantiation issues
    struct ScopeFrame {
        std::unordered_map<std::string, std::unique_ptr<CompiletimeValue>> vars;

        ScopeFrame() = default;
        ScopeFrame(ScopeFrame&&) = default;
        ScopeFrame& operator=(ScopeFrame&&) = default;

        // Explicitly delete copy operations
        ScopeFrame(const ScopeFrame&) = delete;
        ScopeFrame& operator=(const ScopeFrame&) = delete;
    };
    std::vector<ScopeFrame> scopes_;
    std::unique_ptr<CompiletimeValue> lastReturnValue_;
    
    // Expression evaluation helpers
    std::unique_ptr<CompiletimeValue> evalLiteral(Parser::Expression* expr);
    std::unique_ptr<CompiletimeValue> evalBinary(Parser::BinaryExpr* expr);
    std::unique_ptr<CompiletimeValue> evalCall(Parser::CallExpr* expr);
    std::unique_ptr<CompiletimeValue> evalMemberAccess(Parser::MemberAccessExpr* expr);
    std::unique_ptr<CompiletimeValue> evalLambda(Parser::LambdaExpr* expr);
    std::unique_ptr<CompiletimeValue> evalIdentifier(Parser::IdentifierExpr* expr);
    
    // Built-in method evaluation for primitives
    std::unique_ptr<CompiletimeValue> evalIntegerMethod(
        int64_t value, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);
    std::unique_ptr<CompiletimeValue> evalFloatMethod(
        float value, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);
    std::unique_ptr<CompiletimeValue> evalDoubleMethod(
        double value, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);
    std::unique_ptr<CompiletimeValue> evalBoolMethod(
        bool value, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);
    std::unique_ptr<CompiletimeValue> evalStringMethod(
        const std::string& value, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);

    // Unary expression evaluation
    std::unique_ptr<CompiletimeValue> evalUnary(Parser::Expression* expr);

    // Helper to extract class::method from a callee expression
    bool extractCalleeInfo(Parser::Expression* callee, std::string& className, std::string& methodName);

    // Helper to evaluate a method call on a compile-time value
    std::unique_ptr<CompiletimeValue> evalMethodCall(
        CompiletimeValue* receiver, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);

    // Compile-time reflection support
    // Creates a CompiletimeTypeInfo from a class name by looking up the class registry
    std::unique_ptr<CompiletimeTypeInfo> createTypeInfo(const std::string& className);

    // Evaluates reflection method calls on CompiletimeTypeInfo
    std::unique_ptr<CompiletimeValue> evalTypeInfoMethod(
        CompiletimeTypeInfo* typeInfo, const std::string& methodName,
        const std::vector<CompiletimeValue*>& args);
};

} // namespace Semantic
} // namespace XXML
