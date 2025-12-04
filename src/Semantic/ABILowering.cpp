#include "../../include/Semantic/ABILowering.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <regex>

namespace XXML {
namespace Semantic {

ABILowering::ABILowering(Common::ErrorReporter& errorReporter,
                         const TypeResolutionResult& typeResolution,
                         const LayoutComputationResult& layoutResult,
                         TargetPlatform platform)
    : errorReporter_(errorReporter),
      typeResolution_(typeResolution),
      layoutResult_(layoutResult),
      platform_(platform) {
}

ABILoweringResult ABILowering::run(
    const std::vector<Parser::MethodDecl*>& nativeMethods,
    const std::unordered_map<std::string, SemanticAnalyzer::CallbackTypeInfo>& callbackTypes) {

    result_ = ABILoweringResult{};

    // Lower native methods
    for (auto* method : nativeMethods) {
        if (!method || !method->isNative) continue;

        LoweredSignature sig = lowerMethod(method);
        validateSignature(sig);

        if (sig.isValid) {
            result_.nativeMethods[sig.mangledName] = sig;
        }
    }

    // Lower callback types
    for (const auto& [name, info] : callbackTypes) {
        CallbackThunkInfo thunk = lowerCallback(name, info);
        result_.callbackThunks[name] = thunk;
    }

    result_.success = !errorReporter_.hasErrors();
    return result_;
}

const LoweredSignature* ABILowering::getSignature(const std::string& methodName) const {
    auto it = result_.nativeMethods.find(methodName);
    return it != result_.nativeMethods.end() ? &it->second : nullptr;
}

const CallbackThunkInfo* ABILowering::getCallbackThunk(const std::string& callbackName) const {
    auto it = result_.callbackThunks.find(callbackName);
    return it != result_.callbackThunks.end() ? &it->second : nullptr;
}

bool ABILowering::validatePlatformABI() const {
    // Validate that all signatures are compatible with target platform
    for (const auto& [name, sig] : result_.nativeMethods) {
        if (!isValidCallingConvention(sig.convention)) {
            return false;
        }
    }
    return true;
}

//==============================================================================
// METHOD LOWERING
//==============================================================================

LoweredSignature ABILowering::lowerMethod(Parser::MethodDecl* method) {
    LoweredSignature sig;
    sig.functionName = method->name;
    sig.mangledName = method->name;  // Native methods use their symbol name
    sig.dllPath = method->nativePath;
    sig.symbolName = method->nativeSymbol.empty() ? method->name : method->nativeSymbol;
    sig.convention = resolveCallingConvention(method->callingConvention);
    sig.isValid = true;

    // Lower parameters
    for (auto& param : method->parameters) {
        NativeParamInfo paramInfo = lowerParameter(param.get());
        validateParameter(paramInfo, method->name);
        sig.parameters.push_back(paramInfo);

        if (paramInfo.marshal != MarshalStrategy::None) {
            sig.requiresMarshaling = true;
        }
    }

    // Lower return type
    sig.returnInfo = lowerReturnType(method->returnType.get());
    validateReturnType(sig.returnInfo, method->name);

    if (sig.returnInfo.marshal != MarshalStrategy::None) {
        sig.requiresMarshaling = true;
    }

    return sig;
}

CallbackThunkInfo ABILowering::lowerCallback(const std::string& name,
                                              const SemanticAnalyzer::CallbackTypeInfo& info) {
    CallbackThunkInfo thunk;
    thunk.thunkName = "callback_thunk_" + name;
    thunk.callbackTypeName = name;
    thunk.convention = info.convention;
    thunk.returnType = mapTypeToLLVM(info.returnType);

    for (const auto& param : info.parameters) {
        thunk.paramTypes.push_back(mapTypeToLLVM(param.typeName));
    }

    return thunk;
}

//==============================================================================
// PARAMETER/RETURN LOWERING
//==============================================================================

NativeParamInfo ABILowering::lowerParameter(Parser::ParameterDecl* param) {
    NativeParamInfo info;
    if (!param || !param->type) {
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::None;
        return info;
    }

    info.paramName = param->name;
    info.xxmlType = param->type->typeName;
    info.ownership = param->type->ownership;

    // Check if it's a NativeType
    if (isNativeType(info.xxmlType)) {
        std::string nativeTypeName = extractNativeTypeName(info.xxmlType);
        info.llvmType = mapNativeTypeToLLVM(nativeTypeName);
        info.marshal = MarshalStrategy::None;
        info.isPointer = isPointerType(nativeTypeName);
    } else if (isPrimitiveType(info.xxmlType)) {
        // XXML primitive types need marshaling
        info.llvmType = mapTypeToLLVM(info.xxmlType);
        info.marshal = determineMarshalStrategy(info.xxmlType, info.llvmType, true);
    } else if (isStringType(info.xxmlType)) {
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::StringToC;
    } else if (isCallbackType(info.xxmlType)) {
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::CallbackThunk;
        info.isCallback = true;
    } else {
        // Object types are pointers
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::ObjectToPtr;
        info.isPointer = true;
    }

    return info;
}

NativeReturnInfo ABILowering::lowerReturnType(Parser::TypeRef* type) {
    NativeReturnInfo info;

    if (!type || type->typeName == "Void" || type->typeName == "None") {
        info.xxmlType = "Void";
        info.llvmType = "void";
        info.marshal = MarshalStrategy::None;
        info.isVoid = true;
        return info;
    }

    info.xxmlType = type->typeName;
    info.isVoid = false;

    if (isNativeType(info.xxmlType)) {
        std::string nativeTypeName = extractNativeTypeName(info.xxmlType);
        info.llvmType = mapNativeTypeToLLVM(nativeTypeName);
        info.marshal = MarshalStrategy::None;
    } else if (isPrimitiveType(info.xxmlType)) {
        info.llvmType = mapTypeToLLVM(info.xxmlType);
        info.marshal = determineMarshalStrategy(info.xxmlType, info.llvmType, false);
    } else if (isStringType(info.xxmlType)) {
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::CToString;
    } else {
        info.llvmType = "ptr";
        info.marshal = MarshalStrategy::PtrToObject;
    }

    return info;
}

//==============================================================================
// TYPE MAPPING
//==============================================================================

std::string ABILowering::mapTypeToLLVM(const std::string& xxmlType) const {
    if (xxmlType == "Integer" || xxmlType == "Language::Core::Integer") return "i64";
    if (xxmlType == "Float" || xxmlType == "Language::Core::Float") return "float";
    if (xxmlType == "Double" || xxmlType == "Language::Core::Double") return "double";
    if (xxmlType == "Bool" || xxmlType == "Language::Core::Bool") return "i1";
    if (xxmlType == "String" || xxmlType == "Language::Core::String") return "ptr";
    if (xxmlType == "Void" || xxmlType == "None") return "void";
    return "ptr";  // Default to pointer for objects
}

std::string ABILowering::mapNativeTypeToLLVM(const std::string& nativeType) const {
    if (nativeType == "int8") return "i8";
    if (nativeType == "int16") return "i16";
    if (nativeType == "int32") return "i32";
    if (nativeType == "int64") return "i64";
    if (nativeType == "uint8") return "i8";
    if (nativeType == "uint16") return "i16";
    if (nativeType == "uint32") return "i32";
    if (nativeType == "uint64") return "i64";
    if (nativeType == "float") return "float";
    if (nativeType == "double") return "double";
    if (nativeType == "bool") return "i1";
    if (nativeType == "ptr") return "ptr";
    if (nativeType == "cstr") return "ptr";
    if (nativeType == "string_ptr") return "ptr";
    if (nativeType == "void") return "void";
    return "ptr";  // Default to pointer
}

MarshalStrategy ABILowering::determineMarshalStrategy(const std::string& xxmlType,
                                                       const std::string& nativeType,
                                                       bool isParameter) const {
    if (xxmlType == "Integer" || xxmlType == "Language::Core::Integer") {
        return isParameter ? MarshalStrategy::IntToI64 : MarshalStrategy::I64ToInt;
    }
    if (xxmlType == "Float" || xxmlType == "Language::Core::Float") {
        return isParameter ? MarshalStrategy::FloatToC : MarshalStrategy::CToFloat;
    }
    if (xxmlType == "Double" || xxmlType == "Language::Core::Double") {
        return isParameter ? MarshalStrategy::DoubleToC : MarshalStrategy::CToDouble;
    }
    if (xxmlType == "Bool" || xxmlType == "Language::Core::Bool") {
        return isParameter ? MarshalStrategy::BoolToI1 : MarshalStrategy::I1ToBool;
    }
    if (xxmlType == "String" || xxmlType == "Language::Core::String") {
        return isParameter ? MarshalStrategy::StringToC : MarshalStrategy::CToString;
    }
    return MarshalStrategy::None;
}

//==============================================================================
// TYPE CHECKING
//==============================================================================

std::string ABILowering::extractNativeTypeName(const std::string& nativeTypeSpec) const {
    // Extract type from NativeType<X>
    static std::regex pattern(R"(NativeType<(\w+)>)");
    std::smatch match;
    if (std::regex_match(nativeTypeSpec, match, pattern)) {
        return match[1].str();
    }
    return nativeTypeSpec;
}

bool ABILowering::isNativeType(const std::string& typeName) const {
    return typeName.find("NativeType<") == 0;
}

bool ABILowering::isPrimitiveType(const std::string& typeName) const {
    return typeName == "Integer" || typeName == "Float" || typeName == "Double" ||
           typeName == "Bool" || typeName == "String" ||
           typeName == "Language::Core::Integer" ||
           typeName == "Language::Core::Float" ||
           typeName == "Language::Core::Double" ||
           typeName == "Language::Core::Bool" ||
           typeName == "Language::Core::String";
}

bool ABILowering::isStringType(const std::string& typeName) const {
    return typeName == "String" || typeName == "Language::Core::String";
}

bool ABILowering::isPointerType(const std::string& typeName) const {
    return typeName == "ptr" || typeName == "cstr" || typeName == "string_ptr";
}

bool ABILowering::isCallbackType(const std::string& typeName) const {
    // TODO: Check callback type registry
    return typeName.find("Callback") != std::string::npos;
}

//==============================================================================
// CALLING CONVENTION
//==============================================================================

Parser::CallingConvention ABILowering::resolveCallingConvention(
    Parser::CallingConvention requested) const {

    if (requested == Parser::CallingConvention::Auto) {
        // Use platform default
        switch (platform_) {
            case TargetPlatform::Windows_x64:
                return Parser::CallingConvention::CDecl;
            case TargetPlatform::Linux_x64:
            case TargetPlatform::macOS_x64:
            case TargetPlatform::macOS_ARM64:
                return Parser::CallingConvention::CDecl;
        }
    }
    return requested;
}

bool ABILowering::isValidCallingConvention(Parser::CallingConvention conv) const {
    switch (platform_) {
        case TargetPlatform::Windows_x64:
            return conv == Parser::CallingConvention::CDecl ||
                   conv == Parser::CallingConvention::StdCall ||
                   conv == Parser::CallingConvention::FastCall;
        case TargetPlatform::Linux_x64:
        case TargetPlatform::macOS_x64:
            return conv == Parser::CallingConvention::CDecl;
        case TargetPlatform::macOS_ARM64:
            return conv == Parser::CallingConvention::CDecl;
    }
    return false;
}

std::string ABILowering::callingConventionToString(Parser::CallingConvention conv) const {
    switch (conv) {
        case Parser::CallingConvention::Auto: return "auto";
        case Parser::CallingConvention::CDecl: return "cdecl";
        case Parser::CallingConvention::StdCall: return "stdcall";
        case Parser::CallingConvention::FastCall: return "fastcall";
    }
    return "unknown";
}

//==============================================================================
// VALIDATION
//==============================================================================

void ABILowering::validateSignature(const LoweredSignature& sig) {
    if (!isValidCallingConvention(sig.convention)) {
        errorReporter_.reportError(
            Common::ErrorCode::InvalidSyntax,
            "Invalid calling convention '" + callingConventionToString(sig.convention) +
            "' for method '" + sig.functionName + "' on current platform",
            Common::SourceLocation{}
        );
    }
}

void ABILowering::validateParameter(const NativeParamInfo& param, const std::string& methodName) {
    if (param.llvmType.empty()) {
        errorReporter_.reportError(
            Common::ErrorCode::UndefinedType,
            "Cannot determine LLVM type for parameter '" + param.paramName +
            "' of native method '" + methodName + "'",
            Common::SourceLocation{}
        );
    }
}

void ABILowering::validateReturnType(const NativeReturnInfo& ret, const std::string& methodName) {
    if (ret.llvmType.empty() && !ret.isVoid) {
        errorReporter_.reportError(
            Common::ErrorCode::UndefinedType,
            "Cannot determine LLVM type for return type of native method '" + methodName + "'",
            Common::SourceLocation{}
        );
    }
}

void ABILowering::reportInvalidFFIType(const std::string& typeName, const std::string& context,
                                        const Common::SourceLocation& loc) {
    errorReporter_.reportError(
        Common::ErrorCode::UndefinedType,
        "Invalid FFI type '" + typeName + "' in " + context,
        loc
    );
}

void ABILowering::reportInvalidCallingConvention(const std::string& convention,
                                                  const Common::SourceLocation& loc) {
    errorReporter_.reportError(
        Common::ErrorCode::InvalidSyntax,
        "Invalid calling convention '" + convention + "'",
        loc
    );
}

void ABILowering::reportMissingMarshalStrategy(const std::string& typeName,
                                                const Common::SourceLocation& loc) {
    errorReporter_.reportError(
        Common::ErrorCode::TypeMismatch,
        "No marshaling strategy available for type '" + typeName + "'",
        loc
    );
}

} // namespace Semantic
} // namespace XXML
