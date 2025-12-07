#include "Backends/Codegen/NativeCodegen/NativeCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/TypeNormalizer.h"
#include "Core/TypeRegistry.h"
#include "Core/CompilationContext.h"

namespace XXML::Backends::Codegen {

NativeCodegen::NativeCodegen(CodegenContext& ctx, Core::CompilationContext* compCtx)
    : ctx_(ctx), compCtx_(compCtx) {
    // Initialize the type-safe builders
    globalBuilder_ = std::make_unique<LLVMIR::GlobalBuilder>(ctx_.module());
    ffiBuilder_ = std::make_unique<LLVMIR::FFIBuilder>(ctx_.module(), ctx_.builder(), *globalBuilder_);
}

std::string NativeCodegen::getQualifiedName(const std::string& className, const std::string& methodName) const {
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
    std::string combined = mangledClassName + "_" + methodName;
    // Apply mangling to remove consecutive underscores (e.g., "Native__glfwInit" -> "Native_glfwInit")
    return TypeNormalizer::mangleForLLVM(combined);
}

std::string NativeCodegen::mapNativeTypeToLLVMString(const std::string& nativeType) const {
    // Use TypeRegistry for centralized native type lookup
    static Core::TypeRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        registry.registerBuiltinTypes();
        initialized = true;
    }
    return registry.getNativeTypeLLVM(nativeType);
}

LLVMIR::Type* NativeCodegen::getLLVMType(const std::string& xxmlType) const {
    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();

    // Handle NativeType markers
    if (xxmlType.find("NativeType") == 0) {
        // Mangled form: NativeType_type
        if (xxmlType.find("NativeType_") == 0) {
            std::string suffix = xxmlType.substr(11);
            suffix = TypeNormalizer::stripOwnershipMarker(suffix);
            return parseTypeString(suffix, typeCtx);
        }

        // Unmangled form: NativeType<type> or NativeType<"type">
        size_t anglePos = xxmlType.find('<');
        if (anglePos != std::string::npos) {
            size_t endAngle = xxmlType.find('>', anglePos);
            if (endAngle != std::string::npos) {
                std::string nativeType = xxmlType.substr(anglePos + 1, endAngle - anglePos - 1);
                // Remove quotes if present
                if (!nativeType.empty() && nativeType.front() == '"' && nativeType.back() == '"') {
                    nativeType = nativeType.substr(1, nativeType.size() - 2);
                }

                // Map common type aliases to LLVM types
                if (nativeType == "int64") return typeCtx.getInt64Ty();
                if (nativeType == "int32") return typeCtx.getInt32Ty();
                if (nativeType == "int16") return typeCtx.getInt16Ty();
                if (nativeType == "int8") return typeCtx.getInt8Ty();
                if (nativeType == "uint64") return typeCtx.getInt64Ty();
                if (nativeType == "uint32") return typeCtx.getInt32Ty();
                if (nativeType == "uint16") return typeCtx.getInt16Ty();
                if (nativeType == "uint8") return typeCtx.getInt8Ty();
                if (nativeType == "bool") return typeCtx.getInt1Ty();
                if (nativeType == "cstr") return typeCtx.getPtrTy();
                if (nativeType == "string_ptr") return typeCtx.getPtrTy();
                return parseTypeString(nativeType, typeCtx);
            }
        }
    }

    // Raw LLVM native types
    if (xxmlType == "float") return typeCtx.getFloatTy();
    if (xxmlType == "double") return typeCtx.getDoubleTy();
    if (xxmlType == "i1") return typeCtx.getInt1Ty();
    if (xxmlType == "i8") return typeCtx.getInt8Ty();
    if (xxmlType == "i16") return typeCtx.getInt16Ty();
    if (xxmlType == "i32") return typeCtx.getInt32Ty();
    if (xxmlType == "i64") return typeCtx.getInt64Ty();
    if (xxmlType == "ptr") return typeCtx.getPtrTy();

    // None/void
    if (xxmlType == "None" || xxmlType == "void") return typeCtx.getVoidTy();

    // Default to pointer for all other types
    return typeCtx.getPtrTy();
}

LLVMIR::Type* NativeCodegen::parseTypeString(const std::string& typeStr, LLVMIR::TypeContext& typeCtx) const {
    // Parse LLVM type string to Type*
    if (typeStr == "float") return typeCtx.getFloatTy();
    if (typeStr == "double") return typeCtx.getDoubleTy();
    if (typeStr == "i1") return typeCtx.getInt1Ty();
    if (typeStr == "i8") return typeCtx.getInt8Ty();
    if (typeStr == "i16") return typeCtx.getInt16Ty();
    if (typeStr == "i32") return typeCtx.getInt32Ty();
    if (typeStr == "i64") return typeCtx.getInt64Ty();
    if (typeStr == "ptr") return typeCtx.getPtrTy();
    if (typeStr == "void") return typeCtx.getVoidTy();

    // Default to pointer
    return typeCtx.getPtrTy();
}

