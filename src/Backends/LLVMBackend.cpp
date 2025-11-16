#include "Backends/LLVMBackend.h"
#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/OperatorRegistry.h"
#include "Core/FormatCompat.h"  // Compatibility layer for std::format
#include "Utils/ProcessUtils.h"
#include "Semantic/SemanticAnalyzer.h"  // For template instantiation
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>  // For std::replace

#ifdef _WIN32
#include <windows.h>  // For GetShortPathNameA
#endif

namespace XXML::Backends {

LLVMBackend::LLVMBackend(Core::CompilationContext* context)
    : BackendBase() {
    context_ = context;

    // Set capabilities
    addCapability(Core::BackendCapability::Optimizations);
    addCapability(Core::BackendCapability::ValueSemantics);
}

bool LLVMBackend::supportsFeature(std::string_view feature) const {
    return feature == "optimizations" ||
           feature == "jit" ||
           feature == "multi_target";
}

void LLVMBackend::initialize(Core::CompilationContext& context) {
    context_ = &context;
    reset();
}

std::string LLVMBackend::generate(Parser::Program& program) {
    output_.str("");
    output_.clear();
    registerCounter_ = 0;
    labelCounter_ = 0;
    stringLiterals_.clear();  // Clear string literals from previous runs

    // Generate preamble
    std::string preamble = generatePreamble();

    // Generate template instantiations (monomorphization) before main code
    generateTemplateInstantiations();

    // Visit program (this collects string literals)
    program.accept(*this);

    // Get the generated code
    std::string generatedCode = output_.str();

    // Build final output with string literals inserted after preamble
    std::stringstream finalOutput;
    finalOutput << preamble;

    // Emit global string constants if any were collected
    if (!stringLiterals_.empty()) {
        finalOutput << "; ============================================\n";
        finalOutput << "; String Literal Constants\n";
        finalOutput << "; ============================================\n";
        for (const auto& [label, content] : stringLiterals_) {
            // Escape the string content for LLVM IR
            std::string escapedContent = content;
            // TODO: Proper escaping of special characters
            size_t length = escapedContent.length() + 1;  // +1 for null terminator
            finalOutput << "@." << label << " = private unnamed_addr constant ["
                       << length << " x i8] c\"" << escapedContent << "\\00\"\n";
        }
        finalOutput << "\n";
    }

    finalOutput << generatedCode;

    return finalOutput.str();
}

void LLVMBackend::generateTemplateInstantiations() {
    if (!semanticAnalyzer_) {
        return; // No semantic analyzer, skip template generation
    }

    const auto& instantiations = semanticAnalyzer_->getTemplateInstantiations();
    const auto& templateClasses = semanticAnalyzer_->getTemplateClasses();

    for (const auto& inst : instantiations) {
        auto it = templateClasses.find(inst.templateName);
        if (it == templateClasses.end()) {
            continue; // Template class not found, skip
        }

        Parser::ClassDecl* templateClass = it->second;

        // Generate mangled class name for LLVM
        // Replace < > and , with underscores: SomeClass<Integer> -> SomeClass_Integer
        std::string mangledName = inst.templateName;
        size_t valueIndex = 0;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                // Replace namespace separators with underscores
                std::string cleanType = arg.typeArg;
                size_t pos = 0;
                while ((pos = cleanType.find("::")) != std::string::npos) {
                    cleanType.replace(pos, 2, "_");
                }
                mangledName += "_" + cleanType;
            } else {
                // Use evaluated value for non-type parameters
                if (valueIndex < inst.evaluatedValues.size()) {
                    mangledName += "_" + std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Build type substitution map (template params -> concrete types/values)
        std::unordered_map<std::string, std::string> typeMap;
        valueIndex = 0;
        for (size_t i = 0; i < templateClass->templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = templateClass->templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                // Non-type parameter - substitute with the evaluated constant
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone the template class and substitute types
        auto instantiated = cloneAndSubstituteClassDecl(templateClass, mangledName, typeMap);

        // Generate code for the instantiated class
        if (instantiated) {
            instantiated->accept(*this);
        }
    }
}

std::string LLVMBackend::generateHeader(Parser::Program& program) {
    // LLVM IR doesn't separate headers and implementation
    return generate(program);
}

std::string LLVMBackend::generateImplementation(Parser::Program& program) {
    // LLVM IR doesn't separate headers and implementation
    return generate(program);
}

std::string LLVMBackend::getTargetTriple() const {
    switch (targetPlatform_) {
        case TargetPlatform::X86_64_Windows:
            return "x86_64-pc-windows-msvc";
        case TargetPlatform::X86_64_Linux:
            return "x86_64-unknown-linux-gnu";
        case TargetPlatform::X86_64_MacOS:
            return "x86_64-apple-darwin";
        case TargetPlatform::ARM64_Linux:
            return "aarch64-unknown-linux-gnu";
        case TargetPlatform::ARM64_MacOS:
            return "arm64-apple-darwin";
        case TargetPlatform::WebAssembly:
            return "wasm32-unknown-unknown";
        case TargetPlatform::Native:
        default:
            // Detect host platform
#ifdef _WIN32
            return "x86_64-pc-windows-msvc";
#elif defined(__APPLE__)
    #if defined(__aarch64__) || defined(__arm64__)
            return "arm64-apple-darwin";
    #else
            return "x86_64-apple-darwin";
    #endif
#elif defined(__linux__)
    #if defined(__aarch64__) || defined(__arm64__)
            return "aarch64-unknown-linux-gnu";
    #else
            return "x86_64-unknown-linux-gnu";
    #endif
#else
            return "x86_64-unknown-unknown";
#endif
    }
}

std::string LLVMBackend::getDataLayout() const {
    switch (targetPlatform_) {
        case TargetPlatform::X86_64_Windows:
        case TargetPlatform::X86_64_Linux:
        case TargetPlatform::X86_64_MacOS:
            // x86-64 data layout (64-bit pointers, little-endian)
            return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
        case TargetPlatform::ARM64_Linux:
        case TargetPlatform::ARM64_MacOS:
            // ARM64/AArch64 data layout (64-bit pointers, little-endian)
            return "e-m:o-i64:64-i128:128-n32:64-S128";
        case TargetPlatform::WebAssembly:
            // WebAssembly data layout (32-bit pointers)
            return "e-m:e-p:32:32-i64:64-n32:64-S128";
        case TargetPlatform::Native:
        default:
            // Default to x86-64
            return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
    }
}

std::string LLVMBackend::generatePreamble() {
    std::stringstream preamble;

    preamble << "; Generated by XXML Compiler v2.0 (LLVM IR Backend)\n";
    preamble << "; Target: LLVM IR 17.0\n";
    preamble << ";\n\n";

    // Add target triple and data layout
    preamble << "target triple = \"" << getTargetTriple() << "\"\n";
    preamble << "target datalayout = \"" << getDataLayout() << "\"\n\n";

    // Declare LLVM types for XXML built-in types
    preamble << "; Built-in type definitions\n";
    preamble << "%Integer = type { i64 }\n";
    preamble << "%String = type { ptr, i64 }\n";
    preamble << "%Bool = type { i1 }\n";
    preamble << "%Float = type { float }\n";
    preamble << "%Double = type { double }\n";
    preamble << "\n";

    // Declare XXML LLVM Runtime library functions
    preamble << "; ============================================\n";
    preamble << "; XXML LLVM Runtime Library\n";
    preamble << "; ============================================\n\n";

    // Memory Management
    preamble << "; Memory Management\n";
    preamble << "declare ptr @xxml_malloc(i64)\n";
    preamble << "declare void @xxml_free(ptr)\n";
    preamble << "declare ptr @xxml_memcpy(ptr, ptr, i64)\n";
    preamble << "declare ptr @xxml_memset(ptr, i32, i64)\n";
    preamble << "declare ptr @xxml_ptr_read(ptr)\n";
    preamble << "declare void @xxml_ptr_write(ptr, ptr)\n";
    preamble << "declare i8 @xxml_read_byte(ptr)\n";
    preamble << "declare void @xxml_write_byte(ptr, i8)\n";
    preamble << "\n";

    // Integer Operations
    preamble << "; Integer Operations\n";
    preamble << "declare ptr @Integer_Constructor(i64)\n";
    preamble << "declare i64 @Integer_getValue(ptr)\n";
    preamble << "declare ptr @Integer_add(ptr, ptr)\n";
    preamble << "declare ptr @Integer_sub(ptr, ptr)\n";
    preamble << "declare ptr @Integer_mul(ptr, ptr)\n";
    preamble << "declare ptr @Integer_div(ptr, ptr)\n";
    preamble << "declare i1 @Integer_eq(ptr, ptr)\n";
    preamble << "declare i1 @Integer_ne(ptr, ptr)\n";
    preamble << "declare i1 @Integer_lt(ptr, ptr)\n";
    preamble << "declare i1 @Integer_le(ptr, ptr)\n";
    preamble << "declare i1 @Integer_gt(ptr, ptr)\n";
    preamble << "declare i1 @Integer_ge(ptr, ptr)\n";
    preamble << "declare i64 @Integer_toInt64(ptr)\n";
    preamble << "declare ptr @Integer_toString(ptr)\n";
    preamble << "\n";

    // String Operations
    preamble << "; String Operations\n";
    preamble << "declare ptr @String_Constructor(ptr)\n";
    preamble << "declare ptr @String_FromCString(ptr)\n";
    preamble << "declare ptr @String_toCString(ptr)\n";
    preamble << "declare i64 @String_length(ptr)\n";
    preamble << "declare ptr @String_concat(ptr, ptr)\n";
    preamble << "declare ptr @String_append(ptr, ptr)\n";
    preamble << "declare i1 @String_equals(ptr, ptr)\n";
    preamble << "declare void @String_destroy(ptr)\n";
    preamble << "\n";

    // Bool Operations
    preamble << "; Bool Operations\n";
    preamble << "declare ptr @Bool_Constructor(i1)\n";
    preamble << "declare i1 @Bool_getValue(ptr)\n";
    preamble << "declare ptr @Bool_and(ptr, ptr)\n";
    preamble << "declare ptr @Bool_or(ptr, ptr)\n";
    preamble << "declare ptr @Bool_not(ptr)\n";
    preamble << "\n";

    // None Operations (for void returns)
    preamble << "; None Operations\n";
    preamble << "declare ptr @None_Constructor()\n";
    preamble << "\n";

    // List Operations
    preamble << "; List Operations\n";
    preamble << "declare ptr @List_Constructor()\n";
    preamble << "declare void @List_add(ptr, ptr)\n";
    preamble << "declare ptr @List_get(ptr, i64)\n";
    preamble << "declare i64 @List_size(ptr)\n";
    preamble << "\n";

    // Console I/O
    preamble << "; Console I/O\n";
    preamble << "declare void @Console_print(ptr)\n";
    preamble << "declare void @Console_printLine(ptr)\n";
    preamble << "declare void @Console_printInt(i64)\n";
    preamble << "declare void @Console_printBool(i1)\n";
    preamble << "\n";

    // System Functions
    preamble << "; System Functions\n";
    preamble << "declare void @xxml_exit(i32)\n";
    preamble << "declare void @exit(i32)\n";
    preamble << "\n";

    // Optimization attributes
    preamble << "; Optimization attributes\n";
    preamble << "attributes #0 = { noinline nounwind optnone uwtable }\n";
    preamble << "attributes #1 = { nounwind uwtable }\n";
    preamble << "attributes #2 = { alwaysinline nounwind uwtable }\n";
    preamble << "\n";

    return preamble.str();
}

std::vector<std::string> LLVMBackend::getRequiredIncludes() const {
    return {};  // LLVM IR doesn't use includes
}

std::vector<std::string> LLVMBackend::getRequiredLibraries() const {
    return {"LLVM"};
}

std::string LLVMBackend::convertType(std::string_view xxmlType) const {
    return getLLVMType(std::string(xxmlType));
}

std::string LLVMBackend::convertOwnership(std::string_view type,
                                         std::string_view ownershipIndicator) const {
    // In LLVM IR, all objects are pointers by default
    // Ownership is handled by the runtime
    if (ownershipIndicator == "&") {
        return std::format("ptr");  // Reference
    } else if (ownershipIndicator == "%") {
        return getLLVMType(std::string(type));  // Value
    } else {
        return std::format("ptr");  // Owned (pointer)
    }
}

std::string LLVMBackend::allocateRegister() {
    return "%r" + std::to_string(registerCounter_++);
}

std::string LLVMBackend::allocateLabel(std::string_view prefix) {
    return std::format("{}_{}", prefix, labelCounter_++);
}

void LLVMBackend::emitLine(const std::string& line) {
    output_ << getIndent() << line << "\n";
}

std::string LLVMBackend::getLLVMType(const std::string& xxmlType) const {
    // Debug: output what we're trying to convert
    std::cerr << "DEBUG getLLVMType: input type = '" << xxmlType << "'" << std::endl;

    // Handle ptr_to<T> marker (from alloca results)
    if (xxmlType.find("ptr_to<") == 0) {
        return "ptr";  // alloca always returns ptr
    }

    // IMPORTANT: In XXML's LLVM backend, ALL objects are heap-allocated
    // Therefore, all user-defined types (Integer, String, etc.) are represented as ptr
    // Only use primitive LLVM types for special cases

    if (!context_) {
        // Fallback without context
        // All XXML objects are pointers (heap-allocated)
        if (xxmlType == "Integer") return "ptr";  // Changed from i64
        if (xxmlType == "Bool") return "ptr";     // Changed from i1
        if (xxmlType == "Float") return "ptr";    // Changed from float
        if (xxmlType == "Double") return "ptr";   // Changed from double
        if (xxmlType == "String") return "ptr";
        if (xxmlType == "None" || xxmlType == "void") return "void";
        return "ptr";
    }

    // Special case: Check for NativeType (handles both mangled and unmangled forms)
    // NativeType<"ptr"> -> ptr
    // NativeType_ptr -> ptr
    if (xxmlType.find("NativeType") == 0) {
        // Check for mangled form first: NativeType_type (e.g., NativeType_ptr, NativeType_int64)
        if (xxmlType.find("NativeType_") == 0) {
            std::string suffix = xxmlType.substr(11);  // Skip "NativeType_"
            // Remove trailing "^" if present (ownership marker)
            if (!suffix.empty() && suffix.back() == '^') {
                suffix = suffix.substr(0, suffix.size() - 1);
            }
            return suffix;  // Return "ptr", "int64", etc.
        }

        // Check for unmangled form: NativeType<type> or NativeType<"type">
        size_t anglePos = xxmlType.find('<');
        if (anglePos != std::string::npos) {
            size_t endAngle = xxmlType.find('>', anglePos);
            if (endAngle != std::string::npos) {
                std::string nativeType = xxmlType.substr(anglePos + 1, endAngle - anglePos - 1);
                // Remove quotes if present (handles both "ptr" and ptr)
                if (!nativeType.empty() && nativeType.front() == '"' && nativeType.back() == '"') {
                    nativeType = nativeType.substr(1, nativeType.size() - 2);
                }

                // Map common type aliases to LLVM types
                if (nativeType == "int64") nativeType = "i64";
                else if (nativeType == "int32") nativeType = "i32";
                else if (nativeType == "int16") nativeType = "i16";
                else if (nativeType == "int8") nativeType = "i8";
                else if (nativeType == "uint64") nativeType = "i64";
                else if (nativeType == "uint32") nativeType = "i32";
                else if (nativeType == "uint16") nativeType = "i16";
                else if (nativeType == "uint8") nativeType = "i8";

                std::cerr << "DEBUG: Mapped to LLVM type = '" << nativeType << "'" << std::endl;
                return nativeType;  // Return the LLVM type (e.g., "ptr", "i64", "i32")
            }
        }
    }

    // Special case: None/void types must return void, not ptr
    if (xxmlType == "None" || xxmlType == "void") {
        return "void";
    }

    // Check for template types (containing '<')
    if (xxmlType.find('<') != std::string::npos) {
        // Extract base name and template args
        size_t anglePos = xxmlType.find('<');
        std::string baseName = xxmlType.substr(0, anglePos);

        // For regular template types, return a mangled struct name
        // The actual struct will be generated when we visit the ClassDecl
        std::string cleanName = xxmlType;
        // Replace problematic characters for LLVM
        size_t pos = 0;
        while ((pos = cleanName.find("::")) != std::string::npos) {
            cleanName.replace(pos, 2, "_");
        }
        while ((pos = cleanName.find("<")) != std::string::npos) {
            cleanName.replace(pos, 1, "_");
        }
        while ((pos = cleanName.find(">")) != std::string::npos) {
            cleanName.erase(pos, 1);
        }
        while ((pos = cleanName.find(",")) != std::string::npos) {
            cleanName.replace(pos, 1, "_");
        }

        return "%class." + cleanName;
    }

    // ✅ USE TYPE REGISTRY for LLVM type mapping
    const auto* typeInfo = context_->types().getTypeInfo(xxmlType);
    if (typeInfo && !typeInfo->llvmType.empty()) {
        // Even if registry has a specific type, in LLVM backend all objects are heap-allocated
        // So we override with ptr for object types
        std::string registryType = typeInfo->llvmType;

        // Only use non-ptr types for truly primitive operations (if needed)
        // For now, always use ptr for consistency
        return "ptr";
    }

    // Default to pointer for user-defined types
    return "ptr";
}

std::string LLVMBackend::generateBinaryOp(const std::string& op,
                                         const std::string& lhs,
                                         const std::string& rhs,
                                         const std::string& type) {
    // ✅ USE OPERATOR REGISTRY for LLVM generation
    if (context_) {
        return context_->operators().generateBinaryLLVM(op, lhs, rhs);
    }

    // Fallback: basic LLVM operations
    if (op == "+") return std::format("add {} {}, {}", type, lhs, rhs);
    if (op == "-") return std::format("sub {} {}, {}", type, lhs, rhs);
    if (op == "*") return std::format("mul {} {}, {}", type, lhs, rhs);
    if (op == "/") return std::format("sdiv {} {}, {}", type, lhs, rhs);
    if (op == "==") return std::format("icmp eq {} {}, {}", type, lhs, rhs);
    if (op == "!=") return std::format("icmp ne {} {}, {}", type, lhs, rhs);
    if (op == "<") return std::format("icmp slt {} {}, {}", type, lhs, rhs);
    if (op == ">") return std::format("icmp sgt {} {}, {}", type, lhs, rhs);

    return std::format("; Unknown op: {} {} {}", lhs, op, rhs);
}

std::string LLVMBackend::getQualifiedName(const std::string& className, const std::string& methodName) const {
    // Replace :: with _ for valid LLVM identifiers
    std::string mangledClassName = className;
    size_t pos = 0;
    while ((pos = mangledClassName.find("::")) != std::string::npos) {
        mangledClassName.replace(pos, 2, "_");
    }
    return mangledClassName + "_" + methodName;
}

// Template helper methods
std::string LLVMBackend::mangleTemplateName(const std::string& baseName,
                                            const std::vector<std::string>& typeArgs) const {
    if (typeArgs.empty()) {
        return baseName;
    }

    std::string mangled = baseName;
    for (const auto& arg : typeArgs) {
        // Replace problematic characters in type names
        std::string cleanArg = arg;
        // Remove namespace separators
        size_t pos = 0;
        while ((pos = cleanArg.find("::")) != std::string::npos) {
            cleanArg.replace(pos, 2, "_");
        }
        // Remove angle brackets from nested templates
        while ((pos = cleanArg.find("<")) != std::string::npos) {
            cleanArg.erase(pos, 1);
        }
        while ((pos = cleanArg.find(">")) != std::string::npos) {
            cleanArg.erase(pos, 1);
        }

        mangled += "_" + cleanArg;
    }

    return mangled;
}

std::string LLVMBackend::getFullTypeName(const Parser::TypeRef& typeRef) const {
    std::string fullName = typeRef.typeName;

    if (!typeRef.templateArgs.empty()) {
        fullName += "<";
        for (size_t i = 0; i < typeRef.templateArgs.size(); ++i) {
            if (i > 0) fullName += ",";

            const auto& arg = typeRef.templateArgs[i];
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                fullName += arg.typeArg;  // typeArg is already a string
            } else if (arg.kind == Parser::TemplateArgument::Kind::Value) {
                // For value arguments, we'd need to evaluate them - for now just use a placeholder
                fullName += "VALUE";
            }
        }
        fullName += ">";
    }

    return fullName;
}

std::string LLVMBackend::extractTypeName(const Parser::Expression* expr) const {
    // Extract the type name from an expression (used for static method calls)
    // Examples:
    //   IdentifierExpr("Integer") -> "Integer"
    //   MemberAccessExpr(IdentifierExpr("MyNamespace"), "::MyClass") -> "MyNamespace::MyClass"
    //   MemberAccessExpr(MemberAccessExpr(...), "::SomeClass<T>") -> "MyNamespace::SomeClass<T>"

    if (auto* ident = dynamic_cast<const Parser::IdentifierExpr*>(expr)) {
        return ident->name;
    } else if (auto* memberAccess = dynamic_cast<const Parser::MemberAccessExpr*>(expr)) {
        // Recursively build the qualified name
        std::string objectName = extractTypeName(memberAccess->object.get());

        // Strip :: prefix from member if present (parser includes it)
        std::string memberName = memberAccess->member;
        if (memberName.length() >= 2 && memberName.substr(0, 2) == "::") {
            memberName = memberName.substr(2);
        }

        if (objectName.empty()) {
            return memberName;
        }
        return objectName + "::" + memberName;
    }
    return "";
}

// Ownership helper methods
LLVMBackend::OwnershipKind LLVMBackend::parseOwnership(char ownershipChar) const {
    switch (ownershipChar) {
        case '^': return OwnershipKind::Owned;
        case '&': return OwnershipKind::Reference;
        case '%': return OwnershipKind::Value;
        default:  return OwnershipKind::Owned;  // Default to owned
    }
}

void LLVMBackend::registerVariable(const std::string& name, const std::string& type,
                                   const std::string& llvmReg, OwnershipKind ownership) {
    std::string llvmType = getLLVMType(type);
    variables_[name] = VariableInfo{llvmReg, type, llvmType, ownership, false};
    valueMap_[name] = llvmReg;  // Keep valueMap for backwards compatibility
}

bool LLVMBackend::checkAndMarkMoved(const std::string& varName) {
    auto it = variables_.find(varName);
    if (it == variables_.end()) {
        return false;  // Variable not found
    }

    if (it->second.isMovedFrom) {
        emitLine("; ERROR: Use after move of variable: " + varName);
        return false;
    }

    // For owned values, mark as moved
    if (it->second.ownership == OwnershipKind::Owned) {
        it->second.isMovedFrom = true;
    }

    return true;
}

void LLVMBackend::emitDestructor(const std::string& varName) {
    auto it = variables_.find(varName);
    if (it == variables_.end()) {
        return;  // Variable not found
    }

    // IMPORTANT: Do NOT free stack-allocated variables (created with alloca)
    // Stack variables are automatically freed when they go out of scope
    // Only heap-allocated objects need manual cleanup via xxml_free
    //
    // In XXML, all local variables created with "Instantiate" are stack-allocated
    // and should NEVER be freed manually. Calling free() on stack memory causes
    // undefined behavior and will crash the program.
    //
    // TODO: Implement proper destructor support for heap-allocated objects
    // For now, we disable automatic destruction to avoid freeing stack memory

    // Only emit destructor for owned values that haven't been moved
    // if (it->second.ownership == OwnershipKind::Owned && !it->second.isMovedFrom) {
    //     // Call the type's destructor if it has one
    //     // For now, we'll call xxml_free for pointer types
    //     if (it->second.type != "Integer" && it->second.type != "Bool") {
    //         emitLine("; Destroy " + varName);
    //         emitLine("call void @xxml_free(ptr " + it->second.llvmRegister + ")");
    //     }
    // }
}

// Visitor implementations (simplified skeletons)
void LLVMBackend::visit(Parser::Program& node) {
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }
}

void LLVMBackend::visit(Parser::ImportDecl& node) {
    emitLine(std::format("; import {}", node.modulePath));
}

void LLVMBackend::visit(Parser::NamespaceDecl& node) {
    emitLine(std::format("; namespace {}", node.name));

    // Save previous namespace and set current
    std::string previousNamespace = currentNamespace_;
    if (currentNamespace_.empty()) {
        currentNamespace_ = node.name;
    } else {
        currentNamespace_ = currentNamespace_ + "::" + node.name;
    }

    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }

