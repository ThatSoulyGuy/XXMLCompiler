#include "Backends/LLVMBackend.h"
#include "Backends/TypeNormalizer.h"  // For type string utilities
#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/OperatorRegistry.h"
#include "Core/FormatCompat.h"  // Compatibility layer for format
#include "Utils/ProcessUtils.h"
#include "Semantic/SemanticAnalyzer.h"  // For template instantiation
#include "Semantic/CompiletimeInterpreter.h"  // For compile-time evaluation
#include "Semantic/CompiletimeValue.h"  // For compile-time values
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>    // For std::setprecision
#include <algorithm>  // For std::replace
#include <functional> // For std::function
#include <cctype>     // For std::isdigit
#include <cstring>    // For std::memcpy
#include <cstdint>    // For uint32_t
#include <unordered_set>  // For duplicate string label tracking

// Debug output macro - define XXML_DEBUG to enable debug output
#ifdef XXML_DEBUG
#define DEBUG_OUT(x) std::cout << x
#else
#define DEBUG_OUT(x)
#endif

namespace XXML::Backends {

using XXML::Core::format;

// Helper to map NativeType string to LLVM type - uses TypeRegistry
static std::string mapNativeTypeToLLVM(const std::string& nativeType) {
    // Use TypeRegistry for centralized native type lookup
    static Core::TypeRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        registry.registerBuiltinTypes();
        initialized = true;
    }
    return registry.getNativeTypeLLVM(nativeType);
}

