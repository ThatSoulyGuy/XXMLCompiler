#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for binary expressions
class BinaryCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitBinary(Parser::BinaryExpr* expr) override {
        if (!expr || !expr->left || !expr->right) {
            return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
        }

        // Evaluate left operand
        auto leftResult = generate(expr->left.get());

        // Short-circuit for logical operators
        if (expr->op == "&&" || expr->op == "and") {
            return generateLogicalAnd(expr, leftResult);
        }
        if (expr->op == "||" || expr->op == "or") {
            return generateLogicalOr(expr, leftResult);
        }

        // Evaluate right operand
        auto rightResult = generate(expr->right.get());

        // Determine operand types
        std::string leftType = getExpressionType(expr->left.get());
        std::string rightType = getExpressionType(expr->right.get());

        // Dispatch based on operator
        const std::string& op = expr->op;

        // Arithmetic operators
        if (op == "+") return generateAdd(leftResult, rightResult, leftType);
        if (op == "-") return generateSub(leftResult, rightResult, leftType);
        if (op == "*") return generateMul(leftResult, rightResult, leftType);
        if (op == "/") return generateDiv(leftResult, rightResult, leftType);
        if (op == "%") return generateRem(leftResult, rightResult, leftType);

        // Comparison operators
        if (op == "==" || op == "is") return generateEq(leftResult, rightResult, leftType);
        if (op == "!=" || op == "isnt") return generateNe(leftResult, rightResult, leftType);
        if (op == "<") return generateLt(leftResult, rightResult, leftType);
        if (op == "<=") return generateLe(leftResult, rightResult, leftType);
        if (op == ">") return generateGt(leftResult, rightResult, leftType);
        if (op == ">=") return generateGe(leftResult, rightResult, leftType);

        // Bitwise operators
        if (op == "&") return generateBitAnd(leftResult, rightResult);
        if (op == "|") return generateBitOr(leftResult, rightResult);
        if (op == "^") return generateBitXor(leftResult, rightResult);
        if (op == "<<") return generateShl(leftResult, rightResult);
        if (op == ">>") return generateShr(leftResult, rightResult);

        // Unknown operator - return left operand
        ctx_.lastExprValue = leftResult;
        return ctx_.lastExprValue;
    }

private:
    // === Arithmetic Operations ===

    LLVMIR::AnyValue generateAdd(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                  const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFAdd(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createAdd(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateSub(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                  const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFSub(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createSub(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateMul(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                  const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFMul(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createMul(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateDiv(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                  const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFDiv(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createSDiv(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateRem(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                  const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFRem(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createSRem(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    // === Comparison Operations ===

    LLVMIR::AnyValue generateEq(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpOEQ(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else if (left.isPtr() || right.isPtr()) {
            auto result = ctx_.builder().createPtrEQ(left.asPtr(), right.asPtr());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpEQ(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateNe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpONE(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else if (left.isPtr() || right.isPtr()) {
            auto result = ctx_.builder().createPtrNE(left.asPtr(), right.asPtr());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpNE(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateLt(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpOLT(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpSLT(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateLe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpOLE(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpSLE(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateGt(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpOGT(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpSGT(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateGe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                 const std::string& type) {
        if (isFloatType(type)) {
            auto result = ctx_.builder().createFCmpOGE(left.asFloat(), right.asFloat());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        } else {
            auto result = ctx_.builder().createICmpSGE(left.asInt(), right.asInt());
            ctx_.lastExprValue = LLVMIR::AnyValue(result);
        }
        return ctx_.lastExprValue;
    }

    // === Bitwise Operations ===

    LLVMIR::AnyValue generateBitAnd(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
        auto result = ctx_.builder().createAnd(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateBitOr(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
        auto result = ctx_.builder().createOr(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateBitXor(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
        auto result = ctx_.builder().createXor(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateShl(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
        auto result = ctx_.builder().createShl(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateShr(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
        auto result = ctx_.builder().createAShr(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
        return ctx_.lastExprValue;
    }

    // === Logical Operations (Short-Circuit) ===

    LLVMIR::AnyValue generateLogicalAnd(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult) {
        auto* func = ctx_.currentFunction();
        if (!func) {
            return LLVMIR::AnyValue(ctx_.builder().getInt1(false));
        }

        // Create blocks
        auto* rhsBlock = func->createBasicBlock("and.rhs");
        auto* mergeBlock = func->createBasicBlock("and.merge");

        // Branch based on left value
        ctx_.builder().createCondBr(leftResult.asInt(), rhsBlock, mergeBlock);

        // RHS block
        ctx_.setInsertPoint(rhsBlock);
        auto rightResult = generate(expr->right.get());
        ctx_.builder().createBr(mergeBlock);
        auto* fromRhs = ctx_.currentBlock();

        // Merge block with PHI
        ctx_.setInsertPoint(mergeBlock);
        auto* phiNode = ctx_.builder().createIntPHI(ctx_.module().getContext().getInt1Ty(), "and.result");
        phiNode->addIncoming(ctx_.builder().getInt1(false), ctx_.currentBlock());  // from original
        phiNode->addIncoming(rightResult.asInt(), fromRhs);

        ctx_.lastExprValue = LLVMIR::AnyValue(phiNode->result());
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue generateLogicalOr(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult) {
        auto* func = ctx_.currentFunction();
        if (!func) {
            return LLVMIR::AnyValue(ctx_.builder().getInt1(true));
        }

        // Create blocks
        auto* rhsBlock = func->createBasicBlock("or.rhs");
        auto* mergeBlock = func->createBasicBlock("or.merge");

        // Branch based on left value (if true, skip RHS)
        ctx_.builder().createCondBr(leftResult.asInt(), mergeBlock, rhsBlock);

        // RHS block
        ctx_.setInsertPoint(rhsBlock);
        auto rightResult = generate(expr->right.get());
        ctx_.builder().createBr(mergeBlock);
        auto* fromRhs = ctx_.currentBlock();

        // Merge block with PHI
        ctx_.setInsertPoint(mergeBlock);
        auto* phiNode = ctx_.builder().createIntPHI(ctx_.module().getContext().getInt1Ty(), "or.result");
        phiNode->addIncoming(ctx_.builder().getInt1(true), ctx_.currentBlock());  // from original
        phiNode->addIncoming(rightResult.asInt(), fromRhs);

        ctx_.lastExprValue = LLVMIR::AnyValue(phiNode->result());
        return ctx_.lastExprValue;
    }
};

// Factory function
std::unique_ptr<ExprCodegen> createBinaryCodegen(CodegenContext& ctx) {
    return std::make_unique<BinaryCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
