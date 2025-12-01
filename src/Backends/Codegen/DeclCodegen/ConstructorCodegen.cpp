#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include <unordered_set>

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized DeclCodegen for constructors and destructors
class ConstructorCodegenImpl : public DeclCodegen {
public:
    using DeclCodegen::DeclCodegen;

    void visitConstructor(Parser::ConstructorDecl* decl) override {
        if (!decl) return;

        std::string className = std::string(ctx_.currentClassName());
        if (className.empty()) {
            return;  // Constructor outside of class
        }

        ctx_.pushScope();

        // Generate function name: ClassName_Constructor
        std::string funcName = ctx_.mangleFunctionName(className, "Constructor");

        // Mangle for overloading (add param count suffix)
        static const std::unordered_set<std::string> builtinTypes = {
            "Integer", "String", "Bool", "Float", "Double", "None"
        };

        std::string baseClassName = className;
        size_t colonPos = baseClassName.rfind("::");
        if (colonPos != std::string::npos) {
            baseClassName = baseClassName.substr(colonPos + 2);
        }

        if (builtinTypes.find(baseClassName) == builtinTypes.end()) {
            funcName += "_" + std::to_string(decl->parameters.size());
        }

        // Check for duplicate
        if (ctx_.isFunctionDefined(funcName)) {
            ctx_.popScope();
            return;
        }
        ctx_.markFunctionDefined(funcName);

        // Build parameter types (this + user params)
        std::vector<LLVMIR::Type*> paramTypes;
        paramTypes.push_back(ctx_.builder().getPtrTy());  // this pointer
        for (const auto& param : decl->parameters) {
            paramTypes.push_back(ctx_.mapType(param->type->typeName));
        }

        // Create function (constructors return ptr)
        auto* funcType = ctx_.module().getContext().getFunctionTy(
            ctx_.builder().getPtrTy(), paramTypes, false);
        auto* func = ctx_.module().createFunction(funcType, funcName);
        if (!func) {
            ctx_.popScope();
            return;
        }

        ctx_.setCurrentFunction(func);
        ctx_.setCurrentReturnType("ptr");

        // Create entry block
        auto* entryBlock = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBlock);

        // Map 'this' parameter
        auto* thisArg = func->getArg(0);
        ctx_.declareVariable("this", "ptr", LLVMIR::AnyValue(LLVMIR::PtrValue(thisArg)));

        // Map other parameters
        for (size_t i = 0; i < decl->parameters.size(); ++i) {
            auto* arg = func->getArg(static_cast<unsigned>(i + 1));
            ctx_.declareVariable(decl->parameters[i]->name,
                               decl->parameters[i]->type->typeName,
                               LLVMIR::AnyValue(LLVMIR::PtrValue(arg)));
        }

        // For empty body constructors, initialize all fields to defaults
        if (decl->body.empty()) {
            initializeDefaultFields(thisArg, className);
        }

        // Generate body
        generateFunctionBody(decl->body);

        // Return 'this'
        if (!ctx_.builder().getInsertBlock()->getTerminator()) {
            ctx_.builder().createRet(LLVMIR::PtrValue(thisArg));
        }

        ctx_.setCurrentFunction(nullptr);
        ctx_.popScope();
    }

    void visitDestructor(Parser::DestructorDecl* decl) override {
        if (!decl) return;

        std::string className = std::string(ctx_.currentClassName());
        if (className.empty()) {
            return;  // Destructor outside of class
        }

        ctx_.pushScope();

        // Generate function name: ClassName_Destructor
        std::string funcName = ctx_.mangleFunctionName(className, "Destructor");

        // Check for duplicate
        if (ctx_.isFunctionDefined(funcName)) {
            ctx_.popScope();
            return;
        }
        ctx_.markFunctionDefined(funcName);

        // Destructors take just 'this'
        std::vector<LLVMIR::Type*> paramTypes;
        paramTypes.push_back(ctx_.builder().getPtrTy());

        // Create function (destructors return void)
        auto* funcType = ctx_.module().getContext().getFunctionTy(
            ctx_.module().getContext().getVoidTy(), paramTypes, false);
        auto* func = ctx_.module().createFunction(funcType, funcName);
        if (!func) {
            ctx_.popScope();
            return;
        }

        ctx_.setCurrentFunction(func);
        ctx_.setCurrentReturnType("void");

        // Create entry block
        auto* entryBlock = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBlock);

        // Map 'this'
        auto* thisArg = func->getArg(0);
        ctx_.declareVariable("this", "ptr", LLVMIR::AnyValue(LLVMIR::PtrValue(thisArg)));

        // Generate body
        generateFunctionBody(decl->body);

        // Return void
        if (!ctx_.builder().getInsertBlock()->getTerminator()) {
            ctx_.builder().createRetVoid();
        }

        ctx_.setCurrentFunction(nullptr);
        ctx_.popScope();
    }

private:
    void initializeDefaultFields(LLVMIR::Argument* thisArg, const std::string& className) {
        auto* classInfo = ctx_.getClass(className);
        if (!classInfo || !classInfo->structType) {
            return;
        }

        auto thisPtrVal = LLVMIR::PtrValue(thisArg);
        unsigned fieldIndex = 0;

        for (const auto& prop : classInfo->properties) {
            // GEP to field
            auto fieldPtr = ctx_.builder().createStructGEP(
                classInfo->structType,
                thisPtrVal,
                fieldIndex,
                prop.name + "_init_ptr"
            );

            // Get default value
            auto* fieldType = ctx_.mapType(prop.xxmlType);
            if (fieldType->isPointer()) {
                ctx_.builder().createStore(ctx_.builder().getNullPtr(), fieldPtr);
            } else {
                ctx_.builder().createStore(ctx_.builder().getInt64(0), fieldPtr);
            }

            fieldIndex++;
        }
    }
};

// Factory function
std::unique_ptr<DeclCodegen> createConstructorCodegen(CodegenContext& ctx,
                                                       ExprCodegen& exprCodegen,
                                                       StmtCodegen& stmtCodegen) {
    return std::make_unique<ConstructorCodegenImpl>(ctx, exprCodegen, stmtCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
