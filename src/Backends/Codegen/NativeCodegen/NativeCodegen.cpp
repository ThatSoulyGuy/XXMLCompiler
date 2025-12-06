#include "Backends/Codegen/NativeCodegen/NativeCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/TypeNormalizer.h"
#include "Core/TypeRegistry.h"
#include "Core/CompilationContext.h"

namespace XXML::Backends::Codegen {

NativeCodegen::NativeCodegen(CodegenContext& ctx, Core::CompilationContext* compCtx)
    : ctx_(ctx), compCtx_(compCtx) {
}

void NativeCodegen::reset() {
    output_.str("");
    output_.clear();
    registerCounter_ = 0;
    labelCounter_ = 0;
    stringLiterals_.clear();
}

void NativeCodegen::emitLine(const std::string& line) {
    output_ << line << "\n";
}

std::string NativeCodegen::allocateRegister() {
    return "%r" + std::to_string(registerCounter_++);
}

std::string NativeCodegen::allocateLabel(const std::string& prefix) {
    return prefix + "_" + std::to_string(labelCounter_++);
}

std::string NativeCodegen::getQualifiedName(const std::string& className, const std::string& methodName) const {
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
    return mangledClassName + "_" + methodName;
}

std::string NativeCodegen::mapNativeTypeToLLVM(const std::string& nativeType) {
    // Use TypeRegistry for centralized native type lookup
    static Core::TypeRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        registry.registerBuiltinTypes();
        initialized = true;
    }
    return registry.getNativeTypeLLVM(nativeType);
}

std::string NativeCodegen::getLLVMCallingConvention(Parser::CallingConvention conv) {
    switch (conv) {
        case Parser::CallingConvention::CDecl:
            return "ccc";
        case Parser::CallingConvention::StdCall:
            return "x86_stdcallcc";
        case Parser::CallingConvention::FastCall:
            return "x86_fastcallcc";
        case Parser::CallingConvention::Auto:
        default:
            return "ccc";  // Default to C calling convention
    }
}

std::string NativeCodegen::getDefaultReturnValue(const std::string& llvmType) {
    if (llvmType == "void") {
        return "";
    } else if (llvmType == "ptr") {
        return "null";
    } else if (llvmType == "i64" || llvmType == "i32" || llvmType == "i16" || llvmType == "i8") {
        return "0";
    } else if (llvmType == "float") {
        return "0.0";
    } else if (llvmType == "double") {
        return "0.0";
    } else {
        return "zeroinitializer";
    }
}

std::string NativeCodegen::getLLVMType(const std::string& xxmlType) const {
    // Handle NativeType markers
    if (xxmlType.find("NativeType") == 0) {
        // Mangled form: NativeType_type
        if (xxmlType.find("NativeType_") == 0) {
            std::string suffix = xxmlType.substr(11);
            suffix = TypeNormalizer::stripOwnershipMarker(suffix);
            return suffix;
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
                if (nativeType == "int64") return "i64";
                if (nativeType == "int32") return "i32";
                if (nativeType == "int16") return "i16";
                if (nativeType == "int8") return "i8";
                if (nativeType == "uint64") return "i64";
                if (nativeType == "uint32") return "i32";
                if (nativeType == "uint16") return "i16";
                if (nativeType == "uint8") return "i8";
                if (nativeType == "bool") return "i1";
                if (nativeType == "cstr") return "ptr";
                if (nativeType == "string_ptr") return "ptr";
                return nativeType;
            }
        }
    }

    // Raw LLVM native types
    if (xxmlType == "float") return "float";
    if (xxmlType == "double") return "double";
    if (xxmlType == "i1") return "i1";
    if (xxmlType == "i8") return "i8";
    if (xxmlType == "i16") return "i16";
    if (xxmlType == "i32") return "i32";
    if (xxmlType == "i64") return "i64";
    if (xxmlType == "ptr") return "ptr";

    // None/void
    if (xxmlType == "None" || xxmlType == "void") return "void";

    // Default to pointer for all other types
    return "ptr";
}

