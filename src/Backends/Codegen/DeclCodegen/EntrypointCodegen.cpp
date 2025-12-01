#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized DeclCodegen for entrypoint (main function)
class EntrypointCodegenImpl : public DeclCodegen {
public:
    using DeclCodegen::DeclCodegen;

    void visitEntrypoint(Parser::EntrypointDecl* decl) override {
        if (!decl) return;

        ctx_.pushScope();

        // Create main function: i32 @main()
        std::vector<LLVMIR::Type*> paramTypes;  // No parameters
        auto* returnType = ctx_.module().getContext().getInt32Ty();
        auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes, false);
        auto* mainFunc = ctx_.module().createFunction(funcType, "main");

        if (!mainFunc) {
            ctx_.popScope();
            return;
        }

        ctx_.setCurrentFunction(mainFunc);
        ctx_.setCurrentReturnType("i32");

        // Create entry block
        auto* entryBlock = mainFunc->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBlock);

        // Generate body
        generateFunctionBody(decl->body);

        // Return 0 if no terminator
        if (!ctx_.builder().getInsertBlock()->getTerminator()) {
            ctx_.builder().createRet(ctx_.builder().getInt32(0));
        }

        ctx_.setCurrentFunction(nullptr);
        ctx_.popScope();
    }
};

// Factory function
std::unique_ptr<DeclCodegen> createEntrypointCodegen(CodegenContext& ctx,
                                                      ExprCodegen& exprCodegen,
                                                      StmtCodegen& stmtCodegen) {
    return std::make_unique<EntrypointCodegenImpl>(ctx, exprCodegen, stmtCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
