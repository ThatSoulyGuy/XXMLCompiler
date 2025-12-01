#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized StmtCodegen for return statements
class ReturnCodegenImpl : public StmtCodegen {
public:
    using StmtCodegen::StmtCodegen;

    void visitReturn(Parser::ReturnStmt* stmt) override {
        if (!stmt) return;

        if (stmt->value) {
            // Evaluate return expression
            auto retValue = exprCodegen_.generate(stmt->value.get());

            // Create typed return
            if (retValue.isInt()) {
                ctx_.builder().createRet(retValue.asInt());
            } else if (retValue.isFloat()) {
                ctx_.builder().createRet(retValue.asFloat());
            } else if (retValue.isPtr()) {
                ctx_.builder().createRet(retValue.asPtr());
            } else {
                // Default to void return
                ctx_.builder().createRetVoid();
            }
        } else {
            // Void return
            ctx_.builder().createRetVoid();
        }
    }

    void visitExit(Parser::ExitStmt* stmt) override {
        // Exit with optional return code
        if (stmt && stmt->exitCode) {
            auto exitCode = exprCodegen_.generate(stmt->exitCode.get());

            // Call exit() function if available, otherwise just return
            // For now, just create a return with the exit code
            if (exitCode.isInt()) {
                ctx_.builder().createRet(exitCode.asInt());
            } else {
                ctx_.builder().createRetVoid();
            }
        } else {
            // Exit with code 0
            ctx_.builder().createRet(ctx_.builder().getInt32(0));
        }
    }
};

// Factory function
std::unique_ptr<StmtCodegen> createReturnCodegen(CodegenContext& ctx, ExprCodegen& exprCodegen) {
    return std::make_unique<ReturnCodegenImpl>(ctx, exprCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