void NativeCodegen::generateNativeThunk(Parser::MethodDecl& node,
                                        const std::string& className,
                                        const std::string& namespaceName) {
    std::string dllPath = node.nativePath;
    std::string symbolName = node.nativeSymbol;
    Parser::CallingConvention convention = node.callingConvention;

    // Determine function name
    std::string funcName;
    if (!className.empty()) {
        funcName = getQualifiedName(className, node.name);
    } else {
        funcName = node.name;
    }

    // Build return type
    std::string returnLLVMType = "void";
    std::string nativeReturnType = "void";
    if (node.returnType) {
        std::string typeName = node.returnType->typeName;
        if (typeName.find("NativeType<") == 0) {
            // Try quoted form first: NativeType<"int32">
            size_t start = typeName.find("\"");
            size_t end = typeName.rfind("\"");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                nativeReturnType = typeName.substr(start + 1, end - start - 1);
                returnLLVMType = mapNativeTypeToLLVM(nativeReturnType);
            } else {
                // Unquoted form: NativeType<int32>
                size_t angleStart = typeName.find('<');
                size_t angleEnd = typeName.find('>');
                if (angleStart != std::string::npos && angleEnd != std::string::npos && angleEnd > angleStart) {
                    nativeReturnType = typeName.substr(angleStart + 1, angleEnd - angleStart - 1);
                    returnLLVMType = mapNativeTypeToLLVM(nativeReturnType);
                }
            }
        } else {
            returnLLVMType = getLLVMType(typeName);
        }
    }

    // Build parameter info
    bool isInstanceMethod = !className.empty();
    std::vector<std::pair<std::string, std::string>> params;  // name, llvm type

    if (isInstanceMethod) {
        params.push_back({"this", "ptr"});
    }

    for (const auto& param : node.parameters) {
        std::string paramType = "ptr";
        if (param->type) {
            std::string typeName = param->type->typeName;
            if (typeName.find("NativeType<") == 0) {
                // Try quoted form first: NativeType<"int32">
                size_t start = typeName.find("\"");
                size_t end = typeName.rfind("\"");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string nativeType = typeName.substr(start + 1, end - start - 1);
                    paramType = mapNativeTypeToLLVM(nativeType);
                } else {
                    // Unquoted form: NativeType<int32>
                    size_t angleStart = typeName.find('<');
                    size_t angleEnd = typeName.find('>');
                    if (angleStart != std::string::npos && angleEnd != std::string::npos && angleEnd > angleStart) {
                        std::string nativeType = typeName.substr(angleStart + 1, angleEnd - angleStart - 1);
                        paramType = mapNativeTypeToLLVM(nativeType);
                    }
                }
            } else {
                paramType = getLLVMType(typeName);
            }
        }
        params.push_back({param->name, paramType});
    }

    // Build parameter list for function signature
    std::string paramList;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) paramList += ", ";
        paramList += params[i].second + " %" + std::to_string(i);
    }

    // Emit function header with internal linkage
    emitLine("");
    emitLine("; Native FFI thunk for " + symbolName + " from " + dllPath);
    emitLine("define internal " + returnLLVMType + " @" + funcName + "(" + paramList + ") {");
    emitLine("entry:");

    // === Step 1: Create global strings for DLL path and symbol name ===
    std::string dllPathLabel = "ffi.path." + std::to_string(stringLiterals_.size());
    stringLiterals_.push_back({dllPathLabel, dllPath});

    std::string symbolLabel = "ffi.sym." + std::to_string(stringLiterals_.size());
    stringLiterals_.push_back({symbolLabel, symbolName});

    // Load library
    std::string dllHandleReg = allocateRegister();
    emitLine("  " + dllHandleReg + " = call ptr @xxml_FFI_loadLibrary(ptr @." + dllPathLabel + ")");

    // Check if handle is null
    std::string isNullReg = allocateRegister();
    emitLine("  " + isNullReg + " = icmp eq ptr " + dllHandleReg + ", null");

    std::string errorLabel = allocateLabel("ffi.error");
    std::string continueLabel = allocateLabel("ffi.continue");
    emitLine("  br i1 " + isNullReg + ", label %" + errorLabel + ", label %" + continueLabel);

    // Error block - DLL load failed
    emitLine(errorLabel + ":");
    if (returnLLVMType == "void") {
        emitLine("  ret void");
    } else {
        emitLine("  ret " + returnLLVMType + " " + getDefaultReturnValue(returnLLVMType));
    }

    // Continue block - DLL loaded successfully
    emitLine(continueLabel + ":");

    // === Step 2: Get the symbol ===
    std::string symbolPtrReg = allocateRegister();
    emitLine("  " + symbolPtrReg + " = call ptr @xxml_FFI_getSymbol(ptr " + dllHandleReg + ", ptr @." + symbolLabel + ")");

    // Check if symbol is null
    std::string isSymNullReg = allocateRegister();
    emitLine("  " + isSymNullReg + " = icmp eq ptr " + symbolPtrReg + ", null");

    std::string symErrorLabel = allocateLabel("ffi.sym_error");
    std::string callLabel = allocateLabel("ffi.call");
    emitLine("  br i1 " + isSymNullReg + ", label %" + symErrorLabel + ", label %" + callLabel);

    // Symbol error block
    emitLine(symErrorLabel + ":");
    emitLine("  call void @xxml_FFI_freeLibrary(ptr " + dllHandleReg + ")");
    if (returnLLVMType == "void") {
        emitLine("  ret void");
    } else {
        emitLine("  ret " + returnLLVMType + " " + getDefaultReturnValue(returnLLVMType));
    }

    // Call block - ready to call the native function
    emitLine(callLabel + ":");

    // === Step 3: Build function type and call ===
    std::string ccAttr = getLLVMCallingConvention(convention);

    // Build argument list for the call (skip 'this' for instance methods)
    std::string argList;
    size_t startIdx = isInstanceMethod ? 1 : 0;
    for (size_t i = startIdx; i < params.size(); ++i) {
        if (i > startIdx) argList += ", ";
        argList += params[i].second + " %" + std::to_string(i);
    }

    // Generate the indirect call
    std::string resultReg;
    if (returnLLVMType != "void") {
        resultReg = allocateRegister();
        emitLine("  " + resultReg + " = call " + ccAttr + " " + returnLLVMType + " " +
                 symbolPtrReg + "(" + argList + ")");
    } else {
        emitLine("  call " + ccAttr + " void " + symbolPtrReg + "(" + argList + ")");
    }

    // Return
    if (returnLLVMType == "void") {
        emitLine("  ret void");
    } else {
        emitLine("  ret " + returnLLVMType + " " + resultReg);
    }

    emitLine("}");
    emitLine("");
}

} // namespace XXML::Backends::Codegen
