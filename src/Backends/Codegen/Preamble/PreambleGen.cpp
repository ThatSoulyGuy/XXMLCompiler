#include "Backends/Codegen/Preamble/PreambleGen.h"
#include <sstream>

namespace XXML {
namespace Backends {
namespace Codegen {

PreambleGen::PreambleGen(CodegenContext& ctx)
    : ctx_(ctx) {}

std::string PreambleGen::getTargetTriple() const {
#ifdef _WIN32
    return "x86_64-pc-windows-msvc";
#elif defined(__APPLE__)
    return "arm64-apple-darwin";
#else
    return "x86_64-unknown-linux-gnu";
#endif
}

std::string PreambleGen::getDataLayout() const {
#ifdef _WIN32
    return "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
#elif defined(__APPLE__)
    return "e-m:o-i64:64-i128:128-n32:64-S128";
#else
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
#endif
}

void PreambleGen::declareFunc(const std::string& name, LLVMIR::Type* retTy,
                               std::vector<LLVMIR::Type*> paramTys) {
    auto* fn = ctx_.module().getFunction(name);
    if (!fn) {
        auto* funcType = ctx_.module().getContext().getFunctionTy(retTy, paramTys, false);
        ctx_.module().createFunction(funcType, name, LLVMIR::Function::Linkage::External);
    }
}

void PreambleGen::generateBuiltinTypes() {
    // Built-in types are created via module initialization
    // This is a placeholder for future type-safe struct creation
}

void PreambleGen::declareRuntimeFunctions() {
    declareMemoryFunctions();
    declareIntegerFunctions();
    declareFloatFunctions();
    declareDoubleFunctions();
    declareStringFunctions();
    declareBoolFunctions();
    declareConsoleFunctions();
    declareReflectionFunctions();
    declareAnnotationFunctions();
    declareFFIFunctions();
    declareThreadingFunctions();
    declareFileFunctions();
    declareUtilityFunctions();
}

void PreambleGen::declareMemoryFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i32Ty = llvmCtx.getInt32Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* i8Ty = llvmCtx.getInt8Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("xxml_malloc", ptrTy, {i64Ty});
    declareFunc("xxml_free", voidTy, {ptrTy});
    declareFunc("xxml_memcpy", ptrTy, {ptrTy, ptrTy, i64Ty});
    declareFunc("xxml_memset", ptrTy, {ptrTy, i32Ty, i64Ty});
    declareFunc("xxml_ptr_read", ptrTy, {ptrTy});
    declareFunc("xxml_ptr_write", voidTy, {ptrTy, ptrTy});
    declareFunc("xxml_read_byte", i8Ty, {ptrTy});
    declareFunc("xxml_write_byte", voidTy, {ptrTy, i8Ty});
}

void PreambleGen::declareIntegerFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();

    declareFunc("Integer_Constructor", ptrTy, {i64Ty});
    declareFunc("Integer_getValue", i64Ty, {ptrTy});
    declareFunc("Integer_add", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_sub", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_mul", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_div", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_negate", ptrTy, {ptrTy});
    declareFunc("Integer_mod", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_eq", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_ne", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_lt", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_le", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_gt", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_ge", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_toInt64", i64Ty, {ptrTy});
    declareFunc("Integer_toString", ptrTy, {ptrTy});
}

void PreambleGen::declareFloatFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* floatTy = llvmCtx.getFloatTy();

    declareFunc("Float_Constructor", ptrTy, {floatTy});
    declareFunc("Float_getValue", floatTy, {ptrTy});
    declareFunc("Float_toString", ptrTy, {ptrTy});
}

void PreambleGen::declareDoubleFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* doubleTy = llvmCtx.getDoubleTy();

    declareFunc("Double_Constructor", ptrTy, {doubleTy});
    declareFunc("Double_getValue", doubleTy, {ptrTy});
    declareFunc("Double_toString", ptrTy, {ptrTy});
}

void PreambleGen::declareStringFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("String_Constructor", ptrTy, {ptrTy});
    declareFunc("String_FromCString", ptrTy, {ptrTy});
    declareFunc("String_toCString", ptrTy, {ptrTy});
    declareFunc("String_length", i64Ty, {ptrTy});
    declareFunc("String_concat", ptrTy, {ptrTy, ptrTy});
    declareFunc("String_append", ptrTy, {ptrTy, ptrTy});
    declareFunc("String_equals", i1Ty, {ptrTy, ptrTy});
    declareFunc("String_isEmpty", i1Ty, {ptrTy});
    declareFunc("String_destroy", voidTy, {ptrTy});
}

void PreambleGen::declareBoolFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();

    declareFunc("Bool_Constructor", ptrTy, {i1Ty});
    declareFunc("Bool_getValue", i1Ty, {ptrTy});
    declareFunc("Bool_and", ptrTy, {ptrTy, ptrTy});
    declareFunc("Bool_or", ptrTy, {ptrTy, ptrTy});
    declareFunc("Bool_not", ptrTy, {ptrTy});
    declareFunc("None_Constructor", ptrTy, {});
}

void PreambleGen::declareConsoleFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("Console_print", voidTy, {ptrTy});
    declareFunc("Console_printLine", voidTy, {ptrTy});
    declareFunc("Console_printInt", voidTy, {i64Ty});
    declareFunc("Console_printBool", voidTy, {i1Ty});

    // List operations
    declareFunc("List_Constructor", ptrTy, {});
    declareFunc("List_add", voidTy, {ptrTy, ptrTy});
    declareFunc("List_get", ptrTy, {ptrTy, i64Ty});
    declareFunc("List_size", i64Ty, {ptrTy});

    // System
    auto* i32Ty = llvmCtx.getInt32Ty();
    declareFunc("xxml_exit", voidTy, {i32Ty});
    declareFunc("exit", voidTy, {i32Ty});
}