LLVMBackend::LLVMBackend(Core::CompilationContext* context)
    : BackendBase() {
    context_ = context;

    // Set capabilities
    addCapability(Core::BackendCapability::Optimizations);
    addCapability(Core::BackendCapability::ValueSemantics);

    // Initialize modular codegen (type-safe IR generation)
    modularCodegen_ = std::make_unique<Codegen::ModularCodegen>(context);

    // Initialize preamble generator
    preambleGen_ = std::make_unique<Codegen::PreambleGen>(targetPlatform_);
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
    // === Initialize code generation state ===
    registerCounter_ = 0;
    labelCounter_ = 0;
    stringLiterals_.clear();
    generatedFunctions_.clear();  // Reset generated functions tracking
    generatedClasses_.clear();   // Reset generated classes tracking
    lambdaCounter_ = 0;  // Reset lambda counter
    lambdaInfos_.clear();  // Clear lambda tracking
    nativeMethods_.clear();  // Clear native FFI method tracking

    // Generate preamble
    std::string preamble = generatePreamble();

    // Set processor mode on the codegen context
    if (modularCodegen_) {
        modularCodegen_->context().setProcessorMode(processorMode_);
        modularCodegen_->context().setDeriveMode(deriveMode_);
    }

    // Track preamble-declared functions to prevent duplicates when processing modules
    if (modularCodegen_) {
        // Annotation Info bindings
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_Constructor");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_getName");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_getArgumentCount");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_getArgument");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_getArgumentByName");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationInfo_hasArgument");
        // Annotation Arg bindings
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_Constructor");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_getName");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_getType");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_asInteger");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_asString");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_asBool");
        modularCodegen_->markFunctionDeclared("Language_Reflection_AnnotationArg_asDouble");
    }

    // Forward declare all user-defined functions before code generation
    // Must come BEFORE template instantiations to avoid declare-after-define errors
    // Process imported modules first (for standard library functions), then main program
    // NOTE: Only generate declarations for runtime modules - non-runtime modules will
    // have their code generated, and LLVM rejects declare followed by define for same function.
    for (auto* importedModule : importedModules_) {
        if (importedModule && modularCodegen_) {
            // Check if this is a runtime module (only generate declarations for those)
            // A module is "runtime" if its OWN namespace is Language::*, System::*, etc.
            // NOT based on what it imports - e.g., GLFW imports Language::Core but is NOT runtime
            bool isRuntimeModule = false;
            for (auto& decl : importedModule->declarations) {
                if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                    if (ns->name.find("Language") == 0 || ns->name.find("System") == 0 ||
                        ns->name.find("Syscall") == 0 || ns->name.find("Mem") == 0 ||
                        ns->name == "Collections") {
                        isRuntimeModule = true;
                        break;
                    }
                }
                // NOTE: Don't check ImportDecl - a module that imports Language::Core
                // is NOT necessarily a runtime module (e.g., GLFW imports Language::Core)
            }
            // Only generate declarations for runtime modules (their code is in the runtime library)
            if (isRuntimeModule) {
                modularCodegen_->generateFunctionDeclarations(*importedModule);
            }
        }
    }
    // NOTE: Don't generate declarations for the main program!
    // The main program's functions will be DEFINED later, and recent versions of LLVM/clang
    // reject 'declare' followed by 'define' for the same function (treats it as redefinition).
    // We only generate declarations for runtime modules whose code is in the runtime library.

    // Generate template instantiations (monomorphization) before main code
    // Use modular codegen for all templates (class, method, and lambda)
    if (modularCodegen_ && semanticAnalyzer_) {
        modularCodegen_->generateTemplates(*semanticAnalyzer_);
        modularCodegen_->generateLambdaTemplates(*semanticAnalyzer_);
    }

    // Clear namespace context after template instantiation
    // so top-level classes in the main program get the correct (empty) namespace
    if (modularCodegen_) {
        modularCodegen_->setCurrentNamespace("");
        modularCodegen_->setCurrentClass("");
    }

    // NOTE: Template AST ownership issues were fixed (TemplateClassInfo/TemplateMethodInfo
    // now store copied data instead of raw pointers). Imported module code generation is
    // now enabled with careful filtering - we skip Language:: modules that are provided
    // by the runtime library, and only generate code for user-defined imported modules.
    //
    // IMPORTANT: We need to collect reflection metadata from ALL modules (including runtime)
    // so that GetType<T> can access property/method info for standard library classes.
    //
    for (size_t i = 0; i < importedModules_.size(); ++i) {
        auto* importedModule = importedModules_[i];
        // Get module name from importedModuleNames_ if available, otherwise try to find it
        std::string moduleName = (i < importedModuleNames_.size() && !importedModuleNames_[i].empty())
            ? importedModuleNames_[i] : "unknown";

        if (importedModule) {
            // Check if this is a Language:: or System:: module (provided by runtime, skip code gen)
            // A module is "runtime" if its OWN namespace is Language::*, System::*, etc.
            // NOT based on what it imports - e.g., GLFW imports Language::Core but is NOT runtime
            bool isRuntimeModule = false;

            for (auto& decl : importedModule->declarations) {
                if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                    // If moduleName was "unknown", use the namespace name
                    if (moduleName == "unknown") {
                        moduleName = ns->name;
                    }
                    // Skip standard library modules that are provided by runtime
                    // Use prefix matching since namespace name could be "Language::Core" etc.
                    // Exception: In processor mode, we need to generate Language::ProcessorCtx
                    if (ns->name.find("Language") == 0 || ns->name.find("System") == 0 ||
                        ns->name.find("Syscall") == 0 || ns->name.find("Mem") == 0 ||
                        ns->name == "Collections") {
                        // In processor mode, we need to compile ProcessorCtx module classes
                        // In derive mode, we need to compile Derives module classes
                        if (processorMode_ && ns->name.find("Language::ProcessorCtx") == 0) {
                            isRuntimeModule = false;
                        } else if (deriveMode_ && ns->name.find("Language::Derives") == 0) {
                            isRuntimeModule = false;
                        } else {
                            isRuntimeModule = true;
                        }
                        break;
                    }
                }
                // NOTE: Don't check ImportDecl - a module that imports Language::Core
                // is NOT necessarily a runtime module (e.g., GLFW imports Language::Core)
            }

            // ALWAYS collect reflection metadata, even for runtime modules
            // This is needed for GetType<T> to work with standard library classes
            if (modularCodegen_) {
                modularCodegen_->collectReflectionMetadata(*importedModule);
            }

            if (!isRuntimeModule) {
                // Only generate code for non-runtime modules (user-defined modules)
                if (modularCodegen_) {
                    modularCodegen_->generateImportedModule(*importedModule, moduleName);
                }
            }
        }
    }

    // Generate code for main program using modular codegen system
    // DeclCodegen handles: class struct types, method/constructor/entrypoint code
    if (modularCodegen_) {
        modularCodegen_->generateProgramDecls(program.declarations);

        // Generate metadata through modular codegen (reflection, annotations)
        modularCodegen_->generateMetadata();
    }

    // Generate processor entry points if in processor mode
    if (processorMode_ && modularCodegen_) {
        modularCodegen_->generateProcessorEntryPoints(program, processorAnnotationName_);
    }

    // Generate derive entry points if in derive mode
    if (deriveMode_ && modularCodegen_) {
        modularCodegen_->generateDeriveEntryPoints(program, deriveModeName_);
    }

    // Build final output - all IR now comes from the Module
    std::stringstream finalOutput;
    finalOutput << preamble;

    if (modularCodegen_) {
        // Get structs, globals, and functions from LLVMIR module
        // Function declarations are now in the Module (generated by GlobalBuilder)
        std::string functionsIR = modularCodegen_->getFunctionsIR();
        finalOutput << functionsIR;

        // Append lambda template function definitions
        for (const auto& lambdaDef : modularCodegen_->getLambdaDefinitions()) {
            finalOutput << lambdaDef;
        }

        // Append reflection/annotation metadata
        std::string metadataIR = modularCodegen_->getMetadataIR();
        finalOutput << metadataIR;
    }

    return finalOutput.str();
}

