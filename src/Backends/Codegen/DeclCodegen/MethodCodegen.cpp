#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized DeclCodegen for method declarations
class MethodCodegenImpl : public DeclCodegen {
public:
    using DeclCodegen::DeclCodegen;

    void visitMethod(Parser::MethodDecl* decl) override {
        if (!decl) return;

        // Handle native FFI methods separately
        if (decl->isNative) {
            generateNativeMethodThunk(decl);
            return;
        }

        // Clear local scope
        ctx_.pushScope();

        // Determine function name
        std::string funcName;
        std::string className = std::string(ctx_.currentClassName());
        bool isInstanceMethod = !className.empty();

        if (isInstanceMethod) {
            funcName = ctx_.mangleFunctionName(className, decl->name);
        } else {
            funcName = decl->name;
        }

        // Check for duplicate function
        if (ctx_.isFunctionDefined(funcName)) {
            ctx_.popScope();
            return;
        }
        ctx_.markFunctionDefined(funcName);

        // Build parameter types
        std::vector<LLVMIR::Type*> paramTypes;
        if (isInstanceMethod) {
            paramTypes.push_back(ctx_.builder().getPtrTy());  // this pointer
        }
        for (const auto& param : decl->parameters) {
            paramTypes.push_back(ctx_.mapType(param->type->typeName));
        }

        // Determine return type
        std::string returnTypeName = decl->returnType ? decl->returnType->typeName : "void";
        auto* returnType = ctx_.mapType(returnTypeName);
        ctx_.setCurrentReturnType(returnTypeName);

        // Create function
        auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes, false);
        auto* func = ctx_.module().createFunction(funcType, funcName);
        if (!func) {
            ctx_.popScope();
            return;
        }

        ctx_.setCurrentFunction(func);

        // Create entry block
        auto* entryBlock = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBlock);

        // Map parameters to local variables
        unsigned argIdx = 0;
        if (isInstanceMethod) {
            auto* thisArg = func->getArg(argIdx++);
            ctx_.declareVariable("this", "ptr", LLVMIR::AnyValue(LLVMIR::PtrValue(thisArg)));
        }

        for (const auto& param : decl->parameters) {
            auto* arg = func->getArg(argIdx++);
            ctx_.declareVariable(param->name, param->type->typeName,
                               LLVMIR::AnyValue(LLVMIR::PtrValue(arg)));
        }

        // Generate body
        generateFunctionBody(decl->body);

        // Ensure proper return
        ensureReturn(returnTypeName);

        // Clean up
        ctx_.setCurrentFunction(nullptr);
        ctx_.popScope();
    }

private:
    void ensureReturn(const std::string& returnTypeName) {
        // Check if current block has terminator
        auto* currentBlock = ctx_.builder().getInsertBlock();
        if (currentBlock && currentBlock->getTerminator()) {
            return;  // Already has return/br
        }

        if (returnTypeName == "void") {
            ctx_.builder().createRetVoid();
        } else if (returnTypeName == "ptr" || returnTypeName.back() == '^' ||
                   returnTypeName.back() == '%' || returnTypeName.back() == '&') {
            ctx_.builder().createRet(ctx_.builder().getNullPtr());
        } else {
            ctx_.builder().createRet(ctx_.builder().getInt64(0));
        }
    }

    void generateNativeMethodThunk(Parser::MethodDecl* decl) {
        // Native methods are handled by FFI - generate a simple declaration
        // The native name is the method name itself for now
        std::string nativeName = decl->name;

        // Build parameter types
        std::vector<LLVMIR::Type*> paramTypes;
        for (const auto& param : decl->parameters) {
            paramTypes.push_back(ctx_.mapType(param->type->typeName));
        }

        std::string returnTypeName = decl->returnType ? decl->returnType->typeName : "void";
        auto* returnType = ctx_.mapType(returnTypeName);

        // Create function declaration (external linkage)
        auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes, false);
        ctx_.module().createFunction(funcType, nativeName, LLVMIR::Function::Linkage::External);
    }
};

// Factory function
std::unique_ptr<DeclCodegen> createMethodCodegen(CodegenContext& ctx,
                                                  ExprCodegen& exprCodegen,
                                                  StmtCodegen& stmtCodegen) {
    return std::make_unique<MethodCodegenImpl>(ctx, exprCodegen, stmtCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
