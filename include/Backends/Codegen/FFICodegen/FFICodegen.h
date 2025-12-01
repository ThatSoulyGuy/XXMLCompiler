#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/TypedModule.h"
#include "Parser/AST.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace XXML {
namespace Backends {
namespace Codegen {

// NativeMethodInfo and CallbackThunkInfo are defined in CodegenContext.h

/**
 * @brief Generates FFI-related code (marshaling, thunks, callbacks)
 *
 * Responsible for:
 * - Generating FFI thunk functions that load DLLs and call native symbols
 * - Marshaling XXML types to/from C types
 * - Generating callback wrapper thunks for XXML lambdas passed to C code
 * - Type conversion between XXML and native types
 */
class FFICodegen {
public:
    explicit FFICodegen(CodegenContext& ctx);
    ~FFICodegen() = default;

    // === Native Method Registration ===

    /// Register a native method for later call-site handling
    void registerNativeMethod(const std::string& mangledName, const NativeMethodInfo& info);

    /// Get info for a registered native method
    const NativeMethodInfo* getNativeMethodInfo(const std::string& mangledName) const;

    /// Check if a method is a registered native method
    bool isNativeMethod(const std::string& mangledName) const;

    // === Callback Thunk Generation ===

    /// Register a callback thunk for an XXML type
    void registerCallbackThunk(const std::string& typeName, const CallbackThunkInfo& info);

    /// Get callback thunk for a type
    LLVMIR::Function* getCallbackThunk(const std::string& typeName) const;

    // === Type Mapping ===

    /// Map XXML type name to LLVM type string
    std::string mapXXMLTypeToLLVM(const std::string& xxmlType) const;

    /// Map NativeType<"..."> to LLVM type
    std::string mapNativeTypeToLLVM(const std::string& nativeType) const;

    /// Get LLVM IR type for a native type string
    LLVMIR::Type* getLLVMType(const std::string& typeStr) const;

    // === Marshaling Code Generation ===

    /// Generate code to marshal an XXML String to C string (ptr)
    LLVMIR::PtrValue marshalStringToC(LLVMIR::AnyValue xxmlString);

    /// Generate code to marshal a C string (ptr) to XXML String
    LLVMIR::PtrValue marshalCToString(LLVMIR::PtrValue cString);

    /// Generate code to marshal XXML Integer to i64
    LLVMIR::IntValue marshalIntegerToC(LLVMIR::AnyValue xxmlInteger);

    /// Generate code to marshal i64 to XXML Integer
    LLVMIR::PtrValue marshalCToInteger(LLVMIR::IntValue cInt);

    /// Generate code to marshal XXML Float to C float
    LLVMIR::FloatValue marshalFloatToC(LLVMIR::AnyValue xxmlFloat);

    /// Generate code to marshal C float to XXML Float
    LLVMIR::PtrValue marshalCToFloat(LLVMIR::FloatValue cFloat);

    /// Generate code to marshal XXML Bool to i1
    LLVMIR::BoolValue marshalBoolToC(LLVMIR::AnyValue xxmlBool);

    /// Generate code to marshal i1 to XXML Bool
    LLVMIR::PtrValue marshalCToBool(LLVMIR::BoolValue cBool);

    // === FFI Thunk Generation ===

    /// Generate a complete FFI thunk function for a native method
    LLVMIR::Function* generateThunk(Parser::MethodDecl* decl,
                                     const std::string& funcName,
                                     bool isInstanceMethod);

    // === Calling Convention ===

    /// Map XXML calling convention to LLVM calling convention
    LLVMIR::CallingConv mapCallingConv(Parser::CallingConvention cc) const;

private:
    /// Emit a default return value for the given type
    void emitDefaultReturn(const std::string& returnType);
    CodegenContext& ctx_;
    std::unordered_map<std::string, NativeMethodInfo> nativeMethods_;
    std::unordered_map<std::string, CallbackThunkInfo> callbackThunks_;

    /// Get or declare an FFI runtime function
    LLVMIR::Function* getOrDeclareFFIFunc(const std::string& name,
                                           LLVMIR::Type* retTy,
                                           std::vector<LLVMIR::Type*> paramTys);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