// NOTE: All template generation (class, method, lambda) is now handled by
// ModularCodegen::generateTemplates() and ModularCodegen::generateLambdaTemplates()

// NOTE: generateFunctionDeclarations() has been moved to ModularCodegen
// It now uses GlobalBuilder to create typed function declarations in the Module

// NOTE: generateImportedModuleCode() and collectReflectionMetadataFromModule()
// have been removed - they are now handled by ModularCodegen::generateImportedModule()
// and ModularCodegen::collectReflectionMetadata()

std::string LLVMBackend::generateHeader(Parser::Program& program) {
    // LLVM IR doesn't separate headers and implementation
    return generate(program);
}

std::string LLVMBackend::generateImplementation(Parser::Program& program) {
    // LLVM IR doesn't separate headers and implementation
    return generate(program);
}

std::string LLVMBackend::getTargetTriple() const {
    return preambleGen_->getTargetTriple();
}

std::string LLVMBackend::getDataLayout() const {
    return preambleGen_->getDataLayout();
}

std::string LLVMBackend::generatePreamble() {
    return preambleGen_->generate();
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
        return format("ptr");  // Reference
    } else if (ownershipIndicator == "%") {
        return getLLVMType(std::string(type));  // Value
    } else {
        return format("ptr");  // Owned (pointer)
    }
}

std::string LLVMBackend::allocateRegister() {
    return "%r" + std::to_string(registerCounter_++);
}

std::string LLVMBackend::allocateLabel(std::string_view prefix) {
    return format("{}_{}", prefix, labelCounter_++);
}

// NOTE: emitLine() has been removed - all IR generation now goes through the
// type-safe Module system (GlobalBuilder, IRBuilder, etc.)

// Qualify a type name with the current namespace if needed
// This handles types like "Cursor^" -> "GLFW::Cursor^" when inside GLFW namespace
std::string LLVMBackend::qualifyTypeName(const std::string& typeName) const {
    if (typeName.empty() || currentNamespace_.empty()) {
        return typeName;
    }

    // Don't qualify if already qualified (contains ::)
    if (typeName.find("::") != std::string::npos) {
        return typeName;
    }

    // Don't qualify built-in types
    static const std::unordered_set<std::string> builtinTypes = {
        "Integer", "String", "Bool", "Float", "Double", "None", "void",
        "NativeType", "ptr", "__DynamicValue"
    };

    // Extract base type name (strip ownership modifier) using TypeNormalizer
    char marker = TypeNormalizer::getOwnershipMarker(typeName);
    std::string suffix = marker != '\0' ? std::string(1, marker) : "";
    std::string baseType = TypeNormalizer::stripOwnershipMarker(typeName);

    // Don't qualify built-in types
    if (builtinTypes.count(baseType) > 0) {
        return typeName;
    }

    // Don't qualify NativeType<...> or ptr_to<...>
    if (baseType.find("NativeType<") == 0 || baseType.find("ptr_to<") == 0) {
        return typeName;
    }

    // Don't qualify opaque native structures (start with _)
    if (!baseType.empty() && baseType[0] == '_') {
        return typeName;
    }

    // Check if this type is defined in the current namespace
    // by looking at the reflection metadata in CodegenContext
    std::string qualifiedName = currentNamespace_ + "::" + baseType;
    if (modularCodegen_) {
        const auto& reflectionMeta = modularCodegen_->context().reflectionMetadata();
        if (reflectionMeta.find(qualifiedName) != reflectionMeta.end()) {
            return qualifiedName + suffix;
        }
    }

    // Also check generated classes
    if (generatedClasses_.find(qualifiedName) != generatedClasses_.end()) {
        return qualifiedName + suffix;
    }

    // Return original if we can't qualify it
    return typeName;
}

