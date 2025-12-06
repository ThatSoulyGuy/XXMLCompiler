#include "Backends/LLVMIR/FFIBuilder.h"
#include <stdexcept>

namespace XXML {
namespace Backends {
namespace LLVMIR {

FFIBuilder::FFIBuilder(Module& module, IRBuilder& builder, GlobalBuilder& globalBuilder)
    : module_(module), builder_(builder), globalBuilder_(globalBuilder) {
}

// ============================================================================
// FFI Runtime Declaration
// ============================================================================

void FFIBuilder::declareFFIRuntime() {
    if (isFFIRuntimeDeclared()) {
        return;
    }

    TypeContext& ctx = module_.getContext();
    PointerType* ptrTy = ctx.getPtrTy();
    VoidType* voidTy = ctx.getVoidTy();

    // declare ptr @xxml_FFI_loadLibrary(ptr)
    std::vector<Type*> loadParams = {ptrTy};
    FunctionType* loadTy = ctx.getFunctionTy(ptrTy, loadParams);
    loadLibraryFunc_ = globalBuilder_.declareFunction(loadTy, "xxml_FFI_loadLibrary");

    // declare ptr @xxml_FFI_getSymbol(ptr, ptr)
    std::vector<Type*> getSymParams = {ptrTy, ptrTy};
    FunctionType* getSymTy = ctx.getFunctionTy(ptrTy, getSymParams);
    getSymbolFunc_ = globalBuilder_.declareFunction(getSymTy, "xxml_FFI_getSymbol");

    // declare void @xxml_FFI_freeLibrary(ptr)
    std::vector<Type*> freeParams = {ptrTy};
    FunctionType* freeTy = ctx.getFunctionTy(voidTy, freeParams);
    freeLibraryFunc_ = globalBuilder_.declareFunction(freeTy, "xxml_FFI_freeLibrary");
}

bool FFIBuilder::isFFIRuntimeDeclared() const {
    return loadLibraryFunc_ != nullptr &&
           getSymbolFunc_ != nullptr &&
           freeLibraryFunc_ != nullptr;
}

// ============================================================================
// Thunk Generation
// ============================================================================

FFIThunkResult FFIBuilder::createFFIThunk(const FFIThunkConfig& config) {
    FFIThunkResult result;

    // Ensure FFI runtime is declared
    declareFFIRuntime();

    // Create string constants for library path and symbol name
    result.libraryPathStr = globalBuilder_.createStringConstant(config.libraryPath);
    result.symbolNameStr = globalBuilder_.createStringConstant(config.symbolName);

    // Build function type for the wrapper
    FunctionType* funcType = buildFunctionType(config);

    // Create the function with internal linkage
    result.function = globalBuilder_.defineInternalFunction(funcType, config.functionName);

    // Create basic blocks
    BasicBlock* entryBB = result.function->createBasicBlock("entry");
    BasicBlock* errorBB = result.function->createBasicBlock("ffi.error");
    BasicBlock* continueBB = result.function->createBasicBlock("ffi.continue");
    BasicBlock* symErrorBB = result.function->createBasicBlock("ffi.sym_error");
    BasicBlock* callBB = result.function->createBasicBlock("ffi.call");

    // Get pointer values for string constants
    PtrValue libraryPathPtr = result.libraryPathStr->toTypedValue();
    PtrValue symbolNamePtr = result.symbolNameStr->toTypedValue();

    // Emit entry block: load library and check for null
    emitEntryBlock(result.function, config, libraryPathPtr, symbolNamePtr, errorBB, continueBB);

    // Emit error block: library load failed, return default value
    emitErrorBlock(errorBB, config.returnType);

    // Emit continue block: get symbol and check for null
    // Need to get the dll handle from entry block - we'll use a pattern where
    // the entry block stores it and continue block loads it
    // Actually, for SSA form, we need to pass the handle via phi or just regenerate
    // Let's use a simpler approach: emit all in sequence

    // Actually, let me restructure to emit everything properly in one pass
    // Entry block
    builder_.setInsertPoint(entryBB);
    PtrValue dllHandle = builder_.createCallPtr(loadLibraryFunc_, {AnyValue(libraryPathPtr)}, "dll.handle");
    BoolValue isNull = builder_.createPtrEQ(dllHandle, builder_.getNullPtr(), "is.null");
    builder_.createCondBr(isNull, errorBB, continueBB);

    // Error block
    builder_.setInsertPoint(errorBB);
    if (isVoidType(config.returnType)) {
        builder_.createRetVoid();
    } else {
        builder_.createRet(getDefaultReturnValue(config.returnType));
    }

    // Continue block: get symbol
    builder_.setInsertPoint(continueBB);
    PtrValue symbolPtr = builder_.createCallPtr(getSymbolFunc_,
        {AnyValue(dllHandle), AnyValue(symbolNamePtr)}, "sym.ptr");
    BoolValue isSymNull = builder_.createPtrEQ(symbolPtr, builder_.getNullPtr(), "is.sym.null");
    builder_.createCondBr(isSymNull, symErrorBB, callBB);

    // Symbol error block: free library and return default
    builder_.setInsertPoint(symErrorBB);
    builder_.createCallVoid(freeLibraryFunc_, {AnyValue(dllHandle)});
    if (isVoidType(config.returnType)) {
        builder_.createRetVoid();
    } else {
        builder_.createRet(getDefaultReturnValue(config.returnType));
    }

    // Call block: perform the indirect call and return
    builder_.setInsertPoint(callBB);

    // Build the function type for the native function (may exclude 'this')
    FunctionType* nativeFuncType = buildNativeFunctionType(config);

    // Build argument list (skip 'this' if instance method)
    std::vector<AnyValue> callArgs;
    size_t startIdx = config.isInstanceMethod ? 1 : 0;
    for (size_t i = startIdx; i < config.parameters.size(); ++i) {
        Argument* arg = result.function->getArg(i);
        callArgs.push_back(arg->toTypedValue());
    }

    // Convert calling convention
    CallingConv llvmCC = toLLVMCallingConv(config.callingConv);

    // Perform the indirect call
    if (isVoidType(config.returnType)) {
        builder_.createIndirectCallVoid(symbolPtr, nativeFuncType, callArgs, llvmCC);
        builder_.createRetVoid();
    } else {
        AnyValue callResult = builder_.createIndirectCall(symbolPtr, nativeFuncType, callArgs, "call.result", llvmCC);
        builder_.createRet(callResult);
    }

    return result;
}

FFIThunkResult FFIBuilder::createFFIThunk(
    std::string_view functionName,
    std::string_view libraryPath,
    std::string_view symbolName,
    Type* returnType,
    std::span<FFIParameter> parameters,
    FFICallingConv callingConv,
    bool isInstanceMethod) {

    FFIThunkConfig config;
    config.functionName = std::string(functionName);
    config.libraryPath = std::string(libraryPath);
    config.symbolName = std::string(symbolName);
    config.returnType = returnType;
    config.parameters = std::vector<FFIParameter>(parameters.begin(), parameters.end());
    config.callingConv = callingConv;
    config.isInstanceMethod = isInstanceMethod;

    return createFFIThunk(config);
}

// ============================================================================
// Calling Convention Helpers
// ============================================================================

CallingConv FFIBuilder::toLLVMCallingConv(FFICallingConv cc) {
    switch (cc) {
        case FFICallingConv::CDecl:
            return CallingConv::CDecl;
        case FFICallingConv::StdCall:
            return CallingConv::StdCall;
        case FFICallingConv::FastCall:
            return CallingConv::FastCall;
        case FFICallingConv::Win64:
            // Win64 uses the C calling convention on x64 Windows
            return CallingConv::CDecl;
        default:
            return CallingConv::CDecl;
    }
}

std::string FFIBuilder::getCallingConvString(FFICallingConv cc) {
    switch (cc) {
        case FFICallingConv::CDecl:
            return "ccc";
        case FFICallingConv::StdCall:
            return "x86_stdcallcc";
        case FFICallingConv::FastCall:
            return "x86_fastcallcc";
        case FFICallingConv::Win64:
            return "win64cc";
        default:
            return "ccc";
    }
}

// ============================================================================
// Type Helpers
// ============================================================================

AnyValue FFIBuilder::getDefaultReturnValue(Type* type) {
    TypeContext& ctx = module_.getContext();

    if (isVoidType(type)) {
        return AnyValue();  // Void
    }

    if (auto* intTy = dynamic_cast<IntegerType*>(type)) {
        return AnyValue(builder_.getIntN(intTy->getBitWidth(), 0));
    }

    if (auto* floatTy = dynamic_cast<FloatType*>(type)) {
        if (floatTy->getPrecision() == FloatType::Precision::Float) {
            return AnyValue(builder_.getFloat(0.0f));
        } else {
            return AnyValue(builder_.getDouble(0.0));
        }
    }

    if (dynamic_cast<PointerType*>(type)) {
        return AnyValue(builder_.getNullPtr());
    }

    // Default: null pointer
    return AnyValue(builder_.getNullPtr());
}

bool FFIBuilder::isVoidType(Type* type) {
    return type == nullptr || dynamic_cast<VoidType*>(type) != nullptr;
}

// ============================================================================
// Private Implementation Methods
// ============================================================================

void FFIBuilder::emitEntryBlock(Function* func, const FFIThunkConfig& config,
                                PtrValue libraryPathStr, PtrValue symbolNameStr,
                                BasicBlock* errorBlock, BasicBlock* continueBlock) {
    // This method is now handled inline in createFFIThunk for proper SSA form
}

void FFIBuilder::emitErrorBlock(BasicBlock* errorBlock, Type* returnType) {
    // This method is now handled inline in createFFIThunk for proper SSA form
}

void FFIBuilder::emitContinueBlock(BasicBlock* continueBlock, PtrValue dllHandle,
                                   PtrValue symbolNameStr,
                                   BasicBlock* symErrorBlock, BasicBlock* callBlock) {
    // This method is now handled inline in createFFIThunk for proper SSA form
}

void FFIBuilder::emitSymbolErrorBlock(BasicBlock* symErrorBlock, PtrValue dllHandle,
                                      Type* returnType) {
    // This method is now handled inline in createFFIThunk for proper SSA form
}

void FFIBuilder::emitCallBlock(BasicBlock* callBlock, const FFIThunkConfig& config,
                               PtrValue symbolPtr, Function* func) {
    // This method is now handled inline in createFFIThunk for proper SSA form
}

FunctionType* FFIBuilder::buildFunctionType(const FFIThunkConfig& config) {
    TypeContext& ctx = module_.getContext();

    std::vector<Type*> paramTypes;
    for (const auto& param : config.parameters) {
        paramTypes.push_back(param.type);
    }

    Type* retType = config.returnType;
    if (!retType) {
        retType = ctx.getVoidTy();
    }

    return ctx.getFunctionTy(retType, std::move(paramTypes));
}

FunctionType* FFIBuilder::buildNativeFunctionType(const FFIThunkConfig& config) {
    TypeContext& ctx = module_.getContext();

    std::vector<Type*> paramTypes;

    // Skip 'this' parameter for instance methods
    size_t startIdx = config.isInstanceMethod ? 1 : 0;
    for (size_t i = startIdx; i < config.parameters.size(); ++i) {
        paramTypes.push_back(config.parameters[i].type);
    }

    Type* retType = config.returnType;
    if (!retType) {
        retType = ctx.getVoidTy();
    }

    return ctx.getFunctionTy(retType, std::move(paramTypes));
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