    // Restore previous namespace
    currentNamespace_ = previousNamespace;
}

void LLVMBackend::visit(Parser::ClassDecl& node) {
    // Skip template declarations (only generate code for instantiations)
    // Template declarations have templateParams but no '<' in name (e.g., "SomeClass<T>")
    // Template instantiations have '<' in name (e.g., "SomeClass<Integer>")
    if (!node.templateParams.empty() && node.name.find('<') == std::string::npos) {
        return;  // This is a template declaration, skip it - monomorphization handles it
    }

    // Check if this is a template instantiation (avoid duplicate generation)
    if (node.name.find('<') != std::string::npos) {
        std::string mangledName = node.name;
        size_t pos = 0;
        while ((pos = mangledName.find("::")) != std::string::npos) {
            mangledName.replace(pos, 2, "_");
        }
        while ((pos = mangledName.find("<")) != std::string::npos) {
            mangledName.replace(pos, 1, "_");
        }
        while ((pos = mangledName.find(">")) != std::string::npos) {
            mangledName.erase(pos, 1);
        }
        while ((pos = mangledName.find(",")) != std::string::npos) {
            mangledName.replace(pos, 1, "_");
        }

        // Skip if already generated
        if (generatedTemplateInstantiations_.find(mangledName) != generatedTemplateInstantiations_.end()) {
            return;
        }
        generatedTemplateInstantiations_.insert(mangledName);
    }

    emitLine("; ============================================");
    emitLine("; Class: " + node.name);
    if (!node.templateParams.empty()) {
        emitLine("; Template parameters: " + std::to_string(node.templateParams.size()));
    }
    emitLine("; ============================================");

    // Set current class context (include namespace if present)
    if (!currentNamespace_.empty()) {
        currentClassName_ = currentNamespace_ + "::" + node.name;
    } else {
        currentClassName_ = node.name;
    }

    // Create class info entry
    ClassInfo& classInfo = classes_[node.name];
    classInfo.properties.clear();

    // First pass: collect properties from all access sections
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                std::string propType = getLLVMType(prop->type->typeName);
                classInfo.properties.push_back({prop->name, propType});
            }
        }
    }

    // Generate struct type definition
    std::stringstream structDef;
    // Mangle class name to remove :: (invalid in LLVM type names)
    std::string mangledClassName = node.name;
    size_t pos = 0;
    while ((pos = mangledClassName.find("::")) != std::string::npos) {
        mangledClassName.replace(pos, 2, "_");
    }
    structDef << "%class." << mangledClassName << " = type { ";
    for (size_t i = 0; i < classInfo.properties.size(); ++i) {
        if (i > 0) structDef << ", ";
        structDef << classInfo.properties[i].second;
    }
    if (classInfo.properties.empty()) {
        structDef << "i8";  // Empty struct needs at least one field in LLVM
    }
    structDef << " }";
    emitLine(structDef.str());
    emitLine("");

    // Second pass: generate methods and constructors
    bool hasConstructor = false;
    for (auto& section : node.sections) {
        // Check if this section has a constructor
        for (const auto& decl : section->declarations) {
            if (dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                hasConstructor = true;
                break;
            }
        }
        section->accept(*this);
    }

    // If no constructor was defined, generate a default one
    if (!hasConstructor) {
        std::string funcName = getQualifiedName(currentClassName_, "Constructor");
        emitLine("; Default constructor for " + currentClassName_);
        emitLine("define ptr @" + funcName + "(ptr %this) #1 {");
        indent();
        emitLine("ret ptr %this");
        dedent();
        emitLine("}");
        emitLine("");
    }

    // Clear current class context
    currentClassName_ = "";
    emitLine("");
}

