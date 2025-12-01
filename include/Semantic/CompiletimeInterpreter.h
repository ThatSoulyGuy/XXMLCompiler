#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CompiletimeValue.h"
#include "../Parser/AST.h"

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
    
private:
    SemanticAnalyzer& analyzer_;
    std::unordered_map<std::string, std::unique_ptr<CompiletimeValue>> variables_;
    
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
};

} // namespace Semantic
} // namespace XXML
