#include "Core/OperatorRegistry.h"
#include <algorithm>
#include <format>

namespace XXML::Core {

OperatorRegistry::OperatorRegistry() {
    // Constructor - operators will be registered via registerBuiltinOperators()
}

void OperatorRegistry::registerBinaryOperator(const BinaryOperatorInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    binaryOperators_[info.symbol] = info;
}

void OperatorRegistry::registerBinaryOperator(std::string_view symbol,
                                             int precedence,
                                             Associativity assoc) {
    BinaryOperatorInfo info{symbol, precedence, assoc};
    registerBinaryOperator(info);
}

void OperatorRegistry::registerUnaryOperator(const UnaryOperatorInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    unaryOperators_[info.symbol] = info;
}

void OperatorRegistry::registerUnaryOperator(std::string_view symbol,
                                            int precedence,
                                            bool isPrefix) {
    UnaryOperatorInfo info{symbol, precedence, isPrefix};
    registerUnaryOperator(info);
}

bool OperatorRegistry::isBinaryOperator(std::string_view symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return binaryOperators_.contains(std::string(symbol));
}

bool OperatorRegistry::isUnaryOperator(std::string_view symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return unaryOperators_.contains(std::string(symbol));
}

bool OperatorRegistry::isOperator(std::string_view symbol) const {
    return isBinaryOperator(symbol) || isUnaryOperator(symbol);
}

std::optional<BinaryOperatorInfo>
OperatorRegistry::getBinaryOperator(std::string_view symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = binaryOperators_.find(std::string(symbol));
    if (it != binaryOperators_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<UnaryOperatorInfo>
OperatorRegistry::getUnaryOperator(std::string_view symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = unaryOperators_.find(std::string(symbol));
    if (it != unaryOperators_.end()) {
        return it->second;
    }
    return std::nullopt;
}

int OperatorRegistry::getPrecedence(std::string_view symbol) const {
    if (auto op = getBinaryOperator(symbol)) {
        return op->precedence;
    }
    if (auto op = getUnaryOperator(symbol)) {
        return op->precedence;
    }
    return 0;  // Unknown operator
}

Associativity OperatorRegistry::getAssociativity(std::string_view symbol) const {
    if (auto op = getBinaryOperator(symbol)) {
        return op->associativity;
    }
    return Associativity::None;
}

std::string OperatorRegistry::generateBinaryCpp(std::string_view op,
                                               std::string_view lhs,
                                               std::string_view rhs) const {
    if (auto opInfo = getBinaryOperator(op)) {
        if (opInfo->cppGenerator) {
            return opInfo->cppGenerator(lhs, rhs);
        }
    }
    // Fallback: simple infix notation
    return std::format("({} {} {})", lhs, op, rhs);
}

std::string OperatorRegistry::generateUnaryCpp(std::string_view op,
                                              std::string_view operand,
                                              bool isPrefix) const {
    if (auto opInfo = getUnaryOperator(op)) {
        if (opInfo->cppGenerator) {
            return opInfo->cppGenerator(operand);
        }
    }
    // Fallback
    if (isPrefix) {
        return std::format("({}{})", op, operand);
    } else {
        return std::format("({}{})", operand, op);
    }
}

std::string OperatorRegistry::generateBinaryLLVM(std::string_view op,
                                                std::string_view lhs,
                                                std::string_view rhs) const {
    if (auto opInfo = getBinaryOperator(op)) {
        if (opInfo->llvmGenerator) {
            return opInfo->llvmGenerator(lhs, rhs);
        }
    }
    // LLVM fallback (would need proper LLVM IR generation)
    return std::format("; LLVM: {} {} {}", lhs, op, rhs);
}

std::string OperatorRegistry::generateUnaryLLVM(std::string_view op,
                                               std::string_view operand,
                                               bool isPrefix) const {
    if (auto opInfo = getUnaryOperator(op)) {
        if (opInfo->llvmGenerator) {
            return opInfo->llvmGenerator(operand);
        }
    }
    // LLVM fallback
    return std::format("; LLVM: {}{}", op, operand);
}

void OperatorRegistry::registerBuiltinOperators() {
    registerArithmeticOperators();
    registerComparisonOperators();
    registerLogicalOperators();
    registerAssignmentOperators();
    registerBitwiseOperators();
}

void OperatorRegistry::registerArithmeticOperators() {
    using namespace OperatorPrecedence;

    // Multiplicative
    registerBinaryOperator("*", Multiplicative, Associativity::Left);
    registerBinaryOperator("/", Multiplicative, Associativity::Left);
    registerBinaryOperator("%", Multiplicative, Associativity::Left);

    // Additive
    registerBinaryOperator("+", Additive, Associativity::Left);
    registerBinaryOperator("-", Additive, Associativity::Left);

    // Unary
    registerUnaryOperator("+", Unary, true);  // Unary plus
    registerUnaryOperator("-", Unary, true);  // Unary minus
    registerUnaryOperator("++", Unary, true); // Pre-increment
    registerUnaryOperator("--", Unary, true); // Pre-decrement
    registerUnaryOperator("++", Postfix, false); // Post-increment
    registerUnaryOperator("--", Postfix, false); // Post-decrement
}

void OperatorRegistry::registerComparisonOperators() {
    using namespace OperatorPrecedence;

    registerBinaryOperator("==", Equality, Associativity::Left);
    registerBinaryOperator("!=", Equality, Associativity::Left);
    registerBinaryOperator("<", Relational, Associativity::Left);
    registerBinaryOperator(">", Relational, Associativity::Left);
    registerBinaryOperator("<=", Relational, Associativity::Left);
    registerBinaryOperator(">=", Relational, Associativity::Left);
}

void OperatorRegistry::registerLogicalOperators() {
    using namespace OperatorPrecedence;

    registerBinaryOperator("&&", LogicalAnd, Associativity::Left);
    registerBinaryOperator("||", LogicalOr, Associativity::Left);
    registerUnaryOperator("!", Unary, true);  // Logical NOT
}

void OperatorRegistry::registerAssignmentOperators() {
    using namespace OperatorPrecedence;

    registerBinaryOperator("=", Assignment, Associativity::Right);
    registerBinaryOperator("+=", Assignment, Associativity::Right);
    registerBinaryOperator("-=", Assignment, Associativity::Right);
    registerBinaryOperator("*=", Assignment, Associativity::Right);
    registerBinaryOperator("/=", Assignment, Associativity::Right);
    registerBinaryOperator("%=", Assignment, Associativity::Right);
}

void OperatorRegistry::registerBitwiseOperators() {
    using namespace OperatorPrecedence;

    registerBinaryOperator("&", BitwiseAnd, Associativity::Left);
    registerBinaryOperator("|", BitwiseOr, Associativity::Left);
    registerBinaryOperator("^", BitwiseXor, Associativity::Left);
    registerBinaryOperator("<<", Shift, Associativity::Left);
    registerBinaryOperator(">>", Shift, Associativity::Left);
    registerUnaryOperator("~", Unary, true);  // Bitwise NOT
}

std::vector<std::string> OperatorRegistry::getAllBinaryOperators() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(binaryOperators_.size());

    for (const auto& [symbol, _] : binaryOperators_) {
        result.push_back(symbol);
    }

    return result;
}

std::vector<std::string> OperatorRegistry::getAllUnaryOperators() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(unaryOperators_.size());

    for (const auto& [symbol, _] : unaryOperators_) {
        result.push_back(symbol);
    }

    return result;
}

void OperatorRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    binaryOperators_.clear();
    unaryOperators_.clear();
}

} // namespace XXML::Core