LLVMIR::FFICallingConv NativeCodegen::mapCallingConvention(Parser::CallingConvention conv) {
    switch (conv) {
        case Parser::CallingConvention::CDecl:
            return LLVMIR::FFICallingConv::CDecl;
        case Parser::CallingConvention::StdCall:
            return LLVMIR::FFICallingConv::StdCall;
        case Parser::CallingConvention::FastCall:
            return LLVMIR::FFICallingConv::FastCall;
        case Parser::CallingConvention::Auto:
        default:
            return LLVMIR::FFICallingConv::CDecl;
    }
}

void NativeCodegen::generateNativeThunk(Parser::MethodDecl& node,
                                        const std::string& className,
                                        const std::string& namespaceName) {
    // Build FFI thunk config
    LLVMIR::FFIThunkConfig config;

    // Set library path and symbol name
    config.libraryPath = node.nativePath;
    config.symbolName = node.nativeSymbol;
    config.callingConv = mapCallingConvention(node.callingConvention);

    // Determine function name
    if (!className.empty()) {
        config.functionName = getQualifiedName(className, node.name);
    } else {
        config.functionName = node.name;
    }

    // Build return type
    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();
    config.returnType = typeCtx.getVoidTy();

    if (node.returnType) {
        std::string typeName = node.returnType->typeName;
        if (typeName.find("NativeType<") == 0) {
            // Try quoted form first: NativeType<"int32">
            size_t start = typeName.find("\"");
            size_t end = typeName.rfind("\"");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                std::string nativeType = typeName.substr(start + 1, end - start - 1);
                std::string llvmTypeStr = mapNativeTypeToLLVMString(nativeType);
                config.returnType = parseTypeString(llvmTypeStr, typeCtx);
            } else {
                // Unquoted form: NativeType<int32>
                size_t angleStart = typeName.find('<');
                size_t angleEnd = typeName.find('>');
                if (angleStart != std::string::npos && angleEnd != std::string::npos && angleEnd > angleStart) {
                    std::string nativeType = typeName.substr(angleStart + 1, angleEnd - angleStart - 1);
                    std::string llvmTypeStr = mapNativeTypeToLLVMString(nativeType);
                    config.returnType = parseTypeString(llvmTypeStr, typeCtx);
                }
            }
        } else {
            config.returnType = getLLVMType(typeName);
        }
    }

    // Build parameter info
    // Native FFI methods are always static - they don't have a 'this' pointer
    // They're declared in a class for organization but called as Class::method()
    config.isInstanceMethod = false;

    for (const auto& param : node.parameters) {
        LLVMIR::FFIParameter ffiParam;
        ffiParam.name = param->name;
        ffiParam.type = typeCtx.getPtrTy();  // Default
        ffiParam.isCallback = false;

        if (param->type) {
            std::string typeName = param->type->typeName;
            if (typeName.find("NativeType<") == 0) {
                // Try quoted form first: NativeType<"int32">
                size_t start = typeName.find("\"");
                size_t end = typeName.rfind("\"");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string nativeType = typeName.substr(start + 1, end - start - 1);
                    std::string llvmTypeStr = mapNativeTypeToLLVMString(nativeType);
                    ffiParam.type = parseTypeString(llvmTypeStr, typeCtx);
                } else {
                    // Unquoted form: NativeType<int32>
                    size_t angleStart = typeName.find('<');
                    size_t angleEnd = typeName.find('>');
                    if (angleStart != std::string::npos && angleEnd != std::string::npos && angleEnd > angleStart) {
                        std::string nativeType = typeName.substr(angleStart + 1, angleEnd - angleStart - 1);
                        std::string llvmTypeStr = mapNativeTypeToLLVMString(nativeType);
                        ffiParam.type = parseTypeString(llvmTypeStr, typeCtx);
                    }
                }
            } else {
                ffiParam.type = getLLVMType(typeName);
            }
        }
        config.parameters.push_back(ffiParam);
    }

    // Generate the FFI thunk using the type-safe builder
    // The thunk is added directly to the Module and will be emitted via LLVMEmitter
    ffiBuilder_->createFFIThunk(config);
}

} // namespace XXML::Backends::Codegen
