#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for member access expressions
class MemberAccessCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitMemberAccess(Parser::MemberAccessExpr* expr) override {
        if (!expr || !expr->object) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Handle 'this.property'
        if (dynamic_cast<Parser::ThisExpr*>(expr->object.get())) {
            return loadThisProperty(expr->member);
        }

        // Handle 'variable.property'
        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->object.get())) {
            return loadObjectProperty(ident->name, expr->member);
        }

        // Evaluate object and access property
        auto objectValue = generate(expr->object.get());
        if (objectValue.isPtr()) {
            // Would need type info to properly handle this case
            // For now, return the object value as-is
            ctx_.lastExprValue = objectValue;
            return ctx_.lastExprValue;
        }

        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

private:
    LLVMIR::AnyValue loadThisProperty(const std::string& propName) {
        auto* func = ctx_.currentFunction();
        if (!func || func->getNumParams() == 0) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        auto* thisArg = func->getArg(0);
        if (!thisArg) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        auto* classInfo = ctx_.getClass(std::string(ctx_.currentClassName()));
        if (!classInfo || !classInfo->structType) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Find property
        for (const auto& prop : classInfo->properties) {
            if (prop.name == propName) {
                auto thisPtrValue = LLVMIR::PtrValue(thisArg);
                auto propPtr = ctx_.builder().createStructGEP(
                    classInfo->structType,
                    thisPtrValue,
                    static_cast<unsigned>(prop.index),
                    propName + ".ptr"
                );

                auto* propType = ctx_.mapType(prop.xxmlType);
                auto loaded = ctx_.builder().createLoad(propType, propPtr, propName + ".val");
                ctx_.lastExprValue = loaded;
                return ctx_.lastExprValue;
            }
        }

        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

    LLVMIR::AnyValue loadObjectProperty(const std::string& varName, const std::string& propName) {
        auto* varInfo = ctx_.getVariable(varName);
        if (!varInfo) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Strip ownership modifiers
        std::string cleanType = varInfo->xxmlType;
        if (!cleanType.empty() && (cleanType.back() == '^' ||
            cleanType.back() == '%' || cleanType.back() == '&')) {
            cleanType = cleanType.substr(0, cleanType.length() - 1);
        }

        auto* classInfo = ctx_.getClass(cleanType);
        if (!classInfo || !classInfo->structType) {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Get object pointer
        LLVMIR::PtrValue objPtr;
        if (varInfo->alloca) {
            auto loaded = ctx_.builder().createLoadPtr(LLVMIR::PtrValue(varInfo->alloca), varName + ".load");
            objPtr = loaded;
        } else if (varInfo->value.isPtr()) {
            objPtr = varInfo->value.asPtr();
        } else {
            return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
        }

        // Find and load property
        for (const auto& prop : classInfo->properties) {
            if (prop.name == propName) {
                auto propPtr = ctx_.builder().createStructGEP(
                    classInfo->structType,
                    objPtr,
                    static_cast<unsigned>(prop.index),
                    propName + ".ptr"
                );

                auto* propType = ctx_.mapType(prop.xxmlType);
                auto loaded = ctx_.builder().createLoad(propType, propPtr, propName + ".val");
                ctx_.lastExprValue = loaded;
                return ctx_.lastExprValue;
            }
        }

        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }
};

// Factory function
std::unique_ptr<ExprCodegen> createMemberAccessCodegen(CodegenContext& ctx) {
    return std::make_unique<MemberAccessCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
