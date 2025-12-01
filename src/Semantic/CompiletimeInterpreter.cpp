#include "../../include/Semantic/CompiletimeInterpreter.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <iostream>  // For debug output

namespace XXML {
namespace Semantic {

CompiletimeInterpreter::CompiletimeInterpreter(SemanticAnalyzer& analyzer)
    : analyzer_(analyzer) {}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evaluate(Parser::Expression* expr) {
    if (!expr) return nullptr;

    // Literals
    auto literal = evalLiteral(expr);
    if (literal) return literal;

    // Binary expressions
    if (auto* binExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return evalBinary(binExpr);
    }

    // Call expressions (including constructors and method calls)
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        return evalCall(callExpr);
    }

    // Member access expressions
    if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        return evalMemberAccess(memberExpr);
    }

    // Lambda expressions
    if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        return evalLambda(lambdaExpr);
    }

    // Identifier expressions
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        return evalIdentifier(identExpr);
    }

    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalLiteral(Parser::Expression* expr) {
    if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        return std::make_unique<CompiletimeInteger>(intLit->value);
    }
    if (auto* floatLit = dynamic_cast<Parser::FloatLiteralExpr*>(expr)) {
        return std::make_unique<CompiletimeFloat>(floatLit->value);
    }
    if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) {
        return std::make_unique<CompiletimeDouble>(doubleLit->value);
    }
    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        return std::make_unique<CompiletimeString>(strLit->value);
    }
    if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        return std::make_unique<CompiletimeBool>(boolLit->value);
    }
    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalBinary(Parser::BinaryExpr* expr) {
    auto left = evaluate(expr->left.get());
    auto right = evaluate(expr->right.get());
    if (!left || !right) return nullptr;

    const std::string& op = expr->op;

    // Integer operations
    if (left->isInteger() && right->isInteger()) {
        int64_t lv = static_cast<CompiletimeInteger*>(left.get())->value;
        int64_t rv = static_cast<CompiletimeInteger*>(right.get())->value;
        if (op == "+") return std::make_unique<CompiletimeInteger>(lv + rv);
        if (op == "-") return std::make_unique<CompiletimeInteger>(lv - rv);
        if (op == "*") return std::make_unique<CompiletimeInteger>(lv * rv);
        if (op == "/" && rv != 0) return std::make_unique<CompiletimeInteger>(lv / rv);
        if (op == "%" && rv != 0) return std::make_unique<CompiletimeInteger>(lv % rv);
        if (op == "&") return std::make_unique<CompiletimeInteger>(lv & rv);
        if (op == "|") return std::make_unique<CompiletimeInteger>(lv | rv);
        if (op == "^") return std::make_unique<CompiletimeInteger>(lv ^ rv);
        if (op == "<<") return std::make_unique<CompiletimeInteger>(lv << rv);
        if (op == ">>") return std::make_unique<CompiletimeInteger>(lv >> rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
        if (op == "<") return std::make_unique<CompiletimeBool>(lv < rv);
        if (op == ">") return std::make_unique<CompiletimeBool>(lv > rv);
        if (op == "<=") return std::make_unique<CompiletimeBool>(lv <= rv);
        if (op == ">=") return std::make_unique<CompiletimeBool>(lv >= rv);
    }

    // Float operations
    if (left->isFloat() && right->isFloat()) {
        float lv = static_cast<CompiletimeFloat*>(left.get())->value;
        float rv = static_cast<CompiletimeFloat*>(right.get())->value;
        if (op == "+") return std::make_unique<CompiletimeFloat>(lv + rv);
        if (op == "-") return std::make_unique<CompiletimeFloat>(lv - rv);
        if (op == "*") return std::make_unique<CompiletimeFloat>(lv * rv);
        if (op == "/" && rv != 0.0f) return std::make_unique<CompiletimeFloat>(lv / rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
        if (op == "<") return std::make_unique<CompiletimeBool>(lv < rv);
        if (op == ">") return std::make_unique<CompiletimeBool>(lv > rv);
        if (op == "<=") return std::make_unique<CompiletimeBool>(lv <= rv);
        if (op == ">=") return std::make_unique<CompiletimeBool>(lv >= rv);
    }

    // Double operations
    if (left->isDouble() && right->isDouble()) {
        double lv = static_cast<CompiletimeDouble*>(left.get())->value;
        double rv = static_cast<CompiletimeDouble*>(right.get())->value;
        if (op == "+") return std::make_unique<CompiletimeDouble>(lv + rv);
        if (op == "-") return std::make_unique<CompiletimeDouble>(lv - rv);
        if (op == "*") return std::make_unique<CompiletimeDouble>(lv * rv);
        if (op == "/" && rv != 0.0) return std::make_unique<CompiletimeDouble>(lv / rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
        if (op == "<") return std::make_unique<CompiletimeBool>(lv < rv);
        if (op == ">") return std::make_unique<CompiletimeBool>(lv > rv);
        if (op == "<=") return std::make_unique<CompiletimeBool>(lv <= rv);
        if (op == ">=") return std::make_unique<CompiletimeBool>(lv >= rv);
    }

    // Mixed float/double promotion
    if ((left->isFloat() && right->isDouble()) || (left->isDouble() && right->isFloat())) {
        double lv = left->isFloat()
            ? static_cast<double>(static_cast<CompiletimeFloat*>(left.get())->value)
            : static_cast<CompiletimeDouble*>(left.get())->value;
        double rv = right->isFloat()
            ? static_cast<double>(static_cast<CompiletimeFloat*>(right.get())->value)
            : static_cast<CompiletimeDouble*>(right.get())->value;
        if (op == "+") return std::make_unique<CompiletimeDouble>(lv + rv);
        if (op == "-") return std::make_unique<CompiletimeDouble>(lv - rv);
        if (op == "*") return std::make_unique<CompiletimeDouble>(lv * rv);
        if (op == "/" && rv != 0.0) return std::make_unique<CompiletimeDouble>(lv / rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
        if (op == "<") return std::make_unique<CompiletimeBool>(lv < rv);
        if (op == ">") return std::make_unique<CompiletimeBool>(lv > rv);
        if (op == "<=") return std::make_unique<CompiletimeBool>(lv <= rv);
        if (op == ">=") return std::make_unique<CompiletimeBool>(lv >= rv);
    }

    // String concatenation
    if (left->isString() && right->isString()) {
        const std::string& lv = static_cast<CompiletimeString*>(left.get())->value;
        const std::string& rv = static_cast<CompiletimeString*>(right.get())->value;
        if (op == "+") return std::make_unique<CompiletimeString>(lv + rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
        if (op == "<") return std::make_unique<CompiletimeBool>(lv < rv);
        if (op == ">") return std::make_unique<CompiletimeBool>(lv > rv);
        if (op == "<=") return std::make_unique<CompiletimeBool>(lv <= rv);
        if (op == ">=") return std::make_unique<CompiletimeBool>(lv >= rv);
    }

    // Bool operations
    if (left->isBool() && right->isBool()) {
        bool lv = static_cast<CompiletimeBool*>(left.get())->value;
        bool rv = static_cast<CompiletimeBool*>(right.get())->value;
        if (op == "&&") return std::make_unique<CompiletimeBool>(lv && rv);
        if (op == "||") return std::make_unique<CompiletimeBool>(lv || rv);
        if (op == "==") return std::make_unique<CompiletimeBool>(lv == rv);
        if (op == "!=") return std::make_unique<CompiletimeBool>(lv != rv);
    }

    return nullptr;
}

bool CompiletimeInterpreter::extractCalleeInfo(Parser::Expression* callee,
                                                std::string& className,
                                                std::string& methodName) {
    // Handle Class::Method pattern (MemberAccessExpr where object is IdentifierExpr)
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(callee)) {
        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
            className = ident->name;
            methodName = memberAccess->member;
            // Strip leading "::" from method name (parser stores it for static calls)
            if (methodName.length() > 2 && methodName.substr(0, 2) == "::") {
                methodName = methodName.substr(2);
            }
            return true;
        }
        // Handle nested member access like Namespace::Class::Method
        if (auto* nested = dynamic_cast<Parser::MemberAccessExpr*>(memberAccess->object.get())) {
            if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(nested->object.get())) {
                std::string nestedMember = nested->member;
                if (nestedMember.length() > 2 && nestedMember.substr(0, 2) == "::") {
                    nestedMember = nestedMember.substr(2);
                }
                className = ident->name + "::" + nestedMember;
                methodName = memberAccess->member;
                if (methodName.length() > 2 && methodName.substr(0, 2) == "::") {
                    methodName = methodName.substr(2);
                }
                return true;
            }
        }
    }
    return false;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalCall(Parser::CallExpr* expr) {
    if (!expr || !expr->callee) return nullptr;

    // First, check if this is a static constructor call (Class::Constructor)
    std::string className, methodName;
    if (extractCalleeInfo(expr->callee.get(), className, methodName)) {
        // Handle built-in type constructors
        if (methodName == "Constructor") {
            // Evaluate all arguments
            std::vector<std::unique_ptr<CompiletimeValue>> evaluatedArgs;
            for (const auto& arg : expr->arguments) {
                auto evalArg = evaluate(arg.get());
                if (!evalArg) return nullptr;  // Argument not compile-time evaluable
                evaluatedArgs.push_back(std::move(evalArg));
            }

            // Integer::Constructor(value)
            if (className == "Integer" && evaluatedArgs.size() == 1) {
                if (evaluatedArgs[0]->isInteger()) {
                    return std::make_unique<CompiletimeInteger>(
                        static_cast<CompiletimeInteger*>(evaluatedArgs[0].get())->value);
                }
            }

            // Float::Constructor(value)
            if (className == "Float" && evaluatedArgs.size() == 1) {
                if (evaluatedArgs[0]->isFloat()) {
                    return std::make_unique<CompiletimeFloat>(
                        static_cast<CompiletimeFloat*>(evaluatedArgs[0].get())->value);
                }
                if (evaluatedArgs[0]->isInteger()) {
                    return std::make_unique<CompiletimeFloat>(
                        static_cast<float>(static_cast<CompiletimeInteger*>(evaluatedArgs[0].get())->value));
                }
            }

            // Double::Constructor(value)
            if (className == "Double" && evaluatedArgs.size() == 1) {
                if (evaluatedArgs[0]->isDouble()) {
                    return std::make_unique<CompiletimeDouble>(
                        static_cast<CompiletimeDouble*>(evaluatedArgs[0].get())->value);
                }
                if (evaluatedArgs[0]->isFloat()) {
                    return std::make_unique<CompiletimeDouble>(
                        static_cast<double>(static_cast<CompiletimeFloat*>(evaluatedArgs[0].get())->value));
                }
                if (evaluatedArgs[0]->isInteger()) {
                    return std::make_unique<CompiletimeDouble>(
                        static_cast<double>(static_cast<CompiletimeInteger*>(evaluatedArgs[0].get())->value));
                }
            }

            // Bool::Constructor(value)
            if (className == "Bool" && evaluatedArgs.size() == 1) {
                if (evaluatedArgs[0]->isBool()) {
                    return std::make_unique<CompiletimeBool>(
                        static_cast<CompiletimeBool*>(evaluatedArgs[0].get())->value);
                }
            }

            // String::Constructor(value)
            if (className == "String" && evaluatedArgs.size() == 1) {
                if (evaluatedArgs[0]->isString()) {
                    return std::make_unique<CompiletimeString>(
                        static_cast<CompiletimeString*>(evaluatedArgs[0].get())->value);
                }
            }
        }
    }

    // Check if this is an instance method call (object.method())
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(expr->callee.get())) {
        // Evaluate the receiver object
        auto receiver = evaluate(memberAccess->object.get());
        if (receiver) {
            // Evaluate arguments
            std::vector<CompiletimeValue*> argPtrs;
            std::vector<std::unique_ptr<CompiletimeValue>> argValues;
            for (const auto& arg : expr->arguments) {
                auto evalArg = evaluate(arg.get());
                if (!evalArg) return nullptr;
                argPtrs.push_back(evalArg.get());
                argValues.push_back(std::move(evalArg));
            }

            return evalMethodCall(receiver.get(), memberAccess->member, argPtrs);
        }
    }

    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalMethodCall(
    CompiletimeValue* receiver, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    if (!receiver) return nullptr;

    if (receiver->isInteger()) {
        return evalIntegerMethod(static_cast<CompiletimeInteger*>(receiver)->value, methodName, args);
    }
    if (receiver->isFloat()) {
        return evalFloatMethod(static_cast<CompiletimeFloat*>(receiver)->value, methodName, args);
    }
    if (receiver->isDouble()) {
        return evalDoubleMethod(static_cast<CompiletimeDouble*>(receiver)->value, methodName, args);
    }
    if (receiver->isBool()) {
        return evalBoolMethod(static_cast<CompiletimeBool*>(receiver)->value, methodName, args);
    }
    if (receiver->isString()) {
        return evalStringMethod(static_cast<CompiletimeString*>(receiver)->value, methodName, args);
    }

    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalMemberAccess(Parser::MemberAccessExpr* expr) {
    auto obj = evaluate(expr->object.get());
    if (!obj) return nullptr;

    if (obj->isObject()) {
        auto* ctObj = static_cast<CompiletimeObject*>(obj.get());
        auto* prop = ctObj->getProperty(expr->member);
        return prop ? prop->clone() : nullptr;
    }

    // Handle property access on strings
    if (obj->isString()) {
        const std::string& str = static_cast<CompiletimeString*>(obj.get())->value;
        if (expr->member == "length") {
            return std::make_unique<CompiletimeInteger>(static_cast<int64_t>(str.length()));
        }
    }

    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalLambda(Parser::LambdaExpr* expr) {
    return std::make_unique<CompiletimeLambda>(expr);
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalIdentifier(Parser::IdentifierExpr* expr) {
    auto* value = getVariable(expr->name);
    return value ? value->clone() : nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalUnary(Parser::Expression* expr) {
    (void)expr;
    // TODO: Handle unary expressions like -x, !b, ~n
    return nullptr;
}

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::executeMethod(
    const std::string& className,
    const std::string& methodName,
    CompiletimeObject* thisObj,
    const std::vector<CompiletimeValue*>& args) {
    (void)className; (void)methodName; (void)thisObj; (void)args;
    return nullptr;
}

bool CompiletimeInterpreter::isCompiletimeEvaluable(Parser::Expression* expr) const {
    if (!expr) return false;

    // Literals are always evaluable
    if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr) ||
        dynamic_cast<Parser::FloatLiteralExpr*>(expr) ||
        dynamic_cast<Parser::DoubleLiteralExpr*>(expr) ||
        dynamic_cast<Parser::StringLiteralExpr*>(expr) ||
        dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        return true;
    }

    // Binary expressions if both operands are evaluable
    if (auto* binExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return isCompiletimeEvaluable(binExpr->left.get()) &&
               isCompiletimeEvaluable(binExpr->right.get());
    }

    // Call expressions - check for built-in constructors
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(callExpr->callee.get())) {
            if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
                // Built-in type constructors
                if ((ident->name == "Integer" || ident->name == "Float" ||
                     ident->name == "Double" || ident->name == "Bool" ||
                     ident->name == "String") && memberAccess->member == "Constructor") {
                    // Check all arguments are evaluable
                    for (const auto& arg : callExpr->arguments) {
                        if (!isCompiletimeEvaluable(arg.get())) return false;
                    }
                    return true;
                }
            }
        }
        // Method calls on evaluable receivers
        if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(callExpr->callee.get())) {
            if (isCompiletimeEvaluable(memberAccess->object.get())) {
                for (const auto& arg : callExpr->arguments) {
                    if (!isCompiletimeEvaluable(arg.get())) return false;
                }
                return true;
            }
        }
    }

    // Lambda expressions with compiletime flag
    if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        return lambdaExpr->isCompiletime;
    }

    // Identifiers that reference compile-time variables
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        auto it = variables_.find(identExpr->name);
        return it != variables_.end();
    }

    return false;
}

