#include "Backends/Codegen/FFICodegen/FFICodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

FFICodegen::FFICodegen(CodegenContext& ctx)
    : ctx_(ctx) {}

void FFICodegen::registerNativeMethod(const std::string& mangledName, const NativeMethodInfo& info) {
    nativeMethods_[mangledName] = info;
}

const NativeMethodInfo* FFICodegen::getNativeMethodInfo(const std::string& mangledName) const {
    auto it = nativeMethods_.find(mangledName);
    return it != nativeMethods_.end() ? &it->second : nullptr;
}

bool FFICodegen::isNativeMethod(const std::string& mangledName) const {
    return nativeMethods_.count(mangledName) > 0;
}

void FFICodegen::registerCallbackThunk(const std::string& typeName, const CallbackThunkInfo& info) {
    callbackThunks_[typeName] = info;
}

LLVMIR::Function* FFICodegen::getCallbackThunk(const std::string& typeName) const {
    auto it = callbackThunks_.find(typeName);
    if (it != callbackThunks_.end()) {
        return ctx_.module().getFunction(it->second.thunkFunctionName);
    }
    return nullptr;
}

std::string FFICodegen::mapXXMLTypeToLLVM(const std::string& xxmlType) const {
    if (xxmlType.find("NativeType<") == 0) {
        size_t start = xxmlType.find("\"");
        size_t end = xxmlType.rfind("\"");
        std::string nativeType;
        if (start != std::string::npos && end != std::string::npos && end > start) {
            nativeType = xxmlType.substr(start + 1, end - start - 1);
        } else {
            size_t angleStart = xxmlType.find('<');
            size_t angleEnd = xxmlType.find('>');
            if (angleStart != std::string::npos && angleEnd != std::string::npos) {
                nativeType = xxmlType.substr(angleStart + 1, angleEnd - angleStart - 1);
            }
        }
        return mapNativeTypeToLLVM(nativeType);
    }

    if (xxmlType == "Integer" || xxmlType == "Int") return "i64";
    if (xxmlType == "Float") return "float";
    if (xxmlType == "Double") return "double";
    if (xxmlType == "Bool") return "i1";
    if (xxmlType == "None" || xxmlType == "void") return "void";
    if (xxmlType == "String") return "ptr";
    return "ptr";
}

std::string FFICodegen::mapNativeTypeToLLVM(const std::string& nativeType) const {
    if (nativeType == "int8" || nativeType == "i8") return "i8";
    if (nativeType == "int16" || nativeType == "i16") return "i16";
    if (nativeType == "int32" || nativeType == "i32" || nativeType == "int") return "i32";
    if (nativeType == "int64" || nativeType == "i64") return "i64";
    if (nativeType == "uint8" || nativeType == "u8") return "i8";
    if (nativeType == "uint16" || nativeType == "u16") return "i16";
    if (nativeType == "uint32" || nativeType == "u32") return "i32";
    if (nativeType == "uint64" || nativeType == "u64") return "i64";
    if (nativeType == "float" || nativeType == "f32") return "float";
    if (nativeType == "double" || nativeType == "f64") return "double";
    if (nativeType == "void") return "void";
    if (nativeType == "ptr" || nativeType == "pointer") return "ptr";
    return "ptr";
}

LLVMIR::Type* FFICodegen::getLLVMType(const std::string& typeStr) const {
    auto& llvmCtx = ctx_.module().getContext();

    if (typeStr == "void") return llvmCtx.getVoidTy();
    if (typeStr == "i1") return llvmCtx.getInt1Ty();
    if (typeStr == "i8") return llvmCtx.getInt8Ty();
    if (typeStr == "i16") return llvmCtx.getInt16Ty();
    if (typeStr == "i32") return llvmCtx.getInt32Ty();
    if (typeStr == "i64") return llvmCtx.getInt64Ty();
    if (typeStr == "float") return llvmCtx.getFloatTy();
    if (typeStr == "double") return llvmCtx.getDoubleTy();
    return llvmCtx.getPtrTy();
}

LLVMIR::CallingConv FFICodegen::mapCallingConv(Parser::CallingConvention cc) const {
    switch (cc) {
        case Parser::CallingConvention::StdCall:
            return LLVMIR::CallingConv::StdCall;
        case Parser::CallingConvention::FastCall:
            return LLVMIR::CallingConv::FastCall;
        default:
            return LLVMIR::CallingConv::CDecl;
    }
}

LLVMIR::Function* FFICodegen::getOrDeclareFFIFunc(const std::string& name,
                                                    LLVMIR::Type* retTy,
                                                    std::vector<LLVMIR::Type*> paramTys) {
    auto* fn = ctx_.module().getFunction(name);
    if (!fn) {
        auto* funcType = ctx_.module().getContext().getFunctionTy(retTy, paramTys, false);
        fn = ctx_.module().createFunction(funcType, name, LLVMIR::Function::Linkage::External);
    }
    return fn;
}

LLVMIR::PtrValue FFICodegen::marshalStringToC(LLVMIR::AnyValue xxmlString) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_stringToCString",
                                      ctx_.builder().getPtrTy(),
                                      {ctx_.builder().getPtrTy()});
    return ctx_.builder().createCallPtr(func, {xxmlString}, "cstr");
}

LLVMIR::PtrValue FFICodegen::marshalCToString(LLVMIR::PtrValue cString) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_cstringToString",
                                      ctx_.builder().getPtrTy(),
                                      {ctx_.builder().getPtrTy()});
    return ctx_.builder().createCallPtr(func, {LLVMIR::AnyValue(cString)}, "xxml_str");
}

