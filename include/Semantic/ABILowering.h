#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include "PassResults.h"
#include "SemanticError.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// ABI LOWERING
//
// This pass resolves all FFI signatures and calling conventions.
// It performs:
//   1. FFI signature resolution (NativeType mapping)
//   2. Calling convention assignment and validation
//   3. Parameter/return marshaling strategy selection
//   4. Lambda closure lowering for callbacks
//
// After this pass completes successfully:
//   - Each FFI method has exact NativeType signature
//   - All parameters match the native ABI
//   - Pointers are PtrValue only
//   - String marshaling is mapped correctly
//   - Calling conventions are validated for platform
//==============================================================================

class ABILowering {
public:
    // Target platform for ABI validation
    enum class TargetPlatform {
        Windows_x64,
        Linux_x64,
        macOS_x64,
        macOS_ARM64
    };

    ABILowering(Common::ErrorReporter& errorReporter,
                const TypeResolutionResult& typeResolution,
                const LayoutComputationResult& layoutResult,
                TargetPlatform platform = TargetPlatform::Windows_x64);

    // Main entry point
    ABILoweringResult run(
        const std::vector<Parser::MethodDecl*>& nativeMethods,
        const std::unordered_map<std::string, CallbackTypeInfo>& callbackTypes);

    // Get the result
    const ABILoweringResult& result() const { return result_; }

    // Query lowered signatures
    const LoweredSignature* getSignature(const std::string& methodName) const;
    const CallbackThunkInfo* getCallbackThunk(const std::string& callbackName) const;

    // Platform compatibility check
    bool validatePlatformABI() const;

private:
    Common::ErrorReporter& errorReporter_;
    const TypeResolutionResult& typeResolution_;
    const LayoutComputationResult& layoutResult_;
    TargetPlatform platform_;
    ABILoweringResult result_;

    // Lower a native method
    LoweredSignature lowerMethod(Parser::MethodDecl* method);

    // Lower a callback type
    CallbackThunkInfo lowerCallback(const std::string& name,
                                    const CallbackTypeInfo& info);

    // Parameter lowering
    NativeParamInfo lowerParameter(Parser::ParameterDecl* param);
    NativeReturnInfo lowerReturnType(Parser::TypeRef* type);

    // Type mapping
    std::string mapTypeToLLVM(const std::string& xxmlType) const;
    std::string mapNativeTypeToLLVM(const std::string& nativeType) const;
    MarshalStrategy determineMarshalStrategy(const std::string& xxmlType,
                                             const std::string& nativeType,
                                             bool isParameter) const;

    // NativeType extraction
    std::string extractNativeTypeName(const std::string& nativeTypeSpec) const;
    bool isNativeType(const std::string& typeName) const;
    bool isPrimitiveType(const std::string& typeName) const;
    bool isStringType(const std::string& typeName) const;
    bool isPointerType(const std::string& typeName) const;
    bool isCallbackType(const std::string& typeName) const;

    // Calling convention handling
    Parser::CallingConvention resolveCallingConvention(
        Parser::CallingConvention requested) const;
    bool isValidCallingConvention(Parser::CallingConvention conv) const;
    std::string callingConventionToString(Parser::CallingConvention conv) const;

    // Validation
    void validateSignature(const LoweredSignature& sig);
    void validateParameter(const NativeParamInfo& param, const std::string& methodName);
    void validateReturnType(const NativeReturnInfo& ret, const std::string& methodName);

    // Error reporting
    void reportInvalidFFIType(const std::string& typeName, const std::string& context,
                              const Common::SourceLocation& loc);
    void reportInvalidCallingConvention(const std::string& convention,
                                        const Common::SourceLocation& loc);
    void reportMissingMarshalStrategy(const std::string& typeName,
                                      const Common::SourceLocation& loc);
};

} // namespace Semantic
} // namespace XXML
