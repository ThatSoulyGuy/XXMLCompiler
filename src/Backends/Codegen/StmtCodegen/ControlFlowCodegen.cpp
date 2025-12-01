#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized StmtCodegen for control flow statements
class ControlFlowCodegenImpl : public StmtCodegen {
public:
    using StmtCodegen::StmtCodegen;

    void visitIf(Parser::IfStmt* stmt) override {
        if (!stmt || !stmt->condition) return;

        auto* func = ctx_.currentFunction();
        if (!func) return;

        // Evaluate condition
        auto condValue = exprCodegen_.generate(stmt->condition.get());

        // Create basic blocks
        auto* thenBlock = func->createBasicBlock("if.then");
        auto* elseBlock = stmt->elseBranch.empty() ? nullptr : func->createBasicBlock("if.else");
        auto* mergeBlock = func->createBasicBlock("if.merge");

        // Convert condition to BoolValue (BoolValue = IntValue)
        LLVMIR::BoolValue condBool = condValue.asInt();

        // Conditional branch
        if (elseBlock) {
            ctx_.builder().createCondBr(condBool, thenBlock, elseBlock);
        } else {
            ctx_.builder().createCondBr(condBool, thenBlock, mergeBlock);
        }

        // Then block
        ctx_.setInsertPoint(thenBlock);
        for (auto& thenStmt : stmt->thenBranch) {
            generate(thenStmt.get());
        }
        // Only add branch if block is not terminated
        if (!ctx_.currentBlock()->getTerminator()) {
            ctx_.builder().createBr(mergeBlock);
        }

        // Else block
        if (elseBlock) {
            ctx_.setInsertPoint(elseBlock);
            for (auto& elseStmt : stmt->elseBranch) {
                generate(elseStmt.get());
            }
            if (!ctx_.currentBlock()->getTerminator()) {
                ctx_.builder().createBr(mergeBlock);
            }
        }

        // Continue in merge block
        ctx_.setInsertPoint(mergeBlock);
    }

    void visitWhile(Parser::WhileStmt* stmt) override {
        if (!stmt || !stmt->condition) return;

        auto* func = ctx_.currentFunction();
        if (!func) return;

        // Create basic blocks
        auto* condBlock = func->createBasicBlock("while.cond");
        auto* bodyBlock = func->createBasicBlock("while.body");
        auto* endBlock = func->createBasicBlock("while.end");

        // Branch to condition block
        ctx_.builder().createBr(condBlock);

        // Condition block
        ctx_.setInsertPoint(condBlock);
        auto condValue = exprCodegen_.generate(stmt->condition.get());
        LLVMIR::BoolValue condBool = condValue.asInt();
        ctx_.builder().createCondBr(condBool, bodyBlock, endBlock);

        // Push loop context for break/continue
        ctx_.pushLoop(condBlock, endBlock);

        // Body block
        ctx_.setInsertPoint(bodyBlock);
        for (auto& bodyStmt : stmt->body) {
            generate(bodyStmt.get());
        }
        if (!ctx_.currentBlock()->getTerminator()) {
            ctx_.builder().createBr(condBlock);
        }

        // Pop loop context
        ctx_.popLoop();

        // Continue in end block
        ctx_.setInsertPoint(endBlock);
    }

    void visitBreak(Parser::BreakStmt*) override {
        auto* loopCtx = ctx_.currentLoop();
        if (!loopCtx) return;

        ctx_.builder().createBr(loopCtx->endBlock);
    }

    void visitContinue(Parser::ContinueStmt*) override {
        auto* loopCtx = ctx_.currentLoop();
        if (!loopCtx) return;

        ctx_.builder().createBr(loopCtx->condBlock);
    }
};

// Factory function
std::unique_ptr<StmtCodegen> createControlFlowCodegen(CodegenContext& ctx, ExprCodegen& exprCodegen) {
    return std::make_unique<ControlFlowCodegenImpl>(ctx, exprCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
