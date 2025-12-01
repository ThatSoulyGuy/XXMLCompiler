#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized StmtCodegen for assignment statements
class AssignmentCodegenImpl : public StmtCodegen {
public:
    using StmtCodegen::StmtCodegen;

    void visitAssignment(Parser::AssignmentStmt* stmt) override {
        if (!stmt || !stmt->value) return;

        // Evaluate the right-hand side expression
        auto rhsValue = exprCodegen_.generate(stmt->value.get());

        // Handle different assignment targets
        if (auto* identTarget = dynamic_cast<Parser::IdentifierExpr*>(stmt->target.get())) {
            assignToVariable(identTarget->name, rhsValue);
        } else if (auto* memberTarget = dynamic_cast<Parser::MemberAccessExpr*>(stmt->target.get())) {
            assignToMember(memberTarget, rhsValue);
        }
    }

private:
    void assignToVariable(const std::string& name, LLVMIR::AnyValue value) {
        // Check if variable already exists
        auto* existingVar = ctx_.getVariable(name);

        if (existingVar && existingVar->alloca) {
            // Store to existing alloca
            ctx_.builder().createStore(value, LLVMIR::PtrValue(existingVar->alloca));
            ctx_.setVariableValue(name, value);
        } else {
            // Create new variable with alloca - infer type from value
            std::string typeName = "ptr";  // Default to ptr for objects
            auto* allocaType = ctx_.mapType(typeName);

            auto allocaPtr = ctx_.builder().createAlloca(allocaType, name);
            ctx_.builder().createStore(value, allocaPtr);

            // Register the variable
            ctx_.declareVariable(name, typeName, value,
                                static_cast<LLVMIR::AllocaInst*>(allocaPtr.raw()));
        }
    }

    void assignToMember(Parser::MemberAccessExpr* memberExpr, LLVMIR::AnyValue value) {
        if (!memberExpr || !memberExpr->object) return;

        // Get the object pointer
        auto objectValue = exprCodegen_.generate(memberExpr->object.get());

        if (!objectValue.isPtr()) return;

        // If object is 'this', use current class info
        if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(memberExpr->object.get())) {
            assignToThisProperty(memberExpr->member, value);
        } else if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(memberExpr->object.get())) {
            // Get variable info to determine type
            if (auto* varInfo = ctx_.getVariable(identExpr->name)) {
                assignToObjectProperty(objectValue.asPtr(), varInfo->xxmlType,
                                       memberExpr->member, value);
            }
        }
    }

    void assignToThisProperty(const std::string& propName, LLVMIR::AnyValue value) {
        auto* func = ctx_.currentFunction();
        if (!func || func->getNumParams() == 0) return;

        auto* thisArg = func->getArg(0);
        if (!thisArg) return;

        // Get class info
        auto* classInfo = ctx_.getClass(std::string(ctx_.currentClassName()));
        if (!classInfo || !classInfo->structType) return;

        // Find property index
        for (const auto& prop : classInfo->properties) {
            if (prop.name == propName) {
                auto thisPtrValue = LLVMIR::PtrValue(thisArg);
                auto propPtr = ctx_.builder().createStructGEP(
                    classInfo->structType,
                    thisPtrValue,
                    static_cast<unsigned>(prop.index),
                    propName + ".ptr"
                );
                ctx_.builder().createStore(value, propPtr);
                return;
            }
        }
    }

    void assignToObjectProperty(LLVMIR::PtrValue objPtr, const std::string& objType,
                                const std::string& propName, LLVMIR::AnyValue value) {
        // Strip ownership modifiers from type
        std::string cleanType = objType;
        if (!cleanType.empty() && (cleanType.back() == '^' ||
            cleanType.back() == '%' || cleanType.back() == '&')) {
            cleanType = cleanType.substr(0, cleanType.length() - 1);
        }

        // Get class info for the object type
        auto* classInfo = ctx_.getClass(cleanType);
        if (!classInfo || !classInfo->structType) return;

        // Find property index
        for (const auto& prop : classInfo->properties) {
            if (prop.name == propName) {
                auto propPtr = ctx_.builder().createStructGEP(
                    classInfo->structType,
                    objPtr,
                    static_cast<unsigned>(prop.index),
                    propName + ".ptr"
                );
                ctx_.builder().createStore(value, propPtr);
                return;
            }
        }
    }
};

// Factory function
std::unique_ptr<StmtCodegen> createAssignmentCodegen(CodegenContext& ctx, ExprCodegen& exprCodegen) {
    return std::make_unique<AssignmentCodegenImpl>(ctx, exprCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