void LLVMBackend::visit(Parser::AccessSection& node) {
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }
}

void LLVMBackend::visit(Parser::PropertyDecl& node) {
    // Properties are already collected in ClassDecl
    // Nothing to emit here
}

void LLVMBackend::visit(Parser::ConstructorDecl& node) {
    if (currentClassName_.empty()) {
        emitLine("; ERROR: Constructor outside of class");
        return;
    }

    // Generate qualified function name: ClassName_Constructor
    std::string funcName = getQualifiedName(currentClassName_, "Constructor");

    // Build parameter list (including implicit 'this' pointer)
    std::stringstream params;
    params << "ptr %this";  // First parameter is always 'this'

    for (size_t i = 0; i < node.parameters.size(); ++i) {
        params << ", ";
        auto& param = node.parameters[i];
        std::string paramType = getLLVMType(param->type->typeName);
        params << paramType << " %" << param->name;

        // Store parameter in value map and track its type
        valueMap_[param->name] = "%" + param->name;
        registerTypes_["%" + param->name] = param->type->typeName;
    }

    // Store 'this' in value map
    valueMap_["this"] = "%this";

    // Constructors return the initialized object pointer
    currentFunctionReturnType_ = "ptr";  // Track for return statements
    emitLine("; Constructor for " + currentClassName_);
    emitLine("define ptr @" + funcName + "(" + params.str() + ") #1 {");
    indent();

    // CRITICAL FIX: For default constructors (empty body), initialize all fields to zero/null
    if (node.body.empty()) {
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            const ClassInfo& classInfo = classIt->second;
            int fieldIndex = 0;

            for (const auto& [propName, propType] : classInfo.properties) {
                // Remove ownership indicators (^, &, %)
                std::string baseType = propType;
                if (!baseType.empty() && (baseType.back() == '^' || baseType.back() == '&' || baseType.back() == '%')) {
                    baseType = baseType.substr(0, baseType.length() - 1);
                }

                // Get pointer to field
                std::string fieldPtrReg = allocateRegister();
                emitLine(std::format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                   fieldPtrReg, currentClassName_, fieldIndex));

                // Initialize field based on type
                if (baseType == "NativeType" || baseType == "Integer" || baseType == "Float" || baseType == "Double") {
                    // Initialize to zero
                    emitLine(std::format("store i64 0, ptr {}", fieldPtrReg));
                } else if (baseType == "Bool") {
                    // Initialize to false
                    emitLine(std::format("store i1 false, ptr {}", fieldPtrReg));
                } else {
                    // Initialize pointer to null
                    emitLine(std::format("store ptr null, ptr {}", fieldPtrReg));
                }

                fieldIndex++;
            }
        }
    }

    // Generate body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Return 'this' pointer
    emitLine("ret ptr %this");

    dedent();
    emitLine("}");
    emitLine("");

    // Clear parameter mappings
    for (auto& param : node.parameters) {
        valueMap_.erase(param->name);
    }
    valueMap_.erase("this");
}

void LLVMBackend::visit(Parser::MethodDecl& node) {
    // Determine function name (qualified if inside a class)
    std::string funcName;
    bool isInstanceMethod = !currentClassName_.empty();  // All class methods are instance methods

    if (!currentClassName_.empty()) {
        funcName = getQualifiedName(currentClassName_, node.name);
    } else {
        funcName = node.name;
    }

    // Build parameter list
    std::stringstream params;

    // Add implicit 'this' parameter for instance methods
    if (isInstanceMethod) {
        params << "ptr %this";
        valueMap_["this"] = "%this";
    }

    for (size_t i = 0; i < node.parameters.size(); ++i) {
        if (i > 0 || isInstanceMethod) params << ", ";
        auto& param = node.parameters[i];
        std::string paramType = getLLVMType(param->type->typeName);
        params << paramType << " %" << param->name;

        // Store parameter in value map and track its type
        valueMap_[param->name] = "%" + param->name;
        registerTypes_["%" + param->name] = param->type->typeName;
    }

    // Generate function definition
    std::cerr << "DEBUG MethodDecl: method=" << node.name << " returnTypeName='" << node.returnType->typeName << "'" << std::endl;
    std::string returnType = getLLVMType(node.returnType->typeName);
    std::cerr << "DEBUG MethodDecl: getLLVMType returned '" << returnType << "'" << std::endl;
    currentFunctionReturnType_ = returnType;  // Track for return statements

    emitLine("; Method: " + node.name);
    emitLine("define " + returnType + " @" + funcName + "(" + params.str() + ") #1 {");
    indent();

    // Generate body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Ensure function has a return
    if (node.body.empty() || dynamic_cast<Parser::ReturnStmt*>(node.body.back().get()) == nullptr) {
        if (returnType == "void") {
            emitLine("ret void");
        } else if (returnType == "ptr") {
            emitLine("ret ptr null  ; implicit return");
        } else {
            emitLine("ret " + returnType + " 0  ; implicit return");
        }
    }

    dedent();
    emitLine("}");
    emitLine("");

    // Clear parameter mappings
    for (auto& param : node.parameters) {
        valueMap_.erase(param->name);
    }
    if (isInstanceMethod) {
        valueMap_.erase("this");
    }
}

void LLVMBackend::visit(Parser::ParameterDecl& node) {}

void LLVMBackend::visit(Parser::EntrypointDecl& node) {
    emitLine("define i32 @main() #1 {");
    indent();

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    emitLine("ret i32 0");
    dedent();
    emitLine("}");
}

void LLVMBackend::visit(Parser::InstantiateStmt& node) {
    std::string varType = getLLVMType(node.type->typeName);

    // Determine ownership from type
    OwnershipKind ownership;
    switch (node.type->ownership) {
        case Parser::OwnershipType::Owned:
            ownership = OwnershipKind::Owned;
            emitLine("; Instantiate owned variable: " + node.variableName);
            break;
        case Parser::OwnershipType::Reference:
            ownership = OwnershipKind::Reference;
            emitLine("; Instantiate reference variable: " + node.variableName);
            break;
        case Parser::OwnershipType::Copy:
            ownership = OwnershipKind::Value;
            emitLine("; Instantiate value variable: " + node.variableName);
            break;
        default:
            ownership = OwnershipKind::Owned;
            break;
    }

    // Allocate stack space for the variable
    // In LLVM IR backend, XXML objects are heap-allocated, so we usually store pointers
    // But NativeType fields use their actual LLVM types (i64, etc.)
    std::string varReg = allocateRegister();
    emitLine(varReg + " = alloca " + varType);

    // Register variable with ownership tracking
    registerVariable(node.variableName, node.type->typeName, varReg, ownership);

    // Track the declared type (including template arguments like List<Integer>)
    std::string fullTypeName = node.type->typeName;
    if (!node.type->templateArgs.empty()) {
        fullTypeName += "<";
        for (size_t i = 0; i < node.type->templateArgs.size(); ++i) {
            if (i > 0) fullTypeName += ", ";
            fullTypeName += node.type->templateArgs[i].typeArg;
        }
        fullTypeName += ">";
    }
    // IMPORTANT: alloca always returns ptr in LLVM, regardless of what type is being allocated
    // Store a special marker so we know this register is actually ptr type
    registerTypes_[varReg] = "ptr_to<" + fullTypeName + ">";

    // Evaluate initializer if present
    if (node.initializer) {
        node.initializer->accept(*this);
        std::string initValue = valueMap_["__last_expr"];

        // In LLVM IR, integer literal 0 cannot be used as a pointer
        // Convert to null for pointer types
        if (initValue == "0" && varType == "ptr") {
            initValue = "null";
        }

        // Store the initialized value (use the actual type from getLLVMType)
        emitLine("store " + varType + " " + initValue + ", ptr " + varReg);

        // The type is already tracked from the declaration above
    }
}

