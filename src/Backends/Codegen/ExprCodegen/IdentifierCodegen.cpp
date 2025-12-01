#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for identifier/variable expressions
class IdentifierCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitIdentifier(Parser::IdentifierExpr* expr) override {
        if (!expr) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        const std::string& name = expr->name;

        // Check if it's a local variable
        if (auto* varInfo = ctx_.getVariable(name)) {
            // If it has an alloca, load from it
            if (varInfo->alloca) {
                auto* loadType = ctx_.mapType(varInfo->xxmlType);
                auto loaded = ctx_.builder().createLoad(
                    loadType,
                    LLVMIR::PtrValue(varInfo->alloca),
                    name + ".load"
                );
                ctx_.lastExprValue = loaded;
                return ctx_.lastExprValue;
            }
            // Otherwise return the value directly
            ctx_.lastExprValue = varInfo->value;
            return ctx_.lastExprValue;
        }

        // Check if it's an enum value
        if (ctx_.hasEnumValue(name)) {
            int64_t value = ctx_.getEnumValue(name);
            auto intValue = ctx_.builder().getInt64(value);
            ctx_.lastExprValue = LLVMIR::AnyValue(intValue);
            return ctx_.lastExprValue;
        }

        // Check if it's a property of the current class
        if (!ctx_.currentClassName().empty()) {
            auto* classInfo = ctx_.getClass(std::string(ctx_.currentClassName()));
            if (classInfo) {
                for (const auto& prop : classInfo->properties) {
                    if (prop.name == name) {
                        // Load from 'this' pointer
                        return loadPropertyFromThis(prop);
                    }
                }
            }
        }

        // Unknown identifier - return null
        ctx_.lastExprValue = LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitThis(Parser::ThisExpr*) override {
        // Get 'this' pointer from current function's first argument
        auto* func = ctx_.currentFunction();
        if (func && func->getNumParams() > 0) {
            auto* thisArg = func->getArg(0);
            if (thisArg) {
                auto ptrValue = LLVMIR::PtrValue(thisArg);
                ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
                return ctx_.lastExprValue;
            }
        }

        // Fallback to null
        ctx_.lastExprValue = LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitReference(Parser::ReferenceExpr* expr) override {
        if (!expr || !expr->expr) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // If it's an identifier, get its address (alloca)
        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->expr.get())) {
            if (auto* varInfo = ctx_.getVariable(ident->name)) {
                if (varInfo->alloca) {
                    auto ptrValue = LLVMIR::PtrValue(varInfo->alloca);
                    ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
                    return ctx_.lastExprValue;
                }
            }
        }

        // Fallback - evaluate the expression
        return generate(expr->expr.get());
    }

private:
    LLVMIR::AnyValue loadPropertyFromThis(const PropertyInfo& prop) {
        auto* func = ctx_.currentFunction();
        if (!func || func->getNumParams() == 0) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        auto* thisArg = func->getArg(0);
        if (!thisArg) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Get class struct type
        auto* classInfo = ctx_.getClass(std::string(ctx_.currentClassName()));
        if (!classInfo || !classInfo->structType) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // GEP to property
        auto thisPtrValue = LLVMIR::PtrValue(thisArg);
        auto propPtr = ctx_.builder().createStructGEP(
            classInfo->structType,
            thisPtrValue,
            static_cast<unsigned>(prop.index),
            prop.name + ".ptr"
        );

        // Load the property
        auto* propType = ctx_.mapType(prop.xxmlType);
        auto loaded = ctx_.builder().createLoad(propType, propPtr, prop.name + ".val");

        ctx_.lastExprValue = loaded;
        return ctx_.lastExprValue;
    }
};

// Factory function
std::unique_ptr<ExprCodegen> createIdentifierCodegen(CodegenContext& ctx) {
    return std::make_unique<IdentifierCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
