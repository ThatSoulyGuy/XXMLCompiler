#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Parser/AST.h"
#include "Semantic/CompiletimeValue.h"
#include <optional>

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

    // Quote (AST quasi-quotation)
    virtual LLVMIR::AnyValue visitQuote(Parser::QuoteExpr* expr);

    // Splice placeholder (used within Quote blocks)
    virtual LLVMIR::AnyValue visitSplicePlaceholder(Parser::SplicePlaceholder* expr);

protected:
    CodegenContext& ctx_;

    // === Utility Methods ===

    LLVMIR::AnyValue loadIfNeeded(LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca);
    std::string getExpressionType(Parser::Expression* expr) const;
    bool isNumericType(std::string_view type) const;
    bool isIntegerType(std::string_view type) const;
    bool isFloatType(std::string_view type) const;

    // Unwrap boxed types (Integer, Float, etc.) to their raw primitive values
    LLVMIR::AnyValue unwrapBoxedType(LLVMIR::AnyValue value, const std::string& type);

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

    // === Compile-Time Constant Folding ===

    // Attempt to fold an expression at compile-time
    // Returns std::nullopt if the expression cannot be folded
    std::optional<LLVMIR::AnyValue> tryCompiletimeFold(Parser::Expression* expr);

    // Convert a compile-time value to LLVM IR
    LLVMIR::AnyValue emitConstantValue(Semantic::CompiletimeValue* value);

    // Emit raw LLVM constants for primitive types
    LLVMIR::AnyValue emitRawConstant(Semantic::CompiletimeValue* value);

    // Emit constant for string values
    LLVMIR::AnyValue emitStringConstant(const std::string& value);

    // Emit object constants with nested folding support
    LLVMIR::AnyValue emitObjectConstant(Semantic::CompiletimeObject* obj);

    // Emit NativeType wrapper (Integer, Float, etc.) with constant value
    LLVMIR::AnyValue emitNativeTypeWrapper(Semantic::CompiletimeObject* obj);

    // Emit user-defined compiletime struct with folded properties
    LLVMIR::AnyValue emitCompiletimeStruct(Semantic::CompiletimeObject* obj);

    // Check if a class is a NativeType wrapper (Integer, Float, Double, Bool)
    bool isNativeTypeWrapper(const std::string& className) const;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
