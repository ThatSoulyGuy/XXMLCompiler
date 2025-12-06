#pragma once

#include "Backends/LLVMIR/TypedModule.h"
#include "Backends/LLVMIR/TypedBuilder.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// FFIBuilder - Type-Safe FFI Thunk Builder
// ============================================================================
//
// This builder generates FFI (Foreign Function Interface) thunks using the
// typed IR system. It replaces all raw string emission for native method
// generation with type-safe builder calls.
//
// Key design principles:
// 1. No raw string emission - all IR generated through IRBuilder
// 2. All types validated through TypeContext
// 3. Generated thunks handle library loading, symbol lookup, and error handling
// 4. Supports multiple calling conventions (cdecl, stdcall, fastcall)

/// Calling convention for FFI functions
enum class FFICallingConv {
    CDecl,      // Default C calling convention
    StdCall,    // Windows stdcall
    FastCall,   // Fast calling convention
    Win64       // Windows x64 calling convention
};

/// Information about a single FFI parameter
struct FFIParameter {
    std::string name;       // Parameter name
    Type* type;             // LLVM type
    bool isCallback;        // Whether this is a callback function pointer

    FFIParameter() : type(nullptr), isCallback(false) {}

    FFIParameter(std::string_view n, Type* t, bool cb = false)
        : name(n), type(t), isCallback(cb) {}
};

/// Configuration for generating an FFI thunk
struct FFIThunkConfig {
    std::string functionName;       // Name of the XXML wrapper function
    std::string libraryPath;        // Path to the native library (e.g., "glfw3.dll")
    std::string symbolName;         // Name of the symbol to load
    Type* returnType;               // Return type (void for none)
    std::vector<FFIParameter> parameters;  // Function parameters
    FFICallingConv callingConv;     // Calling convention
    bool isInstanceMethod;          // Whether 'this' is the first parameter (skip it for FFI call)

    FFIThunkConfig()
        : returnType(nullptr),
          callingConv(FFICallingConv::CDecl),
          isInstanceMethod(false) {}
};

/// Result of creating an FFI thunk
struct FFIThunkResult {
    Function* function;             // The generated thunk function
    GlobalVariable* libraryPathStr; // Global string for library path
    GlobalVariable* symbolNameStr;  // Global string for symbol name
};

class FFIBuilder {
public:
    FFIBuilder(Module& module, IRBuilder& builder, GlobalBuilder& globalBuilder);

    // ========================================================================
    // FFI Runtime Declaration
    // ========================================================================

    /// Declare the FFI runtime functions if not already declared.
    /// These are: xxml_FFI_loadLibrary, xxml_FFI_getSymbol, xxml_FFI_freeLibrary
    void declareFFIRuntime();

    /// Check if FFI runtime is already declared
    bool isFFIRuntimeDeclared() const;

    // ========================================================================
    // Thunk Generation
    // ========================================================================

    /// Generate a complete FFI thunk function from config.
    /// This is the main entry point for FFI thunk generation.
    FFIThunkResult createFFIThunk(const FFIThunkConfig& config);

    /// Generate an FFI thunk from individual parameters (convenience overload).
    FFIThunkResult createFFIThunk(
        std::string_view functionName,
        std::string_view libraryPath,
        std::string_view symbolName,
        Type* returnType,
        std::span<FFIParameter> parameters,
        FFICallingConv callingConv = FFICallingConv::CDecl,
        bool isInstanceMethod = false);

    // ========================================================================
    // Calling Convention Helpers
    // ========================================================================

    /// Convert FFI calling convention to LLVM calling convention
    static CallingConv toLLVMCallingConv(FFICallingConv cc);

    /// Get the calling convention string for IR output
    static std::string getCallingConvString(FFICallingConv cc);

    // ========================================================================
    // Type Helpers
    // ========================================================================

    /// Get default return value for a type (used in error handling blocks)
    AnyValue getDefaultReturnValue(Type* type);

    /// Check if a type is void
    static bool isVoidType(Type* type);

    // ========================================================================
    // Module & Context Accessors
    // ========================================================================

    Module& getModule() { return module_; }
    const Module& getModule() const { return module_; }

    TypeContext& getContext() { return module_.getContext(); }
    const TypeContext& getContext() const { return module_.getContext(); }

    IRBuilder& getIRBuilder() { return builder_; }
    GlobalBuilder& getGlobalBuilder() { return globalBuilder_; }

private:
    Module& module_;
    IRBuilder& builder_;
    GlobalBuilder& globalBuilder_;

    // FFI runtime function pointers (cached after first declaration)
    Function* loadLibraryFunc_ = nullptr;
    Function* getSymbolFunc_ = nullptr;
    Function* freeLibraryFunc_ = nullptr;

    // Internal implementation methods
    void emitEntryBlock(Function* func, const FFIThunkConfig& config,
                        PtrValue libraryPathStr, PtrValue symbolNameStr,
                        BasicBlock* errorBlock, BasicBlock* continueBlock);

    void emitErrorBlock(BasicBlock* errorBlock, Type* returnType);

    void emitContinueBlock(BasicBlock* continueBlock, PtrValue dllHandle,
                           PtrValue symbolNameStr,
                           BasicBlock* symErrorBlock, BasicBlock* callBlock);

    void emitSymbolErrorBlock(BasicBlock* symErrorBlock, PtrValue dllHandle,
                              Type* returnType);

    void emitCallBlock(BasicBlock* callBlock, const FFIThunkConfig& config,
                       PtrValue symbolPtr, Function* func);

    // Helper to build function type from config
    FunctionType* buildFunctionType(const FFIThunkConfig& config);

    // Helper to build native function type (without 'this' for instance methods)
    FunctionType* buildNativeFunctionType(const FFIThunkConfig& config);
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