LLVMIR::IntValue FFICodegen::marshalIntegerToC(LLVMIR::AnyValue xxmlInteger) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_integerToInt64",
                                      ctx_.module().getContext().getInt64Ty(),
                                      {ctx_.builder().getPtrTy()});
    return ctx_.builder().createCallInt(func, {xxmlInteger}, "c_int");
}

LLVMIR::PtrValue FFICodegen::marshalCToInteger(LLVMIR::IntValue cInt) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_int64ToInteger",
                                      ctx_.builder().getPtrTy(),
                                      {ctx_.module().getContext().getInt64Ty()});
    return ctx_.builder().createCallPtr(func, {LLVMIR::AnyValue(cInt)}, "xxml_int");
}

LLVMIR::FloatValue FFICodegen::marshalFloatToC(LLVMIR::AnyValue xxmlFloat) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_floatToC",
                                      ctx_.module().getContext().getFloatTy(),
                                      {ctx_.builder().getPtrTy()});
    return ctx_.builder().createCallFloat(func, {xxmlFloat}, "c_float");
}

LLVMIR::PtrValue FFICodegen::marshalCToFloat(LLVMIR::FloatValue cFloat) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_cToFloat",
                                      ctx_.builder().getPtrTy(),
                                      {ctx_.module().getContext().getFloatTy()});
    return ctx_.builder().createCallPtr(func, {LLVMIR::AnyValue(cFloat)}, "xxml_float");
}

LLVMIR::BoolValue FFICodegen::marshalBoolToC(LLVMIR::AnyValue xxmlBool) {
    // Call FFI function, then extract bool from result
    auto* func = getOrDeclareFFIFunc("xxml_FFI_boolToC",
                                      ctx_.module().getContext().getInt1Ty(),
                                      {ctx_.builder().getPtrTy()});
    // Use createCall and wrap result
    auto result = ctx_.builder().createCall(func, {xxmlBool}, "c_bool");
    return LLVMIR::BoolValue(result.raw());
}

LLVMIR::PtrValue FFICodegen::marshalCToBool(LLVMIR::BoolValue cBool) {
    auto* func = getOrDeclareFFIFunc("xxml_FFI_cToBool",
                                      ctx_.builder().getPtrTy(),
                                      {ctx_.module().getContext().getInt1Ty()});
    return ctx_.builder().createCallPtr(func, {LLVMIR::AnyValue(cBool)}, "xxml_bool");
}

void FFICodegen::emitDefaultReturn(const std::string& returnType) {
    if (returnType == "void") {
        ctx_.builder().createRetVoid();
    } else if (returnType == "ptr") {
        ctx_.builder().createRet(ctx_.builder().getNullPtr());
    } else if (returnType == "float") {
        ctx_.builder().createRet(ctx_.builder().getFloat(0.0f));
    } else if (returnType == "double") {
        ctx_.builder().createRet(ctx_.builder().getDouble(0.0));
    } else {
        ctx_.builder().createRet(ctx_.builder().getInt64(0));
    }
}

LLVMIR::Function* FFICodegen::generateThunk(Parser::MethodDecl* decl,
                                             const std::string& funcName,
                                             bool isInstanceMethod) {
    if (!decl || !decl->isNative) return nullptr;

    ctx_.pushScope();

    // Build native method info
    NativeMethodInfo nativeInfo;
    // Note: dllPath and nativeSymbol are accessed directly from the decl when needed

    // Build parameter types
    std::vector<LLVMIR::Type*> paramTypes;
    if (isInstanceMethod) {
        paramTypes.push_back(ctx_.builder().getPtrTy());
    }

    for (const auto& param : decl->parameters) {
        std::string llvmType = mapXXMLTypeToLLVM(param->type->typeName);
        nativeInfo.paramTypes.push_back(llvmType);
        nativeInfo.xxmlParamTypes.push_back(param->type->typeName);
        nativeInfo.isStringPtr.push_back(llvmType == "ptr" && param->type->typeName == "String");
        nativeInfo.isCallback.push_back(getCallbackThunk(param->type->typeName) != nullptr);
        paramTypes.push_back(getLLVMType(llvmType));
    }

    // Determine return type
    std::string returnTypeName = decl->returnType ? decl->returnType->typeName : "void";
    nativeInfo.returnType = mapXXMLTypeToLLVM(returnTypeName);
    nativeInfo.xxmlReturnType = returnTypeName;
    LLVMIR::Type* returnType = getLLVMType(nativeInfo.returnType);

    // Register the native method
    registerNativeMethod(funcName, nativeInfo);

    // Create the thunk function
    auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes, false);
    auto* func = ctx_.module().createFunction(funcType, funcName);
    if (!func) {
        ctx_.popScope();
        return nullptr;
    }

    ctx_.setCurrentFunction(func);

    // Create entry block
    auto* entryBlock = func->createBasicBlock("entry");
    ctx_.builder().setInsertPoint(entryBlock);

    // Load library
    auto* loadLibFunc = getOrDeclareFFIFunc("xxml_FFI_loadLibrary",
                                             ctx_.builder().getPtrTy(),
                                             {ctx_.builder().getPtrTy()});

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

    auto* getSymFunc = getOrDeclareFFIFunc("xxml_FFI_getSymbol",
                                            ctx_.builder().getPtrTy(),
                                            {ctx_.builder().getPtrTy(), ctx_.builder().getPtrTy()});

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
    auto* freeLibFunc = getOrDeclareFFIFunc("xxml_FFI_freeLibrary",
                                             ctx_.module().getContext().getVoidTy(),
                                             {ctx_.builder().getPtrTy()});
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
        nativeParamTypes.push_back(getLLVMType(pt));
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

    return func;
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
