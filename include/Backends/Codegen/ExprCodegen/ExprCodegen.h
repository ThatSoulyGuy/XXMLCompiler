#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Parser/AST.h"

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Expression code generation
 *
 * Provides dispatch mechanism and complete implementations for generating
 * IR for all expression types.
 */
class ExprCodegen {
public:
    explicit ExprCodegen(CodegenContext& ctx) : ctx_(ctx) {}
    virtual ~ExprCodegen() = default;

    // Main dispatch - routes to appropriate visitor
    LLVMIR::AnyValue generate(Parser::Expression* expr);

    // === Expression Visitors ===

    // Literals
    virtual LLVMIR::AnyValue visitIntegerLiteral(Parser::IntegerLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitFloatLiteral(Parser::FloatLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitDoubleLiteral(Parser::DoubleLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitStringLiteral(Parser::StringLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitBoolLiteral(Parser::BoolLiteralExpr* expr);

    // Identifiers
    virtual LLVMIR::AnyValue visitIdentifier(Parser::IdentifierExpr* expr);
    virtual LLVMIR::AnyValue visitThis(Parser::ThisExpr* expr);
    virtual LLVMIR::AnyValue visitReference(Parser::ReferenceExpr* expr);

    // Binary operations
    virtual LLVMIR::AnyValue visitBinary(Parser::BinaryExpr* expr);

    // Member access
    virtual LLVMIR::AnyValue visitMemberAccess(Parser::MemberAccessExpr* expr);

    // Calls
    virtual LLVMIR::AnyValue visitCall(Parser::CallExpr* expr);

    // Lambda
    virtual LLVMIR::AnyValue visitLambda(Parser::LambdaExpr* expr);

    // TypeOf
    virtual LLVMIR::AnyValue visitTypeOf(Parser::TypeOfExpr* expr);

protected:
    CodegenContext& ctx_;

    // === Utility Methods ===

    LLVMIR::AnyValue loadIfNeeded(LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca);
    std::string getExpressionType(Parser::Expression* expr) const;
    bool isNumericType(std::string_view type) const;
    bool isIntegerType(std::string_view type) const;
    bool isFloatType(std::string_view type) const;

    // === Property Access Helpers ===

    LLVMIR::AnyValue loadPropertyFromThis(const PropertyInfo& prop);
    LLVMIR::AnyValue loadThisProperty(const std::string& propName);
    LLVMIR::AnyValue loadObjectProperty(const std::string& varName, const std::string& propName);

    // === Call Helpers ===

    LLVMIR::AnyValue handleMemberCall(Parser::CallExpr* expr, Parser::MemberAccessExpr* memberAccess);
    LLVMIR::AnyValue emitCall(const std::string& functionName,
                              const std::vector<LLVMIR::AnyValue>& args,
                              bool isInstanceMethod,
                              LLVMIR::PtrValue instancePtr);

    // === Arithmetic Operations ===

    LLVMIR::AnyValue generateAdd(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateSub(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateMul(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateDiv(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateRem(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);

    // === Comparison Operations ===

    LLVMIR::AnyValue generateEq(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateNe(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateLt(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateLe(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateGt(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);
    LLVMIR::AnyValue generateGe(LLVMIR::AnyValue left, LLVMIR::AnyValue right, const std::string& type);

    // === Bitwise Operations ===

    LLVMIR::AnyValue generateBitAnd(LLVMIR::AnyValue left, LLVMIR::AnyValue right);
    LLVMIR::AnyValue generateBitOr(LLVMIR::AnyValue left, LLVMIR::AnyValue right);
    LLVMIR::AnyValue generateBitXor(LLVMIR::AnyValue left, LLVMIR::AnyValue right);
    LLVMIR::AnyValue generateShl(LLVMIR::AnyValue left, LLVMIR::AnyValue right);
    LLVMIR::AnyValue generateShr(LLVMIR::AnyValue left, LLVMIR::AnyValue right);

    // === Logical Operations (Short-Circuit) ===

    LLVMIR::AnyValue generateLogicalAnd(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult);
    LLVMIR::AnyValue generateLogicalOr(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