std::string LLVMBackend::getLLVMType(const std::string& xxmlType) const {
    // Debug: output what we're trying to convert
    DEBUG_OUT("DEBUG getLLVMType: input type = '" << xxmlType << "'" << std::endl);

    // Handle ptr_to<T> marker (from alloca results)
    if (xxmlType.find("ptr_to<") == 0) {
        return "ptr";  // alloca always returns ptr
    }

    // __DynamicValue is a runtime-typed value - use ptr
    if (xxmlType == "__DynamicValue") {
        return "ptr";
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
            // Remove ownership marker using TypeNormalizer
            suffix = TypeNormalizer::stripOwnershipMarker(suffix);
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
                else if (nativeType == "bool") nativeType = "i1";  // bool -> i1
                else if (nativeType == "cstr") nativeType = "ptr";
                else if (nativeType == "string_ptr") nativeType = "ptr";

                DEBUG_OUT("DEBUG: Mapped to LLVM type = '" << nativeType << "'" << std::endl);
                return nativeType;  // Return the LLVM type (e.g., "ptr", "i64", "i32")
            }
        }
    }

    // Special case: None/void types must return void, not ptr
    if (xxmlType == "None" || xxmlType == "void") {
        return "void";
    }

    // Handle raw LLVM native types (when stored directly)
    if (xxmlType == "float") return "float";
    if (xxmlType == "double") return "double";
    if (xxmlType == "i1") return "i1";
    if (xxmlType == "i8") return "i8";
    if (xxmlType == "i16") return "i16";
    if (xxmlType == "i32") return "i32";
    if (xxmlType == "i64") return "i64";
    if (xxmlType == "ptr") return "ptr";

    // Check for template types (containing '<')
    // Template class instances are heap-allocated objects, so they should be ptr
    if (xxmlType.find('<') != std::string::npos) {
        // All XXML objects (including template instances) are heap-allocated
        // So they should be represented as ptr, not struct types
        return "ptr";
    }

    // âœ… USE TYPE REGISTRY for LLVM type mapping
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

std::string LLVMBackend::getDefaultValueForType(const std::string& llvmType) const {
    // Map LLVM type strings to their default values
    if (llvmType == "ptr" || llvmType.find("%class.") == 0) {
        return "ptr null";
    } else if (llvmType == "i1") {
        return "i1 false";
    } else if (llvmType == "i8" || llvmType == "i16" || llvmType == "i32" || llvmType == "i64") {
        return llvmType + " 0";
    } else if (llvmType == "float") {
        return "float 0.0";
    } else if (llvmType == "double") {
        return "double 0.0";
    }
    // Default: ptr null for unknown types (safer than 0)
    return "ptr null";
}

std::string LLVMBackend::generateBinaryOp(const std::string& op,
                                         const std::string& lhs,
                                         const std::string& rhs,
                                         const std::string& type) {
    // Determine if this is a floating-point type
    bool isFloat = (type == "double" || type == "float");

    // Arithmetic operations
    if (op == "+") {
        return isFloat ? format("fadd {} {}, {}", type, lhs, rhs)
                       : format("add {} {}, {}", type, lhs, rhs);
    }
    if (op == "-") {
        return isFloat ? format("fsub {} {}, {}", type, lhs, rhs)
                       : format("sub {} {}, {}", type, lhs, rhs);
    }
    if (op == "*") {
        return isFloat ? format("fmul {} {}, {}", type, lhs, rhs)
                       : format("mul {} {}, {}", type, lhs, rhs);
    }
    if (op == "/") {
        return isFloat ? format("fdiv {} {}, {}", type, lhs, rhs)
                       : format("sdiv {} {}, {}", type, lhs, rhs);
    }
    if (op == "%") {
        return isFloat ? format("frem {} {}, {}", type, lhs, rhs)
                       : format("srem {} {}, {}", type, lhs, rhs);
    }

    // Comparison operations
    if (isFloat) {
        if (op == "==") return format("fcmp oeq {} {}, {}", type, lhs, rhs);
        if (op == "!=") return format("fcmp one {} {}, {}", type, lhs, rhs);
        if (op == "<") return format("fcmp olt {} {}, {}", type, lhs, rhs);
        if (op == ">") return format("fcmp ogt {} {}, {}", type, lhs, rhs);
        if (op == "<=") return format("fcmp ole {} {}, {}", type, lhs, rhs);
        if (op == ">=") return format("fcmp oge {} {}, {}", type, lhs, rhs);
    } else {
        if (op == "==") return format("icmp eq {} {}, {}", type, lhs, rhs);
        if (op == "!=") return format("icmp ne {} {}, {}", type, lhs, rhs);
        if (op == "<") return format("icmp slt {} {}, {}", type, lhs, rhs);
        if (op == ">") return format("icmp sgt {} {}, {}", type, lhs, rhs);
        if (op == "<=") return format("icmp sle {} {}, {}", type, lhs, rhs);
        if (op == ">=") return format("icmp sge {} {}, {}", type, lhs, rhs);
    }

    return format("; Unknown op: {} {} {}", lhs, op, rhs);
}