void CompiletimeInterpreter::setVariable(const std::string& name, std::unique_ptr<CompiletimeValue> value) {
    variables_[name] = std::move(value);
}

CompiletimeValue* CompiletimeInterpreter::getVariable(const std::string& name) {
    auto it = variables_.find(name);
    return (it != variables_.end()) ? it->second.get() : nullptr;
}

void CompiletimeInterpreter::clearVariables() {
    variables_.clear();
}

// ============================================================================
// Integer Method Evaluation
// ============================================================================

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalIntegerMethod(
    int64_t value, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    // No-argument methods
    if (args.empty()) {
        if (methodName == "negate") return std::make_unique<CompiletimeInteger>(-value);
        if (methodName == "abs") return std::make_unique<CompiletimeInteger>(value < 0 ? -value : value);
        if (methodName == "toString") return std::make_unique<CompiletimeString>(std::to_string(value));
        if (methodName == "toFloat") return std::make_unique<CompiletimeFloat>(static_cast<float>(value));
        if (methodName == "toDouble") return std::make_unique<CompiletimeDouble>(static_cast<double>(value));
        if (methodName == "isZero") return std::make_unique<CompiletimeBool>(value == 0);
        if (methodName == "isPositive") return std::make_unique<CompiletimeBool>(value > 0);
        if (methodName == "isNegative") return std::make_unique<CompiletimeBool>(value < 0);
        if (methodName == "isEven") return std::make_unique<CompiletimeBool>(value % 2 == 0);
        if (methodName == "isOdd") return std::make_unique<CompiletimeBool>(value % 2 != 0);
    }

    // Single-argument methods
    if (args.size() == 1 && args[0]->isInteger()) {
        int64_t arg = static_cast<CompiletimeInteger*>(args[0])->value;
        if (methodName == "add") return std::make_unique<CompiletimeInteger>(value + arg);
        if (methodName == "subtract") return std::make_unique<CompiletimeInteger>(value - arg);
        if (methodName == "multiply") return std::make_unique<CompiletimeInteger>(value * arg);
        if (methodName == "divide" && arg != 0) return std::make_unique<CompiletimeInteger>(value / arg);
        if (methodName == "modulo" && arg != 0) return std::make_unique<CompiletimeInteger>(value % arg);
        if (methodName == "power") {
            int64_t result = 1;
            for (int64_t i = 0; i < arg; ++i) result *= value;
            return std::make_unique<CompiletimeInteger>(result);
        }
        if (methodName == "min") return std::make_unique<CompiletimeInteger>(std::min(value, arg));
        if (methodName == "max") return std::make_unique<CompiletimeInteger>(std::max(value, arg));
        if (methodName == "bitwiseAnd") return std::make_unique<CompiletimeInteger>(value & arg);
        if (methodName == "bitwiseOr") return std::make_unique<CompiletimeInteger>(value | arg);
        if (methodName == "bitwiseXor") return std::make_unique<CompiletimeInteger>(value ^ arg);
        if (methodName == "leftShift") return std::make_unique<CompiletimeInteger>(value << arg);
        if (methodName == "rightShift") return std::make_unique<CompiletimeInteger>(value >> arg);
        if (methodName == "equals") return std::make_unique<CompiletimeBool>(value == arg);
        if (methodName == "notEquals") return std::make_unique<CompiletimeBool>(value != arg);
        if (methodName == "lessThan") return std::make_unique<CompiletimeBool>(value < arg);
        if (methodName == "greaterThan") return std::make_unique<CompiletimeBool>(value > arg);
        if (methodName == "lessOrEqual") return std::make_unique<CompiletimeBool>(value <= arg);
        if (methodName == "greaterOrEqual") return std::make_unique<CompiletimeBool>(value >= arg);
    }

    return nullptr;
}

