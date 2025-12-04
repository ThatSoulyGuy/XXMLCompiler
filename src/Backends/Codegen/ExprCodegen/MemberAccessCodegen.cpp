#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Semantic/SemanticError.h"
#include <iostream>

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized ExprCodegen for member access expressions
class MemberAccessCodegenImpl : public ExprCodegen {
public:
    using ExprCodegen::ExprCodegen;

    LLVMIR::AnyValue visitMemberAccess(Parser::MemberAccessExpr* expr) override {
        if (!expr || !expr->object) {
            throw Semantic::CodegenInvariantViolation("NULL_AST",
                "MemberAccessExpr is null or has null object");
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

        throw Semantic::CodegenInvariantViolation("MEMBER_ACCESS",
            "Cannot access member on non-pointer value");
    }

private:
    LLVMIR::AnyValue loadThisProperty(const std::string& propName) {
        auto* func = ctx_.currentFunction();
        if (!func || func->getNumParams() == 0) {
            throw Semantic::CodegenInvariantViolation("THIS_ACCESS",
                "Cannot access 'this." + propName +
                "' - no current function or function has no parameters");
        }

        auto* thisArg = func->getArg(0);
        if (!thisArg) {
            throw Semantic::CodegenInvariantViolation("THIS_ACCESS",
                "Cannot access 'this." + propName + "' - 'this' argument is null");
        }

        std::string currentClass = std::string(ctx_.currentClassName());
        auto* classInfo = ctx_.getClass(currentClass);
        if (!classInfo) {
            throw Semantic::MissingClassError(currentClass,
                "accessing 'this." + propName + "'");
        }
        if (!classInfo->structType) {
            throw Semantic::CodegenInvariantViolation("MISSING_STRUCT_TYPE",
                "Class '" + currentClass + "' has no struct type for property access");
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

        throw Semantic::MissingPropertyError(currentClass, propName);
    }

    LLVMIR::AnyValue loadObjectProperty(const std::string& varName, const std::string& propName) {
        auto* varInfo = ctx_.getVariable(varName);
        if (!varInfo) {
            throw Semantic::UnresolvedIdentifierError(varName);
        }

        // Strip ownership modifiers
        std::string cleanType = varInfo->xxmlType;
        if (!cleanType.empty() && (cleanType.back() == '^' ||
            cleanType.back() == '%' || cleanType.back() == '&')) {
            cleanType = cleanType.substr(0, cleanType.length() - 1);
        }

        // Handle template instantiation types (e.g., "Box<Integer>" -> "Box")
        std::string lookupType = cleanType;
        size_t angleBracket = cleanType.find('<');
        if (angleBracket != std::string::npos) {
            lookupType = cleanType.substr(0, angleBracket);
        }

        auto* classInfo = ctx_.getClass(lookupType);
        if (!classInfo) {
            throw Semantic::MissingClassError(cleanType,
                "accessing '" + varName + "." + propName + "'");
        }
        if (!classInfo->structType) {
            throw Semantic::CodegenInvariantViolation("MISSING_STRUCT_TYPE",
                "Class '" + cleanType + "' has no struct type for property access");
        }

        // Get object pointer
        LLVMIR::PtrValue objPtr;
        if (varInfo->alloca) {
            auto loaded = ctx_.builder().createLoadPtr(LLVMIR::PtrValue(varInfo->alloca), varName + ".load");
            objPtr = loaded;
        } else if (varInfo->value.isPtr()) {
            objPtr = varInfo->value.asPtr();
        } else {
            throw Semantic::CodegenInvariantViolation("NON_POINTER_ACCESS",
                "Cannot access '" + varName + "." + propName +
                "' - variable '" + varName + "' is not a pointer");
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

        throw Semantic::MissingPropertyError(cleanType, propName);
    }
};

// Factory function
std::unique_ptr<ExprCodegen> createMemberAccessCodegen(CodegenContext& ctx) {
    return std::make_unique<MemberAccessCodegenImpl>(ctx);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