std::string LLVMBackend::getQualifiedName(const std::string& className, const std::string& methodName) const {
    // Replace :: with _ for valid LLVM identifiers using TypeNormalizer
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
    return mangledClassName + "_" + methodName;
}

std::string LLVMBackend::getOrCreateGlobalString(const std::string& content) {
    // Check if this string content already has a global constant
    auto it = globalStringConstants_.find(content);
    if (it != globalStringConstants_.end()) {
        return it->second;  // Reuse existing label
    }

    // Create a new global string constant label (without @. prefix)
    // The @. prefix is added when the global is emitted in stringLiterals_
    std::string label = "str.ct." + std::to_string(stringConstantCounter_++);
    globalStringConstants_[content] = label;
    return label;
}

// NOTE: mangleTemplateName() has been removed - now in TemplateCodegen

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
        // Use after move - this is an error condition
        // Note: Error reporting now happens at semantic analysis phase
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
}


// NOTE: generateProcessorEntryPoints() has been moved to ModularCodegen
// It now uses GlobalBuilder and typed IR to generate processor entry points

// Object file generation using external LLVM tools
bool LLVMBackend::generateObjectFile(const std::string& irCode,
                                     const std::string& outputPath,
                                     int optimizationLevel,
                                     const std::string& optimizationMode,
                                     bool includeDebugSymbols) {
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

    // Build optimization flag string
    std::string optFlag;
    if (!optimizationMode.empty()) {
        optFlag = "-O" + optimizationMode;  // -Os or -Oz
    } else {
        optFlag = "-O" + std::to_string(optimizationLevel);  // -O0, -O1, -O2, -O3
    }

    // Try to find LLVM's llc (LLVM static compiler)
    std::string llcPath = ProcessUtils::findInPath("llc");

    if (!llcPath.empty()) {
        // Use llc to generate object file
        std::vector<std::string> args;
        args.push_back("-filetype=obj");

        // Add optimization level
        args.push_back(optFlag);

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

    // Specify target to match the IR target triple
    cmd << " -target " << getTargetTriple();

    // Add optimization level
    cmd << " " << optFlag;

    // Add debug symbols flag
    if (includeDebugSymbols) {
        cmd << " -g";
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

// NOTE: All AST cloning functions have been moved to ASTCloner
// (cloneTypeRef, cloneStatement, cloneExpression, cloneLambdaExpr)

size_t LLVMBackend::calculateClassSize(const std::string& className) const {
    // Look up class in the classes_ map
    auto it = classes_.find(className);
    if (it == classes_.end()) {
        // Try unmangling: convert IO_File -> IO::File
        std::string unmangledName = className;
        size_t pos = 0;
        while ((pos = unmangledName.find("_", pos)) != std::string::npos) {
            // Check if this looks like a namespace separator (uppercase letter follows)
            if (pos + 1 < unmangledName.length() && std::isupper(unmangledName[pos + 1])) {
                unmangledName.replace(pos, 1, "::");
                pos += 2;
            } else {
                pos++;
            }
        }
        it = classes_.find(unmangledName);
    }

    if (it == classes_.end()) {
        // CRITICAL FIX: If className not found, it might be a template base name (e.g., "List")
        // Try to find a template instantiation by looking for className_ prefix
        // This handles the case where List_Constructor is called but we need List_Integer size
        for (const auto& [name, info] : classes_) {
            if (name.find(className + "_") == 0) {
                // Found a template instantiation of this class
                it = classes_.find(name);
                if (it != classes_.end()) {
                    break;
                }
            }
        }

        // Still not found - use default size (1 byte to avoid zero-sized allocations)
        if (it == classes_.end()) {
            return 1;
        }
    }

    const ClassInfo& classInfo = it->second;
    size_t totalSize = 0;

    // Calculate size based on properties
    for (const auto& prop : classInfo.properties) {
        // prop is tuple<propName, llvmType, xxmlType>
        const std::string& propType = std::get<1>(prop);  // LLVM type

        // Remove ownership indicators using TypeNormalizer
        std::string baseType = TypeNormalizer::stripOwnershipMarker(propType);

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

// NOTE: generateNativeMethodThunk() has been removed - native FFI thunks are now
// generated by NativeCodegen (via ModularCodegen::generateNativeThunk())

} // namespace XXML::Backends