// ============================================================================
// Float Method Evaluation
// ============================================================================

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalFloatMethod(
    float value, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    // No-argument methods
    if (args.empty()) {
        if (methodName == "negate") return std::make_unique<CompiletimeFloat>(-value);
        if (methodName == "abs") return std::make_unique<CompiletimeFloat>(std::fabs(value));
        if (methodName == "toString") return std::make_unique<CompiletimeString>(std::to_string(value));
        if (methodName == "toInteger") return std::make_unique<CompiletimeInteger>(static_cast<int64_t>(value));
        if (methodName == "toDouble") return std::make_unique<CompiletimeDouble>(static_cast<double>(value));
        if (methodName == "floor") return std::make_unique<CompiletimeFloat>(std::floor(value));
        if (methodName == "ceil") return std::make_unique<CompiletimeFloat>(std::ceil(value));
        if (methodName == "round") return std::make_unique<CompiletimeFloat>(std::round(value));
        if (methodName == "sqrt") return std::make_unique<CompiletimeFloat>(std::sqrt(value));
        if (methodName == "sin") return std::make_unique<CompiletimeFloat>(std::sin(value));
        if (methodName == "cos") return std::make_unique<CompiletimeFloat>(std::cos(value));
        if (methodName == "tan") return std::make_unique<CompiletimeFloat>(std::tan(value));
        if (methodName == "exp") return std::make_unique<CompiletimeFloat>(std::exp(value));
        if (methodName == "log") return std::make_unique<CompiletimeFloat>(std::log(value));
        if (methodName == "isZero") return std::make_unique<CompiletimeBool>(value == 0.0f);
        if (methodName == "isPositive") return std::make_unique<CompiletimeBool>(value > 0.0f);
        if (methodName == "isNegative") return std::make_unique<CompiletimeBool>(value < 0.0f);
    }

    // Single-argument methods with Float
    if (args.size() == 1 && args[0]->isFloat()) {
        float arg = static_cast<CompiletimeFloat*>(args[0])->value;
        if (methodName == "add") return std::make_unique<CompiletimeFloat>(value + arg);
        if (methodName == "subtract") return std::make_unique<CompiletimeFloat>(value - arg);
        if (methodName == "multiply") return std::make_unique<CompiletimeFloat>(value * arg);
        if (methodName == "divide" && arg != 0.0f) return std::make_unique<CompiletimeFloat>(value / arg);
        if (methodName == "power") return std::make_unique<CompiletimeFloat>(std::pow(value, arg));
        if (methodName == "min") return std::make_unique<CompiletimeFloat>(std::min(value, arg));
        if (methodName == "max") return std::make_unique<CompiletimeFloat>(std::max(value, arg));
        if (methodName == "equals") return std::make_unique<CompiletimeBool>(value == arg);
        if (methodName == "notEquals") return std::make_unique<CompiletimeBool>(value != arg);
        if (methodName == "lessThan") return std::make_unique<CompiletimeBool>(value < arg);
        if (methodName == "greaterThan") return std::make_unique<CompiletimeBool>(value > arg);
    }

    return nullptr;
}