void LLVMBackend::visit(Parser::RunStmt& node) {
    node.expression->accept(*this);
}

void LLVMBackend::visit(Parser::ForStmt& node) {
    std::string condLabel = allocateLabel("for_cond");
    std::string bodyLabel = allocateLabel("for_body");
    std::string incrLabel = allocateLabel("for_incr");
    std::string endLabel = allocateLabel("for_end");

    // Push loop labels for break/continue
    loopStack_.push_back({incrLabel, endLabel});

    // Allocate iterator variable
    std::string iteratorReg = allocateRegister();
    std::string iteratorType = getLLVMType(node.iteratorType->typeName);
    emitLine(std::format("{} = alloca {}", iteratorReg, iteratorType));
    valueMap_[node.iteratorName] = iteratorReg;

    // Initialize iterator with range start
    node.rangeStart->accept(*this);
    std::string startValue = valueMap_["__last_expr"];
    emitLine(std::format("store {} {}, ptr {}", iteratorType, startValue, iteratorReg));

    // Evaluate range end once
    node.rangeEnd->accept(*this);
    std::string endValue = valueMap_["__last_expr"];
    std::string endReg = allocateRegister();
    emitLine(std::format("{} = alloca {}", endReg, iteratorType));
    emitLine(std::format("store {} {}, ptr {}", iteratorType, endValue, endReg));

    // Jump to condition
    emitLine(std::format("br label %{}", condLabel));

    // Condition: check if iterator < end
    emitLine(std::format("{}:", condLabel));
    indent();
    std::string currentVal = allocateRegister();
    std::string endVal = allocateRegister();
    emitLine(std::format("{} = load {}, ptr {}", currentVal, iteratorType, iteratorReg));
    emitLine(std::format("{} = load {}, ptr {}", endVal, iteratorType, endReg));

    std::string condReg = allocateRegister();
    emitLine(std::format("{} = icmp slt {} {}, {}", condReg, iteratorType, currentVal, endVal));
    emitLine(std::format("br i1 {}, label %{}, label %{}", condReg, bodyLabel, endLabel));
    dedent();

    // Body
    emitLine(std::format("{}:", bodyLabel));
    indent();
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    emitLine(std::format("br label %{}", incrLabel));
    dedent();

    // Increment iterator
    emitLine(std::format("{}:", incrLabel));
    indent();
    std::string iterVal = allocateRegister();
    emitLine(std::format("{} = load {}, ptr {}", iterVal, iteratorType, iteratorReg));
    std::string nextVal = allocateRegister();
    emitLine(std::format("{} = add {} {}, 1", nextVal, iteratorType, iterVal));
    emitLine(std::format("store {} {}, ptr {}", iteratorType, nextVal, iteratorReg));
    emitLine(std::format("br label %{}", condLabel));
    dedent();

    // End
    emitLine(std::format("{}:", endLabel));

    // Pop loop labels
    loopStack_.pop_back();
}

void LLVMBackend::visit(Parser::ExitStmt& node) {
    emitLine("call void @exit(i32 0)");
}

void LLVMBackend::visit(Parser::ReturnStmt& node) {
    if (node.value) {
        node.value->accept(*this);
        std::string returnValue = valueMap_["__last_expr"];

        // Special case: returning None::Constructor() from void function
        // None::Constructor() sets __last_expr to "null" and function should return void
        if (returnValue == "null" && currentFunctionReturnType_ == "void") {
            emitLine("ret void");
        } else {
            // Use the tracked return type from the current function
            emitLine(std::format("ret {} {}", currentFunctionReturnType_, returnValue));
        }
    } else {
        emitLine("ret void");
    }
}

void LLVMBackend::visit(Parser::IfStmt& node) {
    std::string thenLabel = allocateLabel("if_then");
    std::string elseLabel = allocateLabel("if_else");
    std::string endLabel = allocateLabel("if_end");

    // Evaluate condition
    node.condition->accept(*this);
    std::string condValue = valueMap_["__last_expr"];

    // Branch based on condition
    emitLine(std::format("br i1 {}, label %{}, label %{}",
                        condValue, thenLabel, elseLabel));

    // Then branch
    emitLine(std::format("{}:", thenLabel));
    indent();
    for (auto& stmt : node.thenBranch) {
        stmt->accept(*this);
    }
    dedent();
    emitLine(std::format("br label %{}", endLabel));

    // Else branch
    emitLine(std::format("{}:", elseLabel));
    indent();
    if (!node.elseBranch.empty()) {
        for (auto& stmt : node.elseBranch) {
            stmt->accept(*this);
        }
    }
    dedent();
    emitLine(std::format("br label %{}", endLabel));

    // End label
    emitLine(std::format("{}:", endLabel));
}

void LLVMBackend::visit(Parser::WhileStmt& node) {
    std::string condLabel = allocateLabel("while_cond");
    std::string bodyLabel = allocateLabel("while_body");
    std::string endLabel = allocateLabel("while_end");

    // Push loop labels for break/continue
    loopStack_.push_back({condLabel, endLabel});

    // Jump to condition check
    emitLine(std::format("br label %{}", condLabel));

    // Condition label
    emitLine(std::format("{}:", condLabel));
    indent();

    // Evaluate condition
    node.condition->accept(*this);
    std::string condValue = valueMap_["__last_expr"];

    // Branch based on condition
    emitLine(std::format("br i1 {}, label %{}, label %{}",
                        condValue, bodyLabel, endLabel));
    dedent();

    // Body label
    emitLine(std::format("{}:", bodyLabel));
    indent();
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    // Loop back to condition
    emitLine(std::format("br label %{}", condLabel));
    dedent();

    // End label
    emitLine(std::format("{}:", endLabel));

    // Pop loop labels
    loopStack_.pop_back();
}

void LLVMBackend::visit(Parser::BreakStmt& node) {
    if (!loopStack_.empty()) {
        emitLine(std::format("br label %{}  ; break", loopStack_.back().endLabel));
    } else {
        emitLine("; ERROR: break outside of loop");
    }
}

void LLVMBackend::visit(Parser::ContinueStmt& node) {
    if (!loopStack_.empty()) {
        emitLine(std::format("br label %{}  ; continue", loopStack_.back().condLabel));
    } else {
        emitLine("; ERROR: continue outside of loop");
    }
}

void LLVMBackend::visit(Parser::IntegerLiteralExpr& node) {
    // Integer literals are constants, don't need a register
    // Store the result so parent expressions can use it
    std::string reg = std::format("{}", node.value);
    valueMap_["__last_expr"] = reg;
}

void LLVMBackend::visit(Parser::StringLiteralExpr& node) {
    // Create a unique label for this string literal
    std::string strLabel = std::format("str.{}", labelCounter_++);

    // Store the string literal for later emission as a global constant
    stringLiterals_.push_back({strLabel, node.value});

    // Create a reference to the global string constant
    // The reference should be of type ptr, not i64
    std::string reg = std::format("@.{}", strLabel);
    valueMap_["__last_expr"] = reg;
}

void LLVMBackend::visit(Parser::BoolLiteralExpr& node) {
    // Boolean literals are constants
    std::string reg = std::format("{}", node.value ? "1" : "0");
    valueMap_["__last_expr"] = reg;
}

void LLVMBackend::visit(Parser::ThisExpr& node) {
    // 'this' pointer in LLVM IR
    valueMap_["__last_expr"] = "%this";
}

void LLVMBackend::visit(Parser::IdentifierExpr& node) {
    auto it = valueMap_.find(node.name);
    if (it != valueMap_.end()) {
        // Local variable - need to load its value
        auto varIt = variables_.find(node.name);
        if (varIt != variables_.end()) {
            // Generate load instruction with the correct type
            std::string valueReg = allocateRegister();
            emitLine(std::format("{} = load {}, ptr {}",
                               valueReg, varIt->second.llvmType, it->second));
            valueMap_["__last_expr"] = valueReg;
            // Track the type of the loaded value
            registerTypes_[valueReg] = varIt->second.type;
        } else {
            // Fallback: just use the register (for backwards compatibility)
            valueMap_["__last_expr"] = it->second;
        }
    } else if (!currentClassName_.empty()) {
        // Check if this is a property of the current class
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            const auto& properties = classIt->second.properties;
            for (size_t i = 0; i < properties.size(); ++i) {
                if (properties[i].first == node.name) {
                    // This is a property access - generate getelementptr and load
                    std::string ptrReg = allocateRegister();
                    // Mangle class name to remove ::
                    std::string mangledClassName = currentClassName_;
                    size_t pos = 0;
                    while ((pos = mangledClassName.find("::")) != std::string::npos) {
                        mangledClassName.replace(pos, 2, "_");
                    }
                    emitLine(std::format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                       ptrReg, mangledClassName, i));

                    std::string valueReg = allocateRegister();
                    std::string llvmType = properties[i].second; // LLVM type (ptr, i64, etc.)
                    emitLine(std::format("{} = load {}, ptr {}",
                                       valueReg, llvmType, ptrReg));

                    valueMap_["__last_expr"] = valueReg;
                    // Store the LLVM type as a pseudo-XXML type for later lookup
                    // This allows BinaryExpr to determine if this is a ptr
                    if (llvmType == "ptr") {
                        registerTypes_[valueReg] = "NativeType<\"ptr\">";
                    } else if (llvmType == "i64") {
                        registerTypes_[valueReg] = "NativeType<\"int64\">";
                    } else {
                        registerTypes_[valueReg] = node.name; // Fallback to property name
                    }
                    return;
                }
            }
        }

        // Not a property - must be a global identifier
        valueMap_["__last_expr"] = std::format("@{}", node.name);
    } else {
        // No current class context - must be a global identifier
        valueMap_["__last_expr"] = std::format("@{}", node.name);
    }
}

void LLVMBackend::visit(Parser::ReferenceExpr& node) {
    // Handle address-of operator: &variable
    // Returns a pointer to the variable's storage location

    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(node.expr.get())) {
        // Simple variable reference: &varName
        auto varIt = variables_.find(ident->name);
        if (varIt != variables_.end()) {
            // Return the address of the variable (which is already a pointer in LLVM)
            valueMap_["__last_expr"] = varIt->second.llvmRegister;
            return;
        }

        // Check if it's a parameter
        auto paramIt = valueMap_.find(ident->name);
        if (paramIt != valueMap_.end()) {
            // For parameters, we might need to allocate storage if not already done
            valueMap_["__last_expr"] = paramIt->second;
            return;
        }

        // Check if it's a property of the current class (implicit 'this')
        if (!currentClassName_.empty() && valueMap_.count("this")) {
            auto classIt = classes_.find(currentClassName_);
            if (classIt != classes_.end()) {
                const auto& properties = classIt->second.properties;
                for (size_t i = 0; i < properties.size(); ++i) {
                    if (properties[i].first == ident->name) {
                        // Generate getelementptr to get address of the property
                        std::string fieldPtr = allocateRegister();
                        emitLine(fieldPtr + " = getelementptr inbounds %class." + currentClassName_ +
                                ", ptr %this, i32 0, i32 " + std::to_string(i));
                        valueMap_["__last_expr"] = fieldPtr;
                        return;
                    }
                }
            }
        }
    } else if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.expr.get())) {
        // Property reference: &obj.property
        // Evaluate the object
        if (auto* objIdent = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
            auto varIt = variables_.find(objIdent->name);
            if (varIt != variables_.end()) {
                // Get the object type
                auto typeIt = registerTypes_.find(varIt->second.llvmRegister);
                std::string typeName = (typeIt != registerTypes_.end()) ? typeIt->second : varIt->second.type;

                // Find the property
                auto classIt = classes_.find(typeName);
                if (classIt != classes_.end()) {
                    const auto& properties = classIt->second.properties;
                    for (size_t i = 0; i < properties.size(); ++i) {
                        if (properties[i].first == memberAccess->member) {
                            // Generate getelementptr to get address of the property
                            std::string objPtr = allocateRegister();
                            emitLine(objPtr + " = load ptr, ptr " + varIt->second.llvmRegister);

                            std::string fieldPtr = allocateRegister();
                            std::string mangledTypeName = typeName;
                            size_t pos = 0;
                            while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                                mangledTypeName.replace(pos, 2, "_");
                            }
                            emitLine(fieldPtr + " = getelementptr inbounds %class." + mangledTypeName +
                                    ", ptr " + objPtr + ", i32 0, i32 " + std::to_string(i));

                            valueMap_["__last_expr"] = fieldPtr;
                            return;
                        }
                    }
                }
            }
        }
    }

    // Fallback: just evaluate the expression
    node.expr->accept(*this);
}

