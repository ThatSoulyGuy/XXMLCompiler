#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Core/TypeRegistry.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized DeclCodegen for native FFI methods
class NativeMethodCodegenImpl : public DeclCodegen {
public:
    using DeclCodegen::DeclCodegen;

    // Called from visitMethod when decl->isNative is true
    void generateNativeMethod(Parser::MethodDecl* decl) {
        if (!decl || !decl->isNative) return;

        std::string className = std::string(ctx_.currentClassName());
        std::string funcName;

        if (!className.empty()) {
            funcName = ctx_.mangleFunctionName(className, decl->name);
        } else {
            funcName = decl->name;
        }

        // Build NativeMethodInfo for call sites
        NativeMethodInfo nativeInfo;
        bool isInstanceMethod = !className.empty();

        // Build parameter types
        for (const auto& param : decl->parameters) {
            std::string paramType = mapNativeType(param->type->typeName);
            nativeInfo.paramTypes.push_back(paramType);
            nativeInfo.xxmlParamTypes.push_back(param->type->typeName);
            nativeInfo.isStringPtr.push_back(paramType == "ptr");
            nativeInfo.isCallback.push_back(checkIsCallback(param->type->typeName));
        }

        std::string returnTypeName = decl->returnType ? decl->returnType->typeName : "void";
        nativeInfo.returnType = mapNativeType(returnTypeName);
        nativeInfo.xxmlReturnType = returnTypeName;

        ctx_.registerNativeMethod(funcName, nativeInfo);

        // Check for duplicate
        if (ctx_.isFunctionDefined(funcName)) {
            return;
        }
        ctx_.markFunctionDefined(funcName);

        // Generate the FFI thunk function
        generateFFIThunk(decl, funcName, isInstanceMethod, nativeInfo);
    }

private:
    std::string mapNativeType(const std::string& typeName) {
        // Handle NativeType<...> using TypeNormalizer
        if (TypeNormalizer::isNativeType(typeName)) {
            std::string nativeType = TypeNormalizer::extractNativeTypeName(typeName);
            return mapNativeTypeToLLVM(nativeType);
        }

        // Handle primitive XXML types using TypeRegistry
        if (Core::TypeRegistry::isPrimitiveXXML(typeName)) {
            return Core::TypeRegistry::getPrimitiveLLVMType(typeName);
        }

        // Handle special cases
        if (typeName == "Int") return "i64";
        if (typeName == "void") return "void";

        return "ptr";  // Default to pointer for objects
    }

    std::string mapNativeTypeToLLVM(const std::string& nativeType) {
        // Use TypeRegistry for native type lookup
        static Core::TypeRegistry registry;
        static bool initialized = false;
        if (!initialized) {
            registry.registerBuiltinTypes();
            initialized = true;
        }
        return registry.getNativeTypeLLVM(nativeType);
    }

    bool checkIsCallback(const std::string& typeName) {
        // Strip ownership modifiers using TypeNormalizer
        std::string baseType = TypeNormalizer::stripOwnershipMarker(typeName);
        // Check if registered as callback
        return ctx_.getCallbackThunk(baseType) != nullptr;
    }

    LLVMIR::CallingConv mapCallingConv(Parser::CallingConvention cc) {
        switch (cc) {
            case Parser::CallingConvention::StdCall:
                return LLVMIR::CallingConv::StdCall;
            case Parser::CallingConvention::FastCall:
                return LLVMIR::CallingConv::FastCall;
            default:
                return LLVMIR::CallingConv::CDecl;
        }
    }