void PreambleGen::declareReflectionFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i32Ty = llvmCtx.getInt32Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("Reflection_registerType", voidTy, {ptrTy});
    declareFunc("Reflection_getTypeInfo", ptrTy, {ptrTy});
    declareFunc("Reflection_getTypeCount", i32Ty, {});
    declareFunc("Reflection_getAllTypeNames", ptrTy, {});
}

void PreambleGen::declareAnnotationFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i32Ty = llvmCtx.getInt32Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("Annotation_registerForType", voidTy, {ptrTy, i32Ty, ptrTy});
    declareFunc("Annotation_getCountForType", i32Ty, {ptrTy});
    declareFunc("Annotation_getForType", ptrTy, {ptrTy, i32Ty});
}

void PreambleGen::declareFFIFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* floatTy = llvmCtx.getFloatTy();
    auto* doubleTy = llvmCtx.getDoubleTy();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("xxml_FFI_loadLibrary", ptrTy, {ptrTy});
    declareFunc("xxml_FFI_getSymbol", ptrTy, {ptrTy, ptrTy});
    declareFunc("xxml_FFI_freeLibrary", voidTy, {ptrTy});
    declareFunc("xxml_FFI_getError", ptrTy, {});
    declareFunc("xxml_FFI_libraryExists", i1Ty, {ptrTy});

    declareFunc("xxml_FFI_stringToCString", ptrTy, {ptrTy});
    declareFunc("xxml_FFI_cstringToString", ptrTy, {ptrTy});
    declareFunc("xxml_FFI_integerToInt64", i64Ty, {ptrTy});
    declareFunc("xxml_FFI_int64ToInteger", ptrTy, {i64Ty});
    declareFunc("xxml_FFI_floatToC", floatTy, {ptrTy});
    declareFunc("xxml_FFI_cToFloat", ptrTy, {floatTy});
    declareFunc("xxml_FFI_doubleToC", doubleTy, {ptrTy});
    declareFunc("xxml_FFI_cToDouble", ptrTy, {doubleTy});
    declareFunc("xxml_FFI_boolToC", i1Ty, {ptrTy});
    declareFunc("xxml_FFI_cToBool", ptrTy, {i1Ty});
}

void PreambleGen::declareThreadingFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("xxml_Thread_create", ptrTy, {ptrTy, ptrTy});
    declareFunc("xxml_Thread_join", i64Ty, {ptrTy});
    declareFunc("xxml_Thread_detach", i64Ty, {ptrTy});
    declareFunc("xxml_Thread_isJoinable", i1Ty, {ptrTy});
    declareFunc("xxml_Thread_sleep", voidTy, {i64Ty});
    declareFunc("xxml_Thread_yield", voidTy, {});
    declareFunc("xxml_Thread_currentId", i64Ty, {});

    declareFunc("xxml_Mutex_create", ptrTy, {});
    declareFunc("xxml_Mutex_destroy", voidTy, {ptrTy});
    declareFunc("xxml_Mutex_lock", i64Ty, {ptrTy});
    declareFunc("xxml_Mutex_unlock", i64Ty, {ptrTy});
    declareFunc("xxml_Mutex_tryLock", i1Ty, {ptrTy});
}

void PreambleGen::declareFileFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i1Ty = llvmCtx.getInt1Ty();
    auto* i64Ty = llvmCtx.getInt64Ty();
    auto* voidTy = llvmCtx.getVoidTy();

    declareFunc("xxml_File_open", ptrTy, {ptrTy, ptrTy});
    declareFunc("xxml_File_close", voidTy, {ptrTy});
    declareFunc("xxml_File_read", i64Ty, {ptrTy, ptrTy, i64Ty});
    declareFunc("xxml_File_write", i64Ty, {ptrTy, ptrTy, i64Ty});
    declareFunc("xxml_File_readLine", ptrTy, {ptrTy});
    declareFunc("xxml_File_readAll", ptrTy, {ptrTy});
    declareFunc("xxml_File_exists", i1Ty, {ptrTy});
}

void PreambleGen::declareUtilityFunctions() {
    auto& llvmCtx = ctx_.module().getContext();
    auto* ptrTy = llvmCtx.getPtrTy();
    auto* i64Ty = llvmCtx.getInt64Ty();

    declareFunc("xxml_string_create", ptrTy, {ptrTy});
    declareFunc("xxml_string_concat", ptrTy, {ptrTy, ptrTy});
    declareFunc("xxml_string_hash", i64Ty, {ptrTy});
    declareFunc("xxml_ptr_is_null", i64Ty, {ptrTy});
    declareFunc("xxml_ptr_null", ptrTy, {});
}

std::string PreambleGen::generate() {
    std::stringstream preamble;

    preamble << "; Generated by XXML Compiler v2.0 (LLVM IR Backend)\n";
    preamble << "; Target: LLVM IR 17.0\n";
    preamble << ";\n\n";

    preamble << "target triple = \"" << getTargetTriple() << "\"\n";
    preamble << "target datalayout = \"" << getDataLayout() << "\"\n\n";

    preamble << "; Built-in type definitions\n";
    preamble << "%Integer = type { i64 }\n";
    preamble << "%String = type { ptr, i64 }\n";
    preamble << "%Bool = type { i1 }\n";
    preamble << "%Float = type { float }\n";
    preamble << "%Double = type { double }\n";
    preamble << "\n";

    return preamble.str();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