void LLVMBackend::visit(Parser::MemberAccessExpr& node) {
    // Check if this is a property access or method call
    // Method calls are handled by CallExpr, but direct property access needs handling here

    // Evaluate the object expression
    node.object->accept(*this);
    std::string objectReg = valueMap_["__last_expr"];

    if (objectReg.empty() || objectReg == "null") {
        valueMap_["__last_expr"] = "null";
        return;
    }

    // Check if this is a simple identifier (variable)
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(node.object.get())) {
        // Look up the variable to get its type
        auto varIt = variables_.find(ident->name);
        if (varIt != variables_.end()) {
            // Get the full type including template arguments
            auto typeIt = registerTypes_.find(varIt->second.llvmRegister);
            std::string typeName = (typeIt != registerTypes_.end()) ? typeIt->second : varIt->second.type;

            // Check if we have class info for this type
            auto classIt = classes_.find(typeName);
            if (classIt != classes_.end()) {
                // Find the property index
                const auto& properties = classIt->second.properties;
                for (size_t i = 0; i < properties.size(); ++i) {
                    if (properties[i].first == node.member) {
                        // Generate getelementptr to access the property
                        std::string ptrReg = allocateRegister();

                        // Load the object pointer first
                        std::string objPtr = allocateRegister();
                        emitLine(objPtr + " = load ptr, ptr " + varIt->second.llvmRegister);

                        // Get pointer to the field
                        // Mangle class name to remove ::
                        std::string mangledTypeName = typeName;
                        size_t pos = 0;
                        while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                            mangledTypeName.replace(pos, 2, "_");
                        }
                        emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                ", ptr " + objPtr + ", i32 0, i32 " + std::to_string(i));

                        // Load the field value
                        std::string valueReg = allocateRegister();
                        std::string llvmType = properties[i].second; // LLVM type (ptr, i64, etc.)
                        emitLine(valueReg + " = load " + llvmType + ", ptr " + ptrReg);

                        valueMap_["__last_expr"] = valueReg;
                        // Store the LLVM type as a pseudo-XXML type for later lookup
                        // This allows BinaryExpr to determine if this is a ptr
                        if (llvmType == "ptr") {
                            registerTypes_[valueReg] = "NativeType<\"ptr\">";
                        } else if (llvmType == "i64") {
                            registerTypes_[valueReg] = "NativeType<\"int64\">";
                        } else {
                            registerTypes_[valueReg] = properties[i].first; // Fallback to property name
                        }
                        return;
                    }
                }
            }
        }
    }

    // If we get here, property not found or not supported - store member name for CallExpr to handle
    valueMap_["__last_expr"] = node.member;
}