    void generateFFIThunk(Parser::MethodDecl* decl, const std::string& funcName,
                          bool isInstanceMethod, const NativeMethodInfo& nativeInfo) {
        ctx_.pushScope();

        // Build LLVM parameter types
        std::vector<LLVMIR::Type*> paramTypes;
        if (isInstanceMethod) {
            paramTypes.push_back(ctx_.builder().getPtrTy());  // this pointer
        }
        for (const auto& paramType : nativeInfo.paramTypes) {
            paramTypes.push_back(mapLLVMType(paramType));
        }

        // Determine return type
        auto* returnType = mapLLVMType(nativeInfo.returnType);

        // Create the thunk function
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

        // Load library: ptr @xxml_FFI_loadLibrary(ptr @.dllPath)
        auto* loadLibFunc = getOrDeclareFn("xxml_FFI_loadLibrary",
            ctx_.builder().getPtrTy(), {ctx_.builder().getPtrTy()});

        // Create global string for DLL path
        auto dllPathGlobal = ctx_.builder().createGlobalString(decl->nativePath, "ffi.path");
        auto dllHandle = ctx_.builder().createCallPtr(loadLibFunc,
            {LLVMIR::AnyValue(dllPathGlobal)}, "dll_handle");

        // Check if null
        auto isNull = ctx_.builder().createIsNull(dllHandle, "is_null");

        auto* errorBlock = func->createBasicBlock("ffi.error");
        auto* continueBlock = func->createBasicBlock("ffi.continue");

        ctx_.builder().createCondBr(isNull, errorBlock, continueBlock);

        // Error block - return default value
        ctx_.builder().setInsertPoint(errorBlock);
        emitDefaultReturn(nativeInfo.returnType);

        // Continue block - get symbol
        ctx_.builder().setInsertPoint(continueBlock);

        auto* getSymFunc = getOrDeclareFn("xxml_FFI_getSymbol",
            ctx_.builder().getPtrTy(),
            {ctx_.builder().getPtrTy(), ctx_.builder().getPtrTy()});

        // Create global string for symbol name
        auto symGlobal = ctx_.builder().createGlobalString(decl->nativeSymbol, "ffi.sym");
        auto symPtr = ctx_.builder().createCallPtr(getSymFunc,
            {LLVMIR::AnyValue(dllHandle), LLVMIR::AnyValue(symGlobal)}, "sym_ptr");

        // Check if symbol is null
        auto isSymNull = ctx_.builder().createIsNull(symPtr, "is_sym_null");

        auto* symErrorBlock = func->createBasicBlock("ffi.sym_error");
        auto* callBlock = func->createBasicBlock("ffi.call");

        ctx_.builder().createCondBr(isSymNull, symErrorBlock, callBlock);

        // Symbol error block - free library and return
        ctx_.builder().setInsertPoint(symErrorBlock);
        auto* freeLibFunc = getOrDeclareFn("xxml_FFI_freeLibrary",
            ctx_.module().getContext().getVoidTy(), {ctx_.builder().getPtrTy()});
        ctx_.builder().createCallVoid(freeLibFunc, {LLVMIR::AnyValue(dllHandle)});
        emitDefaultReturn(nativeInfo.returnType);

        // Call block - make the indirect call
        ctx_.builder().setInsertPoint(callBlock);

        // Build arguments for indirect call (skip 'this' for instance methods)
        std::vector<LLVMIR::AnyValue> callArgs;
        unsigned argStart = isInstanceMethod ? 1 : 0;
        for (unsigned i = argStart; i < func->getNumParams(); ++i) {
            callArgs.push_back(LLVMIR::AnyValue(LLVMIR::PtrValue(func->getArg(i))));
        }

        // Build native function type for indirect call
        std::vector<LLVMIR::Type*> nativeParamTypes;
        for (const auto& pt : nativeInfo.paramTypes) {
            nativeParamTypes.push_back(mapLLVMType(pt));
        }
        auto* nativeFuncType = ctx_.module().getContext().getFunctionTy(
            returnType, nativeParamTypes, false);

        auto cc = mapCallingConv(decl->callingConvention);

        // Make the indirect call
        if (nativeInfo.returnType == "void") {
            ctx_.builder().createIndirectCallVoid(symPtr, nativeFuncType, callArgs, cc);
            ctx_.builder().createRetVoid();
        } else {
            auto result = ctx_.builder().createIndirectCall(symPtr, nativeFuncType, callArgs, "result", cc);
            if (returnType->isPointer()) {
                ctx_.builder().createRet(LLVMIR::PtrValue(result.raw()));
            } else if (returnType->isInteger()) {
                ctx_.builder().createRet(LLVMIR::IntValue(result.raw()));
            } else if (returnType->isFloat()) {
                ctx_.builder().createRet(LLVMIR::FloatValue(result.raw()));
            } else {
                ctx_.builder().createRet(ctx_.builder().getNullPtr());
            }
        }

        ctx_.setCurrentFunction(nullptr);
        ctx_.popScope();
    }

    LLVMIR::Type* mapLLVMType(const std::string& typeStr) {
        if (typeStr == "void") return ctx_.module().getContext().getVoidTy();
        if (typeStr == "i1") return ctx_.module().getContext().getInt1Ty();
        if (typeStr == "i8") return ctx_.module().getContext().getInt8Ty();
        if (typeStr == "i16") return ctx_.module().getContext().getInt16Ty();
        if (typeStr == "i32") return ctx_.module().getContext().getInt32Ty();
        if (typeStr == "i64") return ctx_.module().getContext().getInt64Ty();
        if (typeStr == "float") return ctx_.module().getContext().getFloatTy();
        if (typeStr == "double") return ctx_.module().getContext().getDoubleTy();
        return ctx_.builder().getPtrTy();
    }

    LLVMIR::Function* getOrDeclareFn(const std::string& name, LLVMIR::Type* retType,
                                      std::vector<LLVMIR::Type*> paramTypes) {
        auto* fn = ctx_.module().getFunction(name);
        if (!fn) {
            auto* funcType = ctx_.module().getContext().getFunctionTy(retType, paramTypes, false);
            fn = ctx_.module().createFunction(funcType, name, LLVMIR::Function::Linkage::External);
        }
        return fn;
    }

    void emitDefaultReturn(const std::string& returnType) {
        if (returnType == "void") {
            ctx_.builder().createRetVoid();
        } else if (returnType == "ptr") {
            ctx_.builder().createRet(ctx_.builder().getNullPtr());
        } else if (returnType == "float") {
            ctx_.builder().createRet(ctx_.builder().getFloat(0.0f));
        } else if (returnType == "double") {
            ctx_.builder().createRet(ctx_.builder().getDouble(0.0));
        } else {
            // Integer types
            ctx_.builder().createRet(ctx_.builder().getInt64(0));
        }
    }
};

// Factory function
std::unique_ptr<DeclCodegen> createNativeMethodCodegen(CodegenContext& ctx,
                                                        ExprCodegen& exprCodegen,
                                                        StmtCodegen& stmtCodegen) {
    return std::make_unique<NativeMethodCodegenImpl>(ctx, exprCodegen, stmtCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
