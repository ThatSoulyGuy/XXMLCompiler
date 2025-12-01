#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Parser/AST.h"

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Base class for expression code generation
 *
 * Provides dispatch mechanism and shared utilities for generating
 * IR for all expression types. Implementations are in separate files.
 */
class ExprCodegen {
public:
    explicit ExprCodegen(CodegenContext& ctx) : ctx_(ctx) {}
    virtual ~ExprCodegen() = default;

    // Main dispatch - routes to appropriate visitor
    LLVMIR::AnyValue generate(Parser::Expression* expr);

    // === Expression Visitors ===
    // Implemented in separate .cpp files for modularity

    // LiteralCodegen.cpp
    virtual LLVMIR::AnyValue visitIntegerLiteral(Parser::IntegerLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitFloatLiteral(Parser::FloatLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitDoubleLiteral(Parser::DoubleLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitStringLiteral(Parser::StringLiteralExpr* expr);
    virtual LLVMIR::AnyValue visitBoolLiteral(Parser::BoolLiteralExpr* expr);

    // IdentifierCodegen.cpp
    virtual LLVMIR::AnyValue visitIdentifier(Parser::IdentifierExpr* expr);
    virtual LLVMIR::AnyValue visitThis(Parser::ThisExpr* expr);
    virtual LLVMIR::AnyValue visitReference(Parser::ReferenceExpr* expr);

    // BinaryCodegen.cpp
    virtual LLVMIR::AnyValue visitBinary(Parser::BinaryExpr* expr);

    // MemberAccessCodegen.cpp
    virtual LLVMIR::AnyValue visitMemberAccess(Parser::MemberAccessExpr* expr);

    // CallCodegen.cpp (dispatches to specialized call handlers)
    virtual LLVMIR::AnyValue visitCall(Parser::CallExpr* expr);

    // LambdaCodegen.cpp
    virtual LLVMIR::AnyValue visitLambda(Parser::LambdaExpr* expr);

    // TypeOfCodegen.cpp
    virtual LLVMIR::AnyValue visitTypeOf(Parser::TypeOfExpr* expr);

protected:
    CodegenContext& ctx_;

    // === Utility Methods ===

    // Load a value if it's stored in an alloca
    LLVMIR::AnyValue loadIfNeeded(LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca);

    // Get the type of an expression (for operator dispatch)
    std::string getExpressionType(Parser::Expression* expr) const;

    // Check if a type is numeric (for arithmetic operations)
    bool isNumericType(std::string_view type) const;
    bool isIntegerType(std::string_view type) const;
    bool isFloatType(std::string_view type) const;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
