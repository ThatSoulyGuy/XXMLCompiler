#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for call expressions
class CallCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitCall(Parser::CallExpr* expr) override {
        if (!expr || !expr->callee) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        std::string functionName;
        LLVMIR::PtrValue instancePtr;
        bool isInstanceMethod = false;

        // Determine call type
        if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(expr->callee.get())) {
            // Method call: obj.method() or ClassName::staticMethod()
            return handleMemberCall(expr, memberAccess);
        } else if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->callee.get())) {
            // Simple function call: funcName()
            functionName = ident->name;
        }

        if (functionName.empty()) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Build arguments
        std::vector<LLVMIR::AnyValue> args;
        for (const auto& argExpr : expr->arguments) {
            auto argValue = generate(argExpr.get());
            args.push_back(argValue);
        }

        // Call the function
        return emitCall(functionName, args, isInstanceMethod, instancePtr);
    }

private:
    LLVMIR::AnyValue handleMemberCall(Parser::CallExpr* expr,
                                      Parser::MemberAccessExpr* memberAccess) {
        std::string functionName;
        LLVMIR::PtrValue instancePtr;
        bool isInstanceMethod = false;

        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
            // Check if it's a variable (instance method) or class name (static/constructor)
            if (auto* varInfo = ctx_.getVariable(ident->name)) {
                // Instance method call: obj.method()
                isInstanceMethod = true;

                // Get object pointer
                if (varInfo->alloca) {
                    auto loaded = ctx_.builder().createLoadPtr(
                        LLVMIR::PtrValue(varInfo->alloca),
                        ident->name + ".obj"
                    );
                    instancePtr = loaded;
                } else if (varInfo->value.isPtr()) {
                    instancePtr = varInfo->value.asPtr();
                }

                // Determine class name from variable type
                std::string className = varInfo->xxmlType;
                // Strip ownership modifiers
                if (!className.empty() && (className.back() == '^' ||
                    className.back() == '%' || className.back() == '&')) {
                    className = className.substr(0, className.length() - 1);
                }

                functionName = ctx_.mangleFunctionName(className, memberAccess->member);
            } else {
                // Static method or constructor: ClassName::method()
                std::string className = ident->name;

                if (memberAccess->member == "Constructor") {
                    // Constructor call
                    functionName = ctx_.mangleFunctionName(className, "Constructor");
                    // Add parameter count suffix for overloading
                    functionName += "_" + std::to_string(expr->arguments.size());
                } else {
                    functionName = ctx_.mangleFunctionName(className, memberAccess->member);
                }
            }
        } else if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(memberAccess->object.get())) {
            // this.method()
            (void)thisExpr;  // Suppress unused warning
            isInstanceMethod = true;
            auto* func = ctx_.currentFunction();
            if (func && func->getNumParams() > 0) {
                instancePtr = LLVMIR::PtrValue(func->getArg(0));
            }
            std::string className = std::string(ctx_.currentClassName());
            functionName = ctx_.mangleFunctionName(className, memberAccess->member);
        }

        if (functionName.empty()) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Build arguments
        std::vector<LLVMIR::AnyValue> args;
        if (isInstanceMethod) {
            args.push_back(LLVMIR::AnyValue(instancePtr));
        }
        for (const auto& argExpr : expr->arguments) {
            auto argValue = generate(argExpr.get());
            args.push_back(argValue);
        }

        return emitCall(functionName, args, isInstanceMethod, instancePtr);
    }

    LLVMIR::AnyValue emitCall(const std::string& functionName,
                              const std::vector<LLVMIR::AnyValue>& args,
                              bool /*isInstanceMethod*/,
                              LLVMIR::PtrValue /*instancePtr*/) {
        // Get or declare the function
        auto* func = ctx_.module().getFunction(functionName);
        if (!func) {
            // Create a declaration for the function
            std::vector<LLVMIR::Type*> paramTypes;
            for (size_t i = 0; i < args.size(); ++i) {
                paramTypes.push_back(ctx_.builder().getPtrTy());
            }
            auto* funcType = ctx_.module().getContext().getFunctionTy(
                ctx_.builder().getPtrTy(), paramTypes, false);
            func = ctx_.module().createFunction(funcType, functionName,
                                                LLVMIR::Function::Linkage::External);
        }

        if (!func) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Create call instruction - createCall takes vector<AnyValue> directly
        auto result = ctx_.builder().createCall(func, args, "call_result");
        ctx_.lastExprValue = result;
        return ctx_.lastExprValue;
    }
};

// Factory function
std::unique_ptr<ExprCodegen> createCallCodegen(CodegenContext& ctx) {
    return std::make_unique<CallCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
