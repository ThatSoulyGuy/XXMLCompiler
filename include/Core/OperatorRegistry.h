#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <vector>
#include <mutex>
#include <optional>
#include <sstream>
#include "Concepts.h"

namespace XXML::Core {

/// Associativity of operators
enum class Associativity {
    Left,
    Right,
    None  // Non-associative
};

/// Operator type classification
enum class OperatorType {
    Binary,
    Unary,
    Ternary
};

/// Information about a binary operator
struct BinaryOperatorInfo {
    std::string symbol;
    int precedence;  // Higher = tighter binding
    Associativity associativity;

    // Code generation function: (lhs, rhs) -> code
    std::function<std::string(std::string_view, std::string_view)> cppGenerator;
    std::function<std::string(std::string_view, std::string_view)> llvmGenerator;

    // Type information
    std::string returnType;  // Can be "auto" for type inference
    bool isComparison = false;
    bool isLogical = false;
    bool isArithmetic = false;
    bool isAssignment = false;

    BinaryOperatorInfo() = default;
    BinaryOperatorInfo(std::string_view sym, int prec, Associativity assoc)
        : symbol(sym), precedence(prec), associativity(assoc),
          cppGenerator(defaultCppGenerator(sym)),
          returnType("auto") {}

private:
    static std::function<std::string(std::string_view, std::string_view)>
    defaultCppGenerator(std::string_view op) {
        return [op = std::string(op)](std::string_view lhs, std::string_view rhs) {
            return "(" + std::string(lhs) + " " + op + " " + std::string(rhs) + ")";
        };
    }
};

/// Information about a unary operator
struct UnaryOperatorInfo {
    std::string symbol;
    int precedence;
    bool isPrefix;  // true for prefix, false for postfix

    // Code generation function: (operand) -> code
    std::function<std::string(std::string_view)> cppGenerator;
    std::function<std::string(std::string_view)> llvmGenerator;

    std::string returnType;

    UnaryOperatorInfo() = default;
    UnaryOperatorInfo(std::string_view sym, int prec, bool prefix)
        : symbol(sym), precedence(prec), isPrefix(prefix),
          cppGenerator(defaultCppGenerator(sym, prefix)),
          returnType("auto") {}

private:
    static std::function<std::string(std::string_view)>
    defaultCppGenerator(std::string_view op, bool prefix) {
        return [op = std::string(op), prefix](std::string_view operand) {
            if (prefix) {
                return "(" + op + std::string(operand) + ")";
            } else {
                return "(" + std::string(operand) + op + ")";
            }
        };
    }
};

/// Registry for operators with precedence and code generation
class OperatorRegistry {
public:
    OperatorRegistry();
    ~OperatorRegistry() = default;

    // Binary operator registration
    void registerBinaryOperator(const BinaryOperatorInfo& info);
    void registerBinaryOperator(std::string_view symbol, int precedence,
                               Associativity assoc = Associativity::Left);

    template<CallableReturning<std::string, std::string_view, std::string_view> Func>
    void registerBinaryOperatorWithGenerator(std::string_view symbol,
                                            int precedence,
                                            Associativity assoc,
                                            Func&& generator) {
        BinaryOperatorInfo info{symbol, precedence, assoc};
        info.cppGenerator = std::forward<Func>(generator);
        registerBinaryOperator(info);
    }

    // Unary operator registration
    void registerUnaryOperator(const UnaryOperatorInfo& info);
    void registerUnaryOperator(std::string_view symbol, int precedence, bool isPrefix);

    template<CallableReturning<std::string, std::string_view> Func>
    void registerUnaryOperatorWithGenerator(std::string_view symbol,
                                           int precedence,
                                           bool isPrefix,
                                           Func&& generator) {
        UnaryOperatorInfo info{symbol, precedence, isPrefix};
        info.cppGenerator = std::forward<Func>(generator);
        registerUnaryOperator(info);
    }

    // Operator queries
    bool isBinaryOperator(std::string_view symbol) const;
    bool isUnaryOperator(std::string_view symbol) const;
    bool isOperator(std::string_view symbol) const;

    std::optional<BinaryOperatorInfo> getBinaryOperator(std::string_view symbol) const;
    std::optional<UnaryOperatorInfo> getUnaryOperator(std::string_view symbol) const;

    // Precedence queries
    int getPrecedence(std::string_view symbol) const;
    Associativity getAssociativity(std::string_view symbol) const;

    // Code generation
    std::string generateBinaryCpp(std::string_view op,
                                 std::string_view lhs,
                                 std::string_view rhs) const;
    std::string generateUnaryCpp(std::string_view op,
                                std::string_view operand,
                                bool isPrefix) const;

    std::string generateBinaryLLVM(std::string_view op,
                                  std::string_view lhs,
                                  std::string_view rhs) const;
    std::string generateUnaryLLVM(std::string_view op,
                                 std::string_view operand,
                                 bool isPrefix) const;

    // Built-in operators
    void registerBuiltinOperators();

    // Utility
    std::vector<std::string> getAllBinaryOperators() const;
    std::vector<std::string> getAllUnaryOperators() const;
    void clear();

    // Fluent API
    OperatorRegistry& withBinary(std::string_view symbol, int precedence,
                                Associativity assoc = Associativity::Left) {
        registerBinaryOperator(symbol, precedence, assoc);
        return *this;
    }

    OperatorRegistry& withUnary(std::string_view symbol, int precedence, bool isPrefix) {
        registerUnaryOperator(symbol, precedence, isPrefix);
        return *this;
    }

private:
    std::unordered_map<std::string, BinaryOperatorInfo> binaryOperators_;
    std::unordered_map<std::string, UnaryOperatorInfo> unaryOperators_;
    mutable std::mutex mutex_;  // Thread-safety

    // Helper methods
    void registerArithmeticOperators();
    void registerComparisonOperators();
    void registerLogicalOperators();
    void registerAssignmentOperators();
    void registerBitwiseOperators();
};

// Precedence levels (higher = tighter binding)
namespace OperatorPrecedence {
    constexpr int Assignment = 2;
    constexpr int LogicalOr = 4;
    constexpr int LogicalAnd = 5;
    constexpr int BitwiseOr = 6;
    constexpr int BitwiseXor = 7;
    constexpr int BitwiseAnd = 8;
    constexpr int Equality = 9;
    constexpr int Relational = 10;
    constexpr int Shift = 11;
    constexpr int Additive = 12;
    constexpr int Multiplicative = 13;
    constexpr int Unary = 14;
    constexpr int Postfix = 15;
    constexpr int Primary = 16;
}

} // namespace XXML::Core
