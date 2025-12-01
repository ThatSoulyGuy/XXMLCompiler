#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for literal expressions
class LiteralCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitIntegerLiteral(Parser::IntegerLiteralExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
        }

        // Create i64 constant
        auto intValue = ctx_.builder().getInt64(expr->value);
        ctx_.lastExprValue = LLVMIR::AnyValue(intValue);

        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitFloatLiteral(Parser::FloatLiteralExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getFloat(0.0f));
        }

        // Create float constant
        auto floatValue = ctx_.builder().getFloat(expr->value);
        ctx_.lastExprValue = LLVMIR::AnyValue(floatValue);

        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitDoubleLiteral(Parser::DoubleLiteralExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getDouble(0.0));
        }

        // Create double constant
        auto doubleValue = ctx_.builder().getDouble(expr->value);
        ctx_.lastExprValue = LLVMIR::AnyValue(doubleValue);

        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitStringLiteral(Parser::StringLiteralExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Create string literal in global constants
        std::string label = ctx_.allocateStringLabel();
        ctx_.addStringLiteral(label, expr->value);

        // The string literal address is @.label
        // For now, create a global variable reference
        auto* globalStr = ctx_.module().getOrCreateStringLiteral(expr->value);
        auto ptrValue = globalStr->toTypedValue();
        ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);

        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitBoolLiteral(Parser::BoolLiteralExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getInt1(false));
        }

        // Create i1 constant (bool)
        auto boolValue = ctx_.builder().getInt1(expr->value);
        ctx_.lastExprValue = LLVMIR::AnyValue(boolValue);

        return ctx_.lastExprValue;
    }
};

// Factory function to create the literal codegen handler
std::unique_ptr<ExprCodegen> createLiteralCodegen(CodegenContext& ctx) {
    return std::make_unique<LiteralCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