void LLVMBackend::visit(Parser::CallExpr& node) {
    // Extract function name from callee
    std::string functionName;
    std::string instanceRegister;  // For instance method calls
    bool isInstanceMethod = false;

    // Handle different callee types
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        // Method call like Integer::Constructor or obj.method
        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
            // Check if this is a variable/parameter (instance method) or class name (static method)
            auto varIt = variables_.find(ident->name);
            auto paramIt = valueMap_.find(ident->name);
            if (varIt != variables_.end()) {
                // Instance method on local variable: obj.method
                isInstanceMethod = true;
                instanceRegister = varIt->second.llvmRegister;

                // Get the class name from the variable's type
                // Use registerTypes_ which includes template arguments
                auto typeIt = registerTypes_.find(varIt->second.llvmRegister);
                std::string className = (typeIt != registerTypes_.end()) ? typeIt->second : varIt->second.type;

                // Strip ptr_to<> wrapper if present (from alloca types)
                if (className.find("ptr_to<") == 0) {
                    size_t start = 7;  // Length of "ptr_to<"
                    size_t end = className.rfind('>');
                    if (end != std::string::npos) {
                        className = className.substr(start, end - start);
                    }
                }

                // Mangle template arguments if present (e.g., SomeClass<Integer> -> SomeClass_Integer)
                if (className.find('<') != std::string::npos) {
                    // Replace namespace separators
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                    // Replace template brackets
                    while ((pos = className.find("<")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(">")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                    while ((pos = className.find(",")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(" ")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                } else {
                    // Mangle namespace separators for non-template classes
                    // e.g., "MyNamespace::MyClass" -> "MyNamespace_MyClass"
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                }

                // Generate qualified method name
                functionName = className + "_" + memberAccess->member;
            } else if (paramIt != valueMap_.end() && paramIt->second[0] == '%') {
                // Instance method on parameter: param.method
                isInstanceMethod = true;
                instanceRegister = paramIt->second;  // Already has % prefix

                // Get the class name from the parameter's type in registerTypes_
                auto typeIt = registerTypes_.find(paramIt->second);
                if (typeIt != registerTypes_.end()) {
                    std::string className = typeIt->second;

                    // Strip ptr_to<> wrapper if present (from alloca types)
                    if (className.find("ptr_to<") == 0) {
                        size_t start = 7;  // Length of "ptr_to<"
                        size_t end = className.rfind('>');
                        if (end != std::string::npos) {
                            className = className.substr(start, end - start);
                        }
                    }

                    // Mangle namespace separators (same as getQualifiedName)
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }

                    functionName = className + "_" + memberAccess->member;
                } else {
                    emitLine("; instance method call on parameter with unknown type: " + ident->name);
                    valueMap_["__last_expr"] = "null";
                    return;
                }
            } else {
                // Static method: Class::Method
                std::string className = ident->name;

                // Check if this is a template parameter (single uppercase letter like T, U, etc.)
                // Template parameters are not concrete classes and cannot be instantiated
                std::string baseClassName = className;
                size_t templateStart = baseClassName.find('<');
                if (templateStart != std::string::npos) {
                    baseClassName = baseClassName.substr(0, templateStart);
                }
                if (baseClassName.length() == 1 && std::isupper(baseClassName[0])) {
                    emitLine("; template parameter instantiation (not supported): " + baseClassName + "::" + memberAccess->member);
                    valueMap_["__last_expr"] = "null";
                    return;
                }

                // Mangle template arguments if present (e.g., SomeClass<Integer> -> SomeClass_Integer)
                if (className.find('<') != std::string::npos) {
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                    while ((pos = className.find("<")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(">")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                    while ((pos = className.find(",")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(" ")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                }

                functionName = className + memberAccess->member;
                // Replace :: with _ for C runtime compatibility
                size_t pos = functionName.find("::");
                while (pos != std::string::npos) {
                    functionName.replace(pos, 2, "_");
                    pos = functionName.find("::", pos + 1);
                }
            }
        } else {
            // Check if this is a Constructor call on a namespaced/templated type
            // e.g., MyNamespace::SomeClass<T>::Constructor()
            // Note: member may be "Constructor" or "::Constructor" depending on parsing
            if (memberAccess->member == "Constructor" || memberAccess->member == "::Constructor") {
                // This is a static call - extract the type name from the object expression
                std::string className = extractTypeName(memberAccess->object.get());

                if (className.empty()) {
                    emitLine("; Constructor call on unknown type");
                    valueMap_["__last_expr"] = "null";
                    return;
                }

                // Mangle template arguments if present (e.g., SomeClass<Integer> -> SomeClass_Integer)
                if (className.find('<') != std::string::npos) {
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                    while ((pos = className.find("<")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(">")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                    while ((pos = className.find(",")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(" ")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                } else {
                    // Mangle namespace separators
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                }

                // Strip :: prefix from member name if present
                std::string memberName = memberAccess->member;
                if (memberName.substr(0, 2) == "::") {
                    memberName = memberName.substr(2);
                }
                functionName = className + "_" + memberName;
            } else {
                // Instance method on complex expression (e.g., obj.method1().method2())
                // Evaluate the complex expression first
                memberAccess->object->accept(*this);
                std::string tempReg = valueMap_["__last_expr"];

                if (tempReg == "null" || tempReg.empty()) {
                    emitLine("; complex instance method call on null");
                    valueMap_["__last_expr"] = "null";
                    return;
                }

                // Look up the type of the expression result
                auto typeIt = registerTypes_.find(tempReg);
                if (typeIt == registerTypes_.end()) {
                    emitLine("; instance method call on expression with unknown type");
                    valueMap_["__last_expr"] = "null";
                    return;
                }

                // Now we know the type! Call the method on it
                isInstanceMethod = true;
                instanceRegister = tempReg;

                std::string className = typeIt->second;

                // Strip ptr_to<> wrapper if present (from alloca types)
                if (className.find("ptr_to<") == 0) {
                    size_t start = 7;  // Length of "ptr_to<"
                    size_t end = className.rfind('>');
                    if (end != std::string::npos) {
                        className = className.substr(start, end - start);
                    }
                }

                // Mangle template arguments if present
                if (className.find('<') != std::string::npos) {
                    // Replace namespace separators
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                    // Replace template brackets
                    while ((pos = className.find("<")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(">")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                    while ((pos = className.find(",")) != std::string::npos) {
                        className.replace(pos, 1, "_");
                    }
                    while ((pos = className.find(" ")) != std::string::npos) {
                        className.erase(pos, 1);
                    }
                } else {
                    // Mangle namespace separators (same as getQualifiedName)
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }
                }

                functionName = className + "_" + memberAccess->member;
            }
        }
    } else if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(node.callee.get())) {
        // Simple function call
        functionName = ident->name;
    } else {
        emitLine("; complex function call (not yet supported)");
        valueMap_["__last_expr"] = "null";
        return;
    }

    // Special handling for None::Constructor() - it represents "no value" / void
    // Just return null pointer without generating any actual call
    if (functionName == "None_Constructor") {
        emitLine("; None::Constructor() - no-op");
        valueMap_["__last_expr"] = "null";
        return;
    }

    // Evaluate arguments
    std::vector<std::string> argValues;

    // For instance methods, add 'this' pointer as first argument
    if (isInstanceMethod) {
        // Check if instanceRegister is a variable or an expression result
        // Variables need to be loaded, but expression results are already values
        std::string instancePtr;

        // Check if this register is a stored variable (needs load)
        bool isVariable = false;
        for (const auto& var : variables_) {
            if (var.second.llvmRegister == instanceRegister) {
                isVariable = true;
                break;
            }
        }

        if (isVariable) {
            // It's a variable, load it
            instancePtr = allocateRegister();
            emitLine(instancePtr + " = load ptr, ptr " + instanceRegister);

            // Transfer type information, stripping ptr_to<> wrapper
            auto typeIt = registerTypes_.find(instanceRegister);
            if (typeIt != registerTypes_.end()) {
                std::string transferType = typeIt->second;
                // Strip ptr_to<> wrapper if present
                if (transferType.find("ptr_to<") == 0) {
                    size_t start = 7;  // Length of "ptr_to<"
                    size_t end = transferType.rfind('>');
                    if (end != std::string::npos) {
                        transferType = transferType.substr(start, end - start);
                    }
                }
                registerTypes_[instancePtr] = transferType;
            }
        } else {
            // It's already an expression result (value), use it directly
            instancePtr = instanceRegister;
        }

        argValues.push_back(instancePtr);
    }

    // For Syscall functions (memcpy, memset, ptr_read, ptr_write, etc.),
    // we should NOT load arguments - they are already pointers/addresses
    bool isSyscallFunction = functionName.find("Syscall_") == 0;

    for (auto& arg : node.arguments) {
        arg->accept(*this);
        std::string argReg = valueMap_["__last_expr"];

        // For Syscall functions, use arguments directly without loading
        if (isSyscallFunction) {
            argValues.push_back(argReg);
            continue;
        }

        // Check if this argument is a variable that needs to be loaded
        bool isVariable = false;
        for (const auto& var : variables_) {
            if (var.second.llvmRegister == argReg) {
                isVariable = true;
                break;
            }
        }

        if (isVariable) {
            // It's a variable, load it
            std::string loadedReg = allocateRegister();
            emitLine(loadedReg + " = load ptr, ptr " + argReg);

            // Transfer type information, stripping ptr_to<> wrapper
            auto typeIt = registerTypes_.find(argReg);
            if (typeIt != registerTypes_.end()) {
                std::string transferType = typeIt->second;
                // Strip ptr_to<> wrapper if present
                if (transferType.find("ptr_to<") == 0) {
                    size_t start = 7;  // Length of "ptr_to<"
                    size_t end = transferType.rfind('>');
                    if (end != std::string::npos) {
                        transferType = transferType.substr(start, end - start);
                    }
                }
                registerTypes_[loadedReg] = transferType;
            }

            argValues.push_back(loadedReg);
        } else {
            // It's already an expression result, use directly
            argValues.push_back(argReg);
        }
    }

    // Map Syscall namespace methods to xxml runtime functions
    if (functionName.find("Syscall_") == 0) {
        std::string syscallMethod = functionName.substr(8);  // Remove "Syscall_" prefix
        functionName = "xxml_" + syscallMethod;  // Replace with "xxml_" prefix
    }

    // CRITICAL FIX: Detect constructor calls and allocate memory with malloc
    bool isConstructorCall = (functionName.find("_Constructor") != std::string::npos &&
                              functionName.find("_Constructor") == functionName.length() - 12);

    if (isConstructorCall && !isInstanceMethod) {
        // Extract class name from function name (e.g., "MyClass_Constructor" -> "MyClass")
        std::string className = functionName.substr(0, functionName.length() - 12);

        // Skip malloc injection for built-in types handled by the runtime library
        // These constructors allocate their own memory internally
        static const std::unordered_set<std::string> builtinTypes = {
            "Integer", "String", "Bool", "Float", "Double", "None"
        };

        if (builtinTypes.find(className) == builtinTypes.end()) {
            // Only inject malloc for user-defined types
            // Calculate size needed for this class
            size_t classSize = calculateClassSize(className);

            // Generate malloc call to allocate memory
            std::string mallocReg = allocateRegister();
            emitLine(std::format("{} = call ptr @xxml_malloc(i64 {})", mallocReg, classSize));

            // CRITICAL: Track this register as ptr type to avoid i64 conversion
            registerTypes_[mallocReg] = className;

            // Add malloc result as first argument (this pointer) to constructor
            argValues.insert(argValues.begin(), mallocReg);

            // Mark this as an instance method now (since we're passing 'this' pointer)
            isInstanceMethod = true;
        }
    }

    // Generate call instruction
    // Query semantic analyzer FIRST to get accurate return type
    std::string returnType = "";
    bool fromSemanticAnalyzer = false;

    // Try to get actual return type from semantic analyzer for instance methods
    if (semanticAnalyzer_ && isInstanceMethod) {
        // Get the actual type of the instance
        auto typeIt = registerTypes_.find(instanceRegister);
        if (typeIt != registerTypes_.end()) {
            std::string instanceType = typeIt->second;

            // Strip ptr_to<> wrapper if present (from alloca types)
            if (instanceType.find("ptr_to<") == 0) {
                size_t start = 7;  // Length of "ptr_to<"
                size_t end = instanceType.rfind('>');
                if (end != std::string::npos) {
                    instanceType = instanceType.substr(start, end - start);
                }
            }

            // Extract method name from function name
            size_t underscorePos = functionName.rfind('_');
            if (underscorePos != std::string::npos) {
                std::string methodName = functionName.substr(underscorePos + 1);

                // Look up the method in the semantic analyzer
                auto* methodInfo = semanticAnalyzer_->findMethod(instanceType, methodName);
                if (methodInfo) {
                    returnType = methodInfo->returnType;
                    fromSemanticAnalyzer = true;
                }
            }
        }
    }

    // Convert XXML return type to LLVM type
    std::string llvmReturnType = "ptr";  // Default to pointer
    bool isVoidReturn = false;

    if (fromSemanticAnalyzer && !returnType.empty()) {
        // Use semantic analyzer's return type - this handles None -> void correctly
        llvmReturnType = getLLVMType(returnType);
        if (llvmReturnType == "void") {
            isVoidReturn = true;
        }
    } else {
        // Fallback: Use heuristics for built-in runtime functions
        // Check for common void-returning patterns
        bool matchesVoidPattern =
            functionName.find("Console_print") == 0 ||
            functionName.find("xxml_exit") == 0 ||
            functionName.find("xxml_free") == 0 ||
            functionName.find("xxml_ptr_write") == 0 ||
            functionName.find("xxml_write_byte") == 0 ||
            functionName.find("String_destroy") == 0 ||
            (functionName.find("List_") == 0 && functionName.find("_add") != std::string::npos) ||
            functionName.find("_add") == functionName.length() - 4 ||  // Methods ending with _add
            functionName.find("_run") == functionName.length() - 4 ||  // Methods ending with _run
            functionName.find("_clear") == functionName.length() - 6 || // Methods ending with _clear
            functionName.find("_reset") == functionName.length() - 6;   // Methods ending with _reset

        if (matchesVoidPattern) {
            llvmReturnType = "void";
            isVoidReturn = true;
        } else if (functionName.find("toInt64") != std::string::npos ||
            functionName.find("toInt32") != std::string::npos ||
            functionName.find("length") != std::string::npos ||
            functionName.find("size") != std::string::npos ||
            functionName.find("count") != std::string::npos) {
            llvmReturnType = "i64";
        } else if (functionName.find("toBool") != std::string::npos) {
            llvmReturnType = "i1";
        }
    }

    std::string resultReg;
    if (!isVoidReturn) {
        resultReg = allocateRegister();
    }

    std::stringstream callInstr;
    if (!isVoidReturn) {
        callInstr << resultReg << " = call " << llvmReturnType << " @" << functionName << "(";
    } else {
        callInstr << "call " << llvmReturnType << " @" << functionName << "(";
    }

    for (size_t i = 0; i < argValues.size(); ++i) {
        if (i > 0) callInstr << ", ";

        std::string argValue = argValues[i];

        // Determine expected argument type for this function
        std::string expectedType = "ptr";  // Default

        // Syscall functions with specific signatures
        if (functionName.find("xxml_malloc") != std::string::npos) {
            expectedType = "i64";  // malloc takes i64 size
        } else if (functionName.find("ptr_write") != std::string::npos ||
            functionName.find("ptr_read") != std::string::npos ||
            functionName.find("free") != std::string::npos) {
            expectedType = "ptr";
        } else if (functionName.find("memcpy") != std::string::npos ||
                   functionName.find("memset") != std::string::npos) {
            expectedType = (i < 2) ? "ptr" : "i64";
        }
        // Constructors that take primitive values (exact match to avoid matching List_Integer_Constructor)
        else if (functionName == "Integer_Constructor") {
            expectedType = "i64";  // Integer constructor takes i64
        }
        else if (functionName == "Bool_Constructor") {
            expectedType = "i1";  // Bool constructor takes i1
        }
        // User-defined class constructors: first arg is always 'this' pointer (ptr)
        else if (isConstructorCall && i == 0) {
            expectedType = "ptr";  // Constructor 'this' pointer
        }
        // Default: for most object methods, first arg is 'this' (ptr), rest are usually ptr
        else if (i == 0 && functionName.find("_") != std::string::npos) {
            // First argument might be 'this' pointer for instance methods
            expectedType = "ptr";
        }

        // Determine actual argument type
        std::string argType;

        if (!argValue.empty()) {
            if (argValue == "null") {
                argType = "ptr";
            } else if (argValue[0] == '%') {
                // Register - check if it's in registerTypes_
                auto typeIt = registerTypes_.find(argValue);
                if (typeIt != registerTypes_.end()) {
                    // We have XXML type info - convert to LLVM type
                    std::string xxmlType = typeIt->second;
                    argType = getLLVMType(xxmlType);

                    // IMPORTANT: getLLVMType() may return struct types like %class.Foo
                    // but for function arguments, we always pass objects by pointer
                    if (argType.find("%class.") == 0) {
                        argType = "ptr";
                    }
                } else {
                    // No type info - use heuristic based on expectedType
                    // This avoids unnecessary conversions
                    argType = expectedType;
                }
            } else if (argValue[0] == '@') {
                argType = "ptr";  // Globals are pointers
            } else {
                argType = "i64";  // Numeric literal
            }
        } else {
            argType = "i64";
        }

        // If we have i64 but need ptr, insert inttoptr conversion
        if (argType == "i64" && expectedType == "ptr") {
            std::string convertedReg = allocateRegister();
            emitLine(std::format("{} = inttoptr i64 {} to ptr", convertedReg, argValue));
            argValue = convertedReg;
            argType = "ptr";
        } else if (argType == "ptr" && expectedType == "i64") {
            std::string convertedReg = allocateRegister();
            emitLine(std::format("{} = ptrtoint ptr {} to i64", convertedReg, argValue));
            argValue = convertedReg;
            argType = "i64";
        }

        callInstr << argType << " " << argValue;
    }

    callInstr << ")";
    emitLine(callInstr.str());

    // For void functions, set last_expr to empty/null
    if (isVoidReturn) {
        valueMap_["__last_expr"] = "";
    } else {
        valueMap_["__last_expr"] = resultReg;
    }

    // Track the return type for the result register
    // We already queried semantic analyzer earlier and have returnType if available
    if (!isVoidReturn && !resultReg.empty()) {
        // If we didn't get returnType from semantic analyzer, infer it from function name
        if (!fromSemanticAnalyzer || returnType.empty()) {
            size_t underscorePos = functionName.rfind('_');
            if (underscorePos != std::string::npos) {
                std::string className = functionName.substr(0, underscorePos);
                std::string methodName = functionName.substr(underscorePos + 1);

                // Special handling for collection methods that return element types
                if (isInstanceMethod && (methodName == "get" || methodName == "pop")) {
                    // For List<T>::get() or similar, return type is T (element type)
                    auto typeIt = registerTypes_.find(instanceRegister);
                    if (typeIt != registerTypes_.end()) {
                        std::string instanceType = typeIt->second;

                        // Strip ptr_to<> wrapper if present
                        if (instanceType.find("ptr_to<") == 0) {
                            size_t start = 7;
                            size_t end = instanceType.rfind('>');
                            if (end != std::string::npos) {
                                instanceType = instanceType.substr(start, end - start);
                            }
                        }

                        // Extract element type from List<Integer> -> Integer
                        size_t openBracket = instanceType.find('<');
                        size_t closeBracket = instanceType.find('>');
                        if (openBracket != std::string::npos && closeBracket != std::string::npos) {
                            returnType = instanceType.substr(openBracket + 1, closeBracket - openBracket - 1);
                        } else {
                            returnType = className;  // Fallback
                        }
                    } else {
                        returnType = className;  // Fallback
                    }
                } else {
                    // Default: assume return type is same as class
                    returnType = className;
                }
            }
        }

        // Store return type with ALL ownership modifiers preserved (%, ^, &)
        // These modifiers have semantic meaning and must persist in the type system
        if (!returnType.empty()) {
            registerTypes_[resultReg] = returnType;
        }
    }
}

void LLVMBackend::visit(Parser::BinaryExpr& node) {
    // Generate code for left operand
    node.left->accept(*this);
    std::string leftValue = valueMap_["__last_expr"];

    // Generate code for right operand
    node.right->accept(*this);
    std::string rightValue = valueMap_["__last_expr"];

    // Determine LLVM types of operands
    std::string leftType = "i64";  // Default
    std::string rightType = "i64"; // Default

    // Check if operands are variables or have known types
    for (const auto& var : variables_) {
        if (var.second.llvmRegister == leftValue) {
            // Get the XXML type and convert to LLVM
            auto typeIt = registerTypes_.find(leftValue);
            if (typeIt != registerTypes_.end()) {
                leftType = getLLVMType(typeIt->second);
            }
            break;
        }
    }
    for (const auto& var : variables_) {
        if (var.second.llvmRegister == rightValue) {
            auto typeIt = registerTypes_.find(rightValue);
            if (typeIt != registerTypes_.end()) {
                rightType = getLLVMType(typeIt->second);
            }
            break;
        }
    }

    // Also check registerTypes_ directly
    auto leftRegType = registerTypes_.find(leftValue);
    if (leftRegType != registerTypes_.end()) {
        leftType = getLLVMType(leftRegType->second);
    }
    auto rightRegType = registerTypes_.find(rightValue);
    if (rightRegType != registerTypes_.end()) {
        rightType = getLLVMType(rightRegType->second);
    }

    // Handle pointer arithmetic: ptr + i64 or i64 + ptr
    if ((leftType == "ptr" || rightType == "ptr") && (node.op == "+" || node.op == "-")) {
        std::string ptrValue;
        std::string offsetValue;
        bool ptrOnLeft = (leftType == "ptr");

        if (ptrOnLeft) {
            ptrValue = leftValue;
            offsetValue = rightValue;
        } else {
            ptrValue = rightValue;
            offsetValue = leftValue;
        }

        // Convert ptr to i64
        std::string ptrAsInt = allocateRegister();
        emitLine(std::format("{} = ptrtoint ptr {} to i64", ptrAsInt, ptrValue));

        // Do the arithmetic
        std::string resultInt = allocateRegister();
        if (node.op == "+") {
            if (ptrOnLeft) {
                emitLine(std::format("{} = add i64 {}, {}", resultInt, ptrAsInt, offsetValue));
            } else {
                emitLine(std::format("{} = add i64 {}, {}", resultInt, offsetValue, ptrAsInt));
            }
        } else { // "-"
            if (ptrOnLeft) {
                emitLine(std::format("{} = sub i64 {}, {}", resultInt, ptrAsInt, offsetValue));
            } else {
                // offset - ptr doesn't make sense, but generate it anyway
                emitLine(std::format("{} = sub i64 {}, {}", resultInt, offsetValue, ptrAsInt));
            }
        }

        // Convert back to ptr
        std::string resultReg = allocateRegister();
        emitLine(std::format("{} = inttoptr i64 {} to ptr", resultReg, resultInt));

        // Result is a ptr
        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "NativeType<\"ptr\">";
    } else if ((leftType == "ptr" || rightType == "ptr") &&
               (node.op == "==" || node.op == "!=" || node.op == "<" || node.op == ">" || node.op == "<=" || node.op == ">=")) {
        // Pointer comparison - convert pointers to i64 first
        std::string leftCmp = leftValue;
        std::string rightCmp = rightValue;

        if (leftType == "ptr") {
            leftCmp = allocateRegister();
            emitLine(std::format("{} = ptrtoint ptr {} to i64", leftCmp, leftValue));
        }
        if (rightType == "ptr") {
            rightCmp = allocateRegister();
            emitLine(std::format("{} = ptrtoint ptr {} to i64", rightCmp, rightValue));
        }

        std::string resultReg = allocateRegister();
        std::string opCode = generateBinaryOp(node.op, leftCmp, rightCmp, "i64");
        emitLine(std::format("{} = {}", resultReg, opCode));
        valueMap_["__last_expr"] = resultReg;
    } else {
        // Normal arithmetic or comparison - use existing logic
        std::string resultReg = allocateRegister();
        std::string opCode = generateBinaryOp(node.op, leftValue, rightValue, "i64");
        emitLine(std::format("{} = {}", resultReg, opCode));
        valueMap_["__last_expr"] = resultReg;
    }
}

void LLVMBackend::visit(Parser::TypeRef& node) {
    // TypeRef nodes don't generate code themselves
    // The type is accessed by the parent node (e.g., InstantiateStmt, ParameterDecl)
}

void LLVMBackend::visit(Parser::AssignmentStmt& node) {
    // Check if this is a move assignment (owned value to owned value)
    bool isMove = false;
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.value.get())) {
        auto varIt = variables_.find(identExpr->name);
        if (varIt != variables_.end() && varIt->second.ownership == OwnershipKind::Owned) {
            // This is a move - check and mark the source as moved
            if (checkAndMarkMoved(identExpr->name)) {
                emitLine("; Move " + identExpr->name + " to " + node.variableName);
                isMove = true;
            }
        }
    }

    // Generate code for the value expression
    node.value->accept(*this);
    std::string valueReg = valueMap_["__last_expr"];

    // Get the target variable's info
    auto targetIt = variables_.find(node.variableName);
    if (targetIt != variables_.end()) {
        // If target was owned and not moved, emit destructor first
        if (targetIt->second.ownership == OwnershipKind::Owned && !targetIt->second.isMovedFrom) {
            emitDestructor(node.variableName);
        }

        // Store the value to the variable's memory location
        emitLine("store i64 " + valueReg + ", ptr " + targetIt->second.llvmRegister);

        // If this was a move, mark target as not moved
        if (isMove) {
            targetIt->second.isMovedFrom = false;
        }
    } else if (valueMap_.find(node.variableName) != valueMap_.end()) {
        // Fallback for variables not in ownership tracking
        emitLine("store i64 " + valueReg + ", ptr " + valueMap_[node.variableName]);
    } else {
        emitLine("; Error: variable " + node.variableName + " not found");
    }
}

void LLVMBackend::visit(Parser::TypeOfExpr& node) {
    // TypeOf is a compile-time feature, emit a comment
    emitLine("; typeof expression (compile-time only)");
    valueMap_["__last_expr"] = "null";  // Placeholder
}

void LLVMBackend::visit(Parser::ConstraintDecl& node) {
    // Constraints are compile-time only
    emitLine(std::format("; constraint {}", node.name));
}

void LLVMBackend::visit(Parser::RequireStmt& node) {
    // Require statements are compile-time only
    emitLine("; require statement (compile-time only)");
}

// Object file generation using external LLVM tools
bool LLVMBackend::generateObjectFile(const std::string& irCode,
                                     const std::string& outputPath,
                                     int optimizationLevel) {
    using namespace XXML::Utils;

    // Write IR code to temporary file
    std::string tempIRFile = outputPath + ".temp.ll";
    std::ofstream irFile(tempIRFile);
    if (!irFile) {
        std::cerr << "Error: Failed to create temporary IR file: " << tempIRFile << std::endl;
        return false;
    }
    irFile << irCode;
    irFile.close();

    // Try to find LLVM's llc (LLVM static compiler)
    std::string llcPath = ProcessUtils::findInPath("llc");

    if (!llcPath.empty()) {
        // Use llc to generate object file
        std::vector<std::string> args;
        args.push_back("-filetype=obj");

        // Add optimization level
        if (optimizationLevel > 0) {
            args.push_back("-O" + std::to_string(optimizationLevel));
        }

        args.push_back("-o");
        args.push_back(outputPath);
        args.push_back(tempIRFile);

        ProcessResult result = ProcessUtils::execute(llcPath, args);

        if (!result.success) {
            std::cerr << "Error: llc failed with exit code " << result.exitCode << std::endl;
            if (!result.error.empty()) {
                std::cerr << "llc stderr: " << result.error << std::endl;
            }
            ProcessUtils::deleteFile(tempIRFile);
            return false;
        }

        ProcessUtils::deleteFile(tempIRFile);
        return true;
    }

    // Fallback: Try to use clang (just use "clang" - should be in PATH)
    std::ostringstream cmd;
    cmd << "clang -c";

    // Add optimization level
    if (optimizationLevel > 0) {
        cmd << " -O" << optimizationLevel;
    } else {
        cmd << " -O0";
    }

    cmd << " -o \"" << outputPath << "\" \"" << tempIRFile << "\"";

    // Execute using system()
    std::string cmdStr = cmd.str();
    int exitCode = std::system(cmdStr.c_str());

    if (exitCode != 0) {
        std::cerr << "Error: clang failed with exit code " << exitCode << std::endl;
        std::cerr << "Command: " << cmdStr << std::endl;
        ProcessUtils::deleteFile(tempIRFile);
        return false;
    }

    ProcessUtils::deleteFile(tempIRFile);
    return true;
}

// Template instantiation AST cloning methods

std::unique_ptr<Parser::ClassDecl> LLVMBackend::cloneAndSubstituteClassDecl(
    Parser::ClassDecl* original,
    const std::string& newName,
    const std::unordered_map<std::string, std::string>& typeMap) {

    // Create new class with mangled name and no template parameters
    auto instantiated = std::make_unique<Parser::ClassDecl>(
        newName,
        std::vector<Parser::TemplateParameter>{},  // No template params in instantiation
        original->isFinal,
        original->baseClass,
        original->location
    );

    // Deep clone all sections
    for (const auto& section : original->sections) {
        auto newSection = std::make_unique<Parser::AccessSection>(
            section->modifier,
            section->location
        );

        // Clone all declarations in the section
        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                auto clonedProp = clonePropertyDecl(prop, typeMap);
                if (clonedProp) newSection->declarations.push_back(std::move(clonedProp));
            }
            else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Clone constructor
                std::vector<std::unique_ptr<Parser::ParameterDecl>> clonedParams;
                for (const auto& param : ctor->parameters) {
                    auto clonedType = cloneTypeRef(param->type.get(), typeMap);
                    auto clonedParam = std::make_unique<Parser::ParameterDecl>(
                        param->name,
                        std::move(clonedType),
                        param->location
                    );
                    clonedParams.push_back(std::move(clonedParam));
                }

                std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
                for (const auto& stmt : ctor->body) {
                    auto clonedStmt = cloneStatement(stmt.get(), typeMap);
                    if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
                }

                auto clonedCtor = std::make_unique<Parser::ConstructorDecl>(
                    ctor->isDefault,
                    std::move(clonedParams),
                    std::move(clonedBody),
                    ctor->location
                );
                newSection->declarations.push_back(std::move(clonedCtor));
            }
            else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                auto clonedMethod = cloneMethodDecl(method, typeMap);
                if (clonedMethod) newSection->declarations.push_back(std::move(clonedMethod));
            }
        }

        instantiated->sections.push_back(std::move(newSection));
    }

    return instantiated;
}

std::unique_ptr<Parser::TypeRef> LLVMBackend::cloneTypeRef(
    const Parser::TypeRef* original,
    const std::unordered_map<std::string, std::string>& typeMap) {

    if (!original) return nullptr;

    // Substitute type name if it's a template parameter
    std::string typeName = original->typeName;
    auto it = typeMap.find(typeName);
    if (it != typeMap.end()) {
        typeName = it->second;
    }

    // Clone template arguments
    std::vector<Parser::TemplateArgument> clonedArgs;
    for (const auto& arg : original->templateArgs) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Type argument - substitute if it's a template parameter
            std::string argType = arg.typeArg;
            auto argIt = typeMap.find(argType);
            if (argIt != typeMap.end()) {
                argType = argIt->second;
            }
            clonedArgs.emplace_back(argType, arg.location);
        } else {
            // Value argument - clone the expression
            auto clonedExpr = cloneExpression(arg.valueArg.get(), typeMap);
            clonedArgs.emplace_back(std::move(clonedExpr), arg.location);
        }
    }

    return std::make_unique<Parser::TypeRef>(
        typeName,
        std::move(clonedArgs),
        original->ownership,
        original->location
    );
}

std::unique_ptr<Parser::MethodDecl> LLVMBackend::cloneMethodDecl(
    const Parser::MethodDecl* original,
    const std::unordered_map<std::string, std::string>& typeMap) {

    if (!original) return nullptr;

    auto clonedRetType = cloneTypeRef(original->returnType.get(), typeMap);

    std::vector<std::unique_ptr<Parser::ParameterDecl>> clonedParams;
    for (const auto& param : original->parameters) {
        auto clonedType = cloneTypeRef(param->type.get(), typeMap);
        auto clonedParam = std::make_unique<Parser::ParameterDecl>(
            param->name,
            std::move(clonedType),
            param->location
        );
        clonedParams.push_back(std::move(clonedParam));
    }

    std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
    for (const auto& stmt : original->body) {
        auto clonedStmt = cloneStatement(stmt.get(), typeMap);
        if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
    }

    return std::make_unique<Parser::MethodDecl>(
        original->name,
        std::move(clonedRetType),
        std::move(clonedParams),
        std::move(clonedBody),
        original->location
    );
}

std::unique_ptr<Parser::PropertyDecl> LLVMBackend::clonePropertyDecl(
    const Parser::PropertyDecl* original,
    const std::unordered_map<std::string, std::string>& typeMap) {

    if (!original) return nullptr;

    auto clonedType = cloneTypeRef(original->type.get(), typeMap);
    return std::make_unique<Parser::PropertyDecl>(
        original->name,
        std::move(clonedType),
        original->location
    );
}

std::unique_ptr<Parser::Statement> LLVMBackend::cloneStatement(
    const Parser::Statement* stmt,
    const std::unordered_map<std::string, std::string>& typeMap) {

    if (!stmt) return nullptr;

    // Handle different statement types
    if (auto* inst = dynamic_cast<const Parser::InstantiateStmt*>(stmt)) {
        auto clonedType = cloneTypeRef(inst->type.get(), typeMap);
        auto clonedInit = cloneExpression(inst->initializer.get(), typeMap);
        return std::make_unique<Parser::InstantiateStmt>(
            std::move(clonedType),
            inst->variableName,
            std::move(clonedInit),
            inst->location
        );
    }
    if (auto* run = dynamic_cast<const Parser::RunStmt*>(stmt)) {
        auto clonedExpr = cloneExpression(run->expression.get(), typeMap);
        return std::make_unique<Parser::RunStmt>(std::move(clonedExpr), run->location);
    }
    if (auto* ret = dynamic_cast<const Parser::ReturnStmt*>(stmt)) {
        auto clonedExpr = ret->value ? cloneExpression(ret->value.get(), typeMap) : nullptr;
        return std::make_unique<Parser::ReturnStmt>(std::move(clonedExpr), ret->location);
    }
    if (auto* assign = dynamic_cast<const Parser::AssignmentStmt*>(stmt)) {
        auto clonedValue = cloneExpression(assign->value.get(), typeMap);
        return std::make_unique<Parser::AssignmentStmt>(
            assign->variableName,
            std::move(clonedValue),
            assign->location
        );
    }
    if (auto* ifStmt = dynamic_cast<const Parser::IfStmt*>(stmt)) {
        auto clonedCondition = cloneExpression(ifStmt->condition.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedThenBranch;
        for (const auto& s : ifStmt->thenBranch) {
            auto cloned = cloneStatement(s.get(), typeMap);
            if (cloned) clonedThenBranch.push_back(std::move(cloned));
        }

        std::vector<std::unique_ptr<Parser::Statement>> clonedElseBranch;
        for (const auto& s : ifStmt->elseBranch) {
            auto cloned = cloneStatement(s.get(), typeMap);
            if (cloned) clonedElseBranch.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::IfStmt>(
            std::move(clonedCondition),
            std::move(clonedThenBranch),
            std::move(clonedElseBranch),
            ifStmt->location
        );
    }
    if (auto* whileStmt = dynamic_cast<const Parser::WhileStmt*>(stmt)) {
        auto clonedCondition = cloneExpression(whileStmt->condition.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
        for (const auto& s : whileStmt->body) {
            auto cloned = cloneStatement(s.get(), typeMap);
            if (cloned) clonedBody.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::WhileStmt>(
            std::move(clonedCondition),
            std::move(clonedBody),
            whileStmt->location
        );
    }
    if (auto* forStmt = dynamic_cast<const Parser::ForStmt*>(stmt)) {
        auto clonedIteratorType = cloneTypeRef(forStmt->iteratorType.get(), typeMap);
        auto clonedRangeStart = cloneExpression(forStmt->rangeStart.get(), typeMap);
        auto clonedRangeEnd = cloneExpression(forStmt->rangeEnd.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
        for (const auto& s : forStmt->body) {
            auto cloned = cloneStatement(s.get(), typeMap);
            if (cloned) clonedBody.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::ForStmt>(
            std::move(clonedIteratorType),
            forStmt->iteratorName,
            std::move(clonedRangeStart),
            std::move(clonedRangeEnd),
            std::move(clonedBody),
            forStmt->location
        );
    }
    if (auto* breakStmt = dynamic_cast<const Parser::BreakStmt*>(stmt)) {
        return std::make_unique<Parser::BreakStmt>(breakStmt->location);
    }
    if (auto* continueStmt = dynamic_cast<const Parser::ContinueStmt*>(stmt)) {
        return std::make_unique<Parser::ContinueStmt>(continueStmt->location);
    }
    if (auto* exitStmt = dynamic_cast<const Parser::ExitStmt*>(stmt)) {
        auto clonedCode = cloneExpression(exitStmt->exitCode.get(), typeMap);
        return std::make_unique<Parser::ExitStmt>(std::move(clonedCode), exitStmt->location);
    }

    // For other statement types, return nullptr (placeholder)
    return nullptr;
}

std::unique_ptr<Parser::Expression> LLVMBackend::cloneExpression(
    const Parser::Expression* expr,
    const std::unordered_map<std::string, std::string>& typeMap) {

    if (!expr) return nullptr;

    // Clone various expression types
    if (auto* intLit = dynamic_cast<const Parser::IntegerLiteralExpr*>(expr)) {
        return std::make_unique<Parser::IntegerLiteralExpr>(intLit->value, intLit->location);
    }
    if (auto* strLit = dynamic_cast<const Parser::StringLiteralExpr*>(expr)) {
        return std::make_unique<Parser::StringLiteralExpr>(strLit->value, strLit->location);
    }
    if (auto* boolLit = dynamic_cast<const Parser::BoolLiteralExpr*>(expr)) {
        return std::make_unique<Parser::BoolLiteralExpr>(boolLit->value, boolLit->location);
    }
    if (auto* ident = dynamic_cast<const Parser::IdentifierExpr*>(expr)) {
        // Check if this identifier is a template parameter that needs substitution
        auto it = typeMap.find(ident->name);
        if (it != typeMap.end()) {
            // This is a template parameter - substitute with the actual type
            // The type might be qualified (e.g., "MyNamespace::MyClass")
            // We need to convert it to a MemberAccessExpr chain
            std::string fullType = it->second;

            // Split by :: to build nested MemberAccessExprs
            size_t pos = fullType.find("::");
            if (pos != std::string::npos) {
                // Build chain: MyNamespace::MyClass -> MemberAccessExpr(IdentifierExpr("MyNamespace"), "::MyClass")
                std::unique_ptr<Parser::Expression> current = std::make_unique<Parser::IdentifierExpr>(
                    fullType.substr(0, pos), ident->location);

                size_t start = pos;
                while ((pos = fullType.find("::", start + 2)) != std::string::npos) {
                    std::string member = "::" + fullType.substr(start + 2, pos - start - 2);
                    current = std::make_unique<Parser::MemberAccessExpr>(
                        std::move(current), member, ident->location);
                    start = pos;
                }

                // Add the final member
                std::string lastMember = "::" + fullType.substr(start + 2);
                return std::make_unique<Parser::MemberAccessExpr>(
                    std::move(current), lastMember, ident->location);
            } else {
                // Simple type without namespace - just replace the identifier name
                return std::make_unique<Parser::IdentifierExpr>(fullType, ident->location);
            }
        }

        // Not a template parameter - keep the identifier as-is
        return std::make_unique<Parser::IdentifierExpr>(ident->name, ident->location);
    }
    if (auto* call = dynamic_cast<const Parser::CallExpr*>(expr)) {
        auto clonedCallee = cloneExpression(call->callee.get(), typeMap);
        std::vector<std::unique_ptr<Parser::Expression>> clonedArgs;
        for (const auto& arg : call->arguments) {
            auto clonedArg = cloneExpression(arg.get(), typeMap);
            if (clonedArg) clonedArgs.push_back(std::move(clonedArg));
        }
        return std::make_unique<Parser::CallExpr>(
            std::move(clonedCallee),
            std::move(clonedArgs),
            call->location
        );
    }
    if (auto* member = dynamic_cast<const Parser::MemberAccessExpr*>(expr)) {
        auto clonedObject = cloneExpression(member->object.get(), typeMap);
        return std::make_unique<Parser::MemberAccessExpr>(
            std::move(clonedObject),
            member->member,
            member->location
        );
    }
    if (auto* binary = dynamic_cast<const Parser::BinaryExpr*>(expr)) {
        auto clonedLeft = cloneExpression(binary->left.get(), typeMap);
        auto clonedRight = cloneExpression(binary->right.get(), typeMap);
        return std::make_unique<Parser::BinaryExpr>(
            std::move(clonedLeft),
            binary->op,
            std::move(clonedRight),
            binary->location
        );
    }
    if (auto* ref = dynamic_cast<const Parser::ReferenceExpr*>(expr)) {
        auto clonedInner = cloneExpression(ref->expr.get(), typeMap);
        return std::make_unique<Parser::ReferenceExpr>(
            std::move(clonedInner),
            ref->location
        );
    }

    // For unknown expression types, return nullptr
    return nullptr;
}

size_t LLVMBackend::calculateClassSize(const std::string& className) const {
    // Look up class in the classes_ map
    auto it = classes_.find(className);
    if (it == classes_.end()) {
        // Unknown class - use default size (1 byte to avoid zero-sized allocations)
        return 1;
    }

    const ClassInfo& classInfo = it->second;
    size_t totalSize = 0;

    // Calculate size based on properties
    for (const auto& [propName, propType] : classInfo.properties) {
        // Remove ownership indicators (^, &, %)
        std::string baseType = propType;
        if (!baseType.empty() && (baseType.back() == '^' || baseType.back() == '&' || baseType.back() == '%')) {
            baseType = baseType.substr(0, baseType.length() - 1);
        }

        // Calculate size based on type
        if (baseType == "NativeType" || baseType == "Integer" || baseType == "Float" || baseType == "Double") {
            totalSize += 8;  // 64-bit value
        } else if (baseType == "Bool") {
            totalSize += 1;  // 1 byte for bool
        } else {
            totalSize += 8;  // Pointer size (all reference types)
        }
    }

    // Ensure at least 1 byte for empty classes
    return (totalSize > 0) ? totalSize : 1;
}

} // namespace XXML::Backends