// ============================================================================
// Double Method Evaluation
// ============================================================================

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalDoubleMethod(
    double value, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    // No-argument methods
    if (args.empty()) {
        if (methodName == "negate") return std::make_unique<CompiletimeDouble>(-value);
        if (methodName == "abs") return std::make_unique<CompiletimeDouble>(std::fabs(value));
        if (methodName == "toString") return std::make_unique<CompiletimeString>(std::to_string(value));
        if (methodName == "toInteger") return std::make_unique<CompiletimeInteger>(static_cast<int64_t>(value));
        if (methodName == "toFloat") return std::make_unique<CompiletimeFloat>(static_cast<float>(value));
        if (methodName == "floor") return std::make_unique<CompiletimeDouble>(std::floor(value));
        if (methodName == "ceil") return std::make_unique<CompiletimeDouble>(std::ceil(value));
        if (methodName == "round") return std::make_unique<CompiletimeDouble>(std::round(value));
        if (methodName == "sqrt") return std::make_unique<CompiletimeDouble>(std::sqrt(value));
        if (methodName == "sin") return std::make_unique<CompiletimeDouble>(std::sin(value));
        if (methodName == "cos") return std::make_unique<CompiletimeDouble>(std::cos(value));
        if (methodName == "tan") return std::make_unique<CompiletimeDouble>(std::tan(value));
        if (methodName == "asin") return std::make_unique<CompiletimeDouble>(std::asin(value));
        if (methodName == "acos") return std::make_unique<CompiletimeDouble>(std::acos(value));
        if (methodName == "atan") return std::make_unique<CompiletimeDouble>(std::atan(value));
        if (methodName == "exp") return std::make_unique<CompiletimeDouble>(std::exp(value));
        if (methodName == "log") return std::make_unique<CompiletimeDouble>(std::log(value));
        if (methodName == "log10") return std::make_unique<CompiletimeDouble>(std::log10(value));
        if (methodName == "isZero") return std::make_unique<CompiletimeBool>(value == 0.0);
        if (methodName == "isPositive") return std::make_unique<CompiletimeBool>(value > 0.0);
        if (methodName == "isNegative") return std::make_unique<CompiletimeBool>(value < 0.0);
    }

    // Single-argument methods with Double
    if (args.size() == 1 && args[0]->isDouble()) {
        double arg = static_cast<CompiletimeDouble*>(args[0])->value;
        if (methodName == "add") return std::make_unique<CompiletimeDouble>(value + arg);
        if (methodName == "subtract") return std::make_unique<CompiletimeDouble>(value - arg);
        if (methodName == "multiply") return std::make_unique<CompiletimeDouble>(value * arg);
        if (methodName == "divide" && arg != 0.0) return std::make_unique<CompiletimeDouble>(value / arg);
        if (methodName == "power") return std::make_unique<CompiletimeDouble>(std::pow(value, arg));
        if (methodName == "min") return std::make_unique<CompiletimeDouble>(std::min(value, arg));
        if (methodName == "max") return std::make_unique<CompiletimeDouble>(std::max(value, arg));
        if (methodName == "atan2") return std::make_unique<CompiletimeDouble>(std::atan2(value, arg));
        if (methodName == "equals") return std::make_unique<CompiletimeBool>(value == arg);
        if (methodName == "notEquals") return std::make_unique<CompiletimeBool>(value != arg);
        if (methodName == "lessThan") return std::make_unique<CompiletimeBool>(value < arg);
        if (methodName == "greaterThan") return std::make_unique<CompiletimeBool>(value > arg);
    }

    return nullptr;
}

