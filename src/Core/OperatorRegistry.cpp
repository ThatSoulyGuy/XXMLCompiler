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

    // Multiplicative - register with LLVM generators
    BinaryOperatorInfo mulOp{"*", Multiplicative, Associativity::Left};
    mulOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("mul i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(mulOp);

    BinaryOperatorInfo divOp{"/", Multiplicative, Associativity::Left};
    divOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("sdiv i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(divOp);

    BinaryOperatorInfo modOp{"%", Multiplicative, Associativity::Left};
    modOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("srem i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(modOp);

    // Additive - register with LLVM generators
    BinaryOperatorInfo addOp{"+", Additive, Associativity::Left};
    addOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("add i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(addOp);

    BinaryOperatorInfo subOp{"-", Additive, Associativity::Left};
    subOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("sub i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(subOp);

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

    BinaryOperatorInfo eqOp{"==", Equality, Associativity::Left};
    eqOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp eq i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(eqOp);

    BinaryOperatorInfo neOp{"!=", Equality, Associativity::Left};
    neOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp ne i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(neOp);

    BinaryOperatorInfo ltOp{"<", Relational, Associativity::Left};
    ltOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp slt i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(ltOp);

    BinaryOperatorInfo gtOp{">", Relational, Associativity::Left};
    gtOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp sgt i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(gtOp);

    BinaryOperatorInfo leOp{"<=", Relational, Associativity::Left};
    leOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp sle i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(leOp);

    BinaryOperatorInfo geOp{">=", Relational, Associativity::Left};
    geOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("icmp sge i64 {}, {}", lhs, rhs);
    };
    registerBinaryOperator(geOp);
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
