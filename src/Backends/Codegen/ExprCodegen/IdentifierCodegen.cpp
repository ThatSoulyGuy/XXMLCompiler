#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Semantic/SemanticError.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for identifier/variable expressions
class IdentifierCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitIdentifier(Parser::IdentifierExpr* expr) override {
        if (!expr) {
            throw Semantic::CodegenInvariantViolation("NULL_IDENTIFIER",
                "IdentifierExpr is null");
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

        // Unknown identifier - this is a semantic error that should have been caught
        throw Semantic::UnresolvedIdentifierError(name);
    }

    LLVMIR::AnyValue visitThis(Parser::ThisExpr*) override {
        // Get 'this' pointer from current function's first argument
        auto* func = ctx_.currentFunction();
        if (!func) {
            throw Semantic::CodegenInvariantViolation("THIS_NO_FUNCTION",
                "'this' used outside of a function");
        }
        if (func->getNumParams() == 0) {
            throw Semantic::CodegenInvariantViolation("THIS_NO_PARAMS",
                "'this' used in function with no parameters (not a method?)");
        }

        auto* thisArg = func->getArg(0);
        if (!thisArg) {
            throw Semantic::CodegenInvariantViolation("THIS_NULL_ARG",
                "'this' argument is null");
        }

        auto ptrValue = LLVMIR::PtrValue(thisArg);
        ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
        return ctx_.lastExprValue;
    }

    LLVMIR::AnyValue visitReference(Parser::ReferenceExpr* expr) override {
        if (!expr || !expr->expr) {
            throw Semantic::CodegenInvariantViolation("NULL_REFERENCE",
                "ReferenceExpr is null or has null inner expression");
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
            throw Semantic::CodegenInvariantViolation("PROPERTY_NO_FUNCTION",
                "Cannot load property '" + prop.name + "' - no current function or method");
        }

        auto* thisArg = func->getArg(0);
        if (!thisArg) {
            throw Semantic::CodegenInvariantViolation("PROPERTY_NO_THIS",
                "Cannot load property '" + prop.name + "' - 'this' argument is null");
        }

        // Get class struct type
        std::string className = std::string(ctx_.currentClassName());
        auto* classInfo = ctx_.getClass(className);
        if (!classInfo) {
            throw Semantic::MissingClassError(className, "loading property '" + prop.name + "'");
        }
        if (!classInfo->structType) {
            throw Semantic::CodegenInvariantViolation("PROPERTY_NO_STRUCT",
                "Class '" + className + "' has no struct type for property '" + prop.name + "'");
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