// ============================================================================
// Bool Method Evaluation
// ============================================================================

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalBoolMethod(
    bool value, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    // No-argument methods
    if (args.empty()) {
        if (methodName == "not") return std::make_unique<CompiletimeBool>(!value);
        if (methodName == "toString") return std::make_unique<CompiletimeString>(value ? "true" : "false");
        if (methodName == "toInteger") return std::make_unique<CompiletimeInteger>(value ? 1 : 0);
    }

    // Single-argument methods
    if (args.size() == 1 && args[0]->isBool()) {
        bool arg = static_cast<CompiletimeBool*>(args[0])->value;
        if (methodName == "and") return std::make_unique<CompiletimeBool>(value && arg);
        if (methodName == "or") return std::make_unique<CompiletimeBool>(value || arg);
        if (methodName == "xor") return std::make_unique<CompiletimeBool>(value != arg);
        if (methodName == "equals") return std::make_unique<CompiletimeBool>(value == arg);
        if (methodName == "notEquals") return std::make_unique<CompiletimeBool>(value != arg);
        if (methodName == "implies") return std::make_unique<CompiletimeBool>(!value || arg);
    }

    return nullptr;
}

// ============================================================================
// String Method Evaluation
// ============================================================================

std::unique_ptr<CompiletimeValue> CompiletimeInterpreter::evalStringMethod(
    const std::string& value, const std::string& methodName,
    const std::vector<CompiletimeValue*>& args) {

    // No-argument methods
    if (args.empty()) {
        if (methodName == "length") return std::make_unique<CompiletimeInteger>(static_cast<int64_t>(value.length()));
        if (methodName == "isEmpty") return std::make_unique<CompiletimeBool>(value.empty());
        if (methodName == "toUpperCase") {
            std::string result = value;
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            return std::make_unique<CompiletimeString>(result);
        }
        if (methodName == "toLowerCase") {
            std::string result = value;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return std::make_unique<CompiletimeString>(result);
        }
        if (methodName == "trim") {
            size_t start = value.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) return std::make_unique<CompiletimeString>("");
            size_t end = value.find_last_not_of(" \t\n\r");
            return std::make_unique<CompiletimeString>(value.substr(start, end - start + 1));
        }
        if (methodName == "reverse") {
            std::string result(value.rbegin(), value.rend());
            return std::make_unique<CompiletimeString>(result);
        }
    }

    // Single String argument methods
    if (args.size() == 1 && args[0]->isString()) {
        const std::string& arg = static_cast<CompiletimeString*>(args[0])->value;
        if (methodName == "append" || methodName == "concat") {
            return std::make_unique<CompiletimeString>(value + arg);
        }
        if (methodName == "contains") {
            return std::make_unique<CompiletimeBool>(value.find(arg) != std::string::npos);
        }
        if (methodName == "startsWith") {
            return std::make_unique<CompiletimeBool>(value.rfind(arg, 0) == 0);
        }
        if (methodName == "endsWith") {
            if (arg.length() > value.length()) return std::make_unique<CompiletimeBool>(false);
            return std::make_unique<CompiletimeBool>(
                value.compare(value.length() - arg.length(), arg.length(), arg) == 0);
        }
        if (methodName == "indexOf") {
            size_t pos = value.find(arg);
            return std::make_unique<CompiletimeInteger>(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
        }
        if (methodName == "lastIndexOf") {
            size_t pos = value.rfind(arg);
            return std::make_unique<CompiletimeInteger>(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
        }
        if (methodName == "equals") {
            return std::make_unique<CompiletimeBool>(value == arg);
        }
        if (methodName == "compareTo") {
            return std::make_unique<CompiletimeInteger>(value.compare(arg));
        }
    }

    // Single Integer argument methods
    if (args.size() == 1 && args[0]->isInteger()) {
        int64_t arg = static_cast<CompiletimeInteger*>(args[0])->value;
        if (methodName == "charAt" && arg >= 0 && static_cast<size_t>(arg) < value.length()) {
            return std::make_unique<CompiletimeString>(std::string(1, value[static_cast<size_t>(arg)]));
        }
        if (methodName == "repeat" && arg >= 0) {
            std::string result;
            for (int64_t i = 0; i < arg; ++i) result += value;
            return std::make_unique<CompiletimeString>(result);
        }
    }

    // Two Integer argument methods (substring)
    if (args.size() == 2 && args[0]->isInteger() && args[1]->isInteger()) {
        int64_t start = static_cast<CompiletimeInteger*>(args[0])->value;
        int64_t len = static_cast<CompiletimeInteger*>(args[1])->value;
        if (methodName == "substring" && start >= 0 && len >= 0 &&
            static_cast<size_t>(start) <= value.length()) {
            return std::make_unique<CompiletimeString>(
                value.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
        }
    }

    // Two String argument methods (replace)
    if (args.size() == 2 && args[0]->isString() && args[1]->isString()) {
        const std::string& from = static_cast<CompiletimeString*>(args[0])->value;
        const std::string& to = static_cast<CompiletimeString*>(args[1])->value;
        if (methodName == "replace") {
            std::string result = value;
            size_t pos = 0;
            while ((pos = result.find(from, pos)) != std::string::npos) {
                result.replace(pos, from.length(), to);
                pos += to.length();
            }
            return std::make_unique<CompiletimeString>(result);
        }
        if (methodName == "replaceFirst") {
            std::string result = value;
            size_t pos = result.find(from);
            if (pos != std::string::npos) {
                result.replace(pos, from.length(), to);
            }
            return std::make_unique<CompiletimeString>(result);
        }
    }

    return nullptr;
}

} // namespace Semantic
} // namespace XXML
