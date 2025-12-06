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
    output_.str("");
    output_.clear();
    registerCounter_ = 0;
    labelCounter_ = 0;
    stringLiterals_.clear();
    declaredFunctions_.clear();  // Reset declared functions tracking
    generatedFunctions_.clear();  // Reset generated functions tracking
    generatedClasses_.clear();   // Reset generated classes tracking
    lambdaCounter_ = 0;  // Reset lambda counter
    lambdaInfos_.clear();  // Clear lambda tracking
    nativeMethods_.clear();  // Clear native FFI method tracking

    // Generate preamble (legacy - will be replaced)
    std::string preamble = generatePreamble();

    // Track preamble-declared functions to prevent duplicates when processing modules
    // Annotation Info bindings
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_Constructor");
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_getName");
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_getArgumentCount");
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_getArgument");
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_getArgumentByName");
    declaredFunctions_.insert("Language_Reflection_AnnotationInfo_hasArgument");
    // Annotation Arg bindings
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_Constructor");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_getName");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_getType");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_asInteger");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_asString");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_asBool");
    declaredFunctions_.insert("Language_Reflection_AnnotationArg_asDouble");

    // Forward declare all user-defined functions before code generation
    // Must come BEFORE template instantiations to avoid declare-after-define errors
    // Process imported modules first (for standard library functions), then main program
    // NOTE: Only generate declarations for runtime modules - non-runtime modules will
    // have their code generated, and LLVM rejects declare followed by define for same function.
    for (auto* importedModule : importedModules_) {
        if (importedModule) {
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
                generateFunctionDeclarations(*importedModule);
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
    if (processorMode_) {
        generateProcessorEntryPoints(program);
    }

    // Build final output
    std::stringstream finalOutput;
    finalOutput << preamble;

    if (modularCodegen_) {
        // Include legacy output FIRST (function declarations from emitLine)
        // These must come before function definitions to satisfy LLVM's use-before-define requirement
        std::string legacyCode = output_.str();
        if (!legacyCode.empty()) {
            finalOutput << legacyCode;
        }

        // Get structs, globals, and functions from LLVMIR module
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

void LLVMBackend::generateFunctionDeclarations(Parser::Program& program) {
    // Forward declare all user-defined functions to avoid "undefined value" errors
    // when the main function calls methods before their definitions are emitted

    // Only emit header once (check if any declarations have been made)
    if (declaredFunctions_.empty()) {
        emitLine("; ============================================");
        emitLine("; User-defined function declarations");
        emitLine("; ============================================");
    }

    // Primitive types that should use simple names (no namespace prefix)
    // These match the primitives list in SemanticAnalyzer::resolveTypeArgToQualified
    static const std::unordered_set<std::string> primitives = {
        "Integer", "String", "Bool", "Float", "Double", "None", "Void",
        "Int", "Int8", "Int16", "Int32", "Int64", "Byte"
    };

    // Helper lambda to process a class and emit declarations for its methods
    std::function<void(Parser::ClassDecl*, const std::string&)> processClass =
        [&](Parser::ClassDecl* classDecl, const std::string& namespacePrefix) {
        if (!classDecl) return;

        // Skip template declarations (only process instantiations)
        if (!classDecl->templateParams.empty() && classDecl->name.find('<') == std::string::npos) {
            return;
        }

        // Skip primitive types entirely - their declarations are in the preamble
        // The preamble uses a different calling convention (no 'this' pointer)
        if (primitives.count(classDecl->name) > 0) {
            return;
        }

        // Build the full class name with namespace
        std::string fullClassName = namespacePrefix.empty() ?
            classDecl->name : namespacePrefix + "_" + classDecl->name;

        // Mangle template arguments in class name using TypeNormalizer
        std::string mangledClassName = TypeNormalizer::mangleForLLVM(fullClassName);

        // Process constructors and methods from sections
        for (const auto& section : classDecl->sections) {
            if (!section) continue;
            for (const auto& decl : section->declarations) {
                // Check for constructors
                if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                    if (ctor->isDefault) continue;

                    // Build parameter list for declaration
                    std::stringstream params;
                    params << "ptr";  // 'this' pointer
                    for (size_t i = 0; i < ctor->parameters.size(); ++i) {
                        params << ", ";
                        std::string paramType = getLLVMType(ctor->parameters[i]->type->typeName);
                        // void is not valid as a parameter type - use ptr instead
                        if (paramType == "void") paramType = "ptr";
                        params << paramType;
                    }

                    // Mangle constructor name with parameter count
                    std::string funcName = mangledClassName + "_Constructor_" +
                        std::to_string(ctor->parameters.size());

                    // Skip if already declared
                    if (declaredFunctions_.find(funcName) == declaredFunctions_.end()) {
                        declaredFunctions_.insert(funcName);
                        emitLine("declare ptr @" + funcName + "(" + params.str() + ")");
                    }
                }
                // Check for methods
                else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                    // Skip native FFI methods - they're defined with internal linkage
                    if (method->isNative) {
                        continue;
                    }

                    // Build parameter list for declaration
                    std::stringstream params;
                    params << "ptr";  // 'this' pointer
                    for (size_t i = 0; i < method->parameters.size(); ++i) {
                        params << ", ";
                        std::string paramType = getLLVMType(method->parameters[i]->type->typeName);
                        // void is not valid as a parameter type - use ptr instead
                        if (paramType == "void") paramType = "ptr";
                        params << paramType;
                    }

                    std::string funcName = mangledClassName + "_" + method->name;
                    std::string returnType = getLLVMType(method->returnType->typeName);

                    // Skip if already declared
                    if (declaredFunctions_.find(funcName) == declaredFunctions_.end()) {
                        declaredFunctions_.insert(funcName);
                        emitLine("declare " + returnType + " @" + funcName + "(" + params.str() + ")");
                    }
                }
            }
        }
    };

    // Helper lambda to process namespace declarations recursively
    std::function<void(Parser::NamespaceDecl*, const std::string&)> processNamespace =
        [&](Parser::NamespaceDecl* ns, const std::string& prefix) {
        if (!ns) return;

        std::string newPrefix = prefix.empty() ? ns->name : prefix + "_" + ns->name;

        for (const auto& decl : ns->declarations) {
            if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
                processClass(classDecl, newPrefix);
            } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                processNamespace(nestedNs, newPrefix);
            }
        }
    };

    // Process all declarations in the program
    for (const auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            processClass(classDecl, "");
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            processNamespace(ns, "");
        }
    }

    // NOTE: Template instantiation functions are NOT declared here because
    // they will be defined by generateTemplateInstantiations().
    // Only functions that are called but NOT defined in this module need declarations.
    // Standard library functions like Type_forName are handled by processing imports.

    emitLine("");
}

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

void LLVMBackend::emitLine(const std::string& line) {
    output_ << getIndent() << line << "\n";
}

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
}


// ============================================
// Annotation Code Generation
// Processor entry point generation (for --processor mode)
void LLVMBackend::generateProcessorEntryPoints(Parser::Program& program) {
    emitLine("");
    emitLine("; ============================================");
    emitLine("; Annotation Processor Entry Points");
    emitLine("; ============================================");
    emitLine("");

    // Find the annotation declaration with a processor block
    Parser::AnnotationDecl* processorAnnotation = nullptr;
    for (const auto& decl : program.declarations) {
        if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            for (const auto& nsDecl : ns->declarations) {
                if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(nsDecl.get())) {
                    if (annotDecl->processor) {
                        processorAnnotation = annotDecl;
                        break;
                    }
                }
            }
        } else if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(decl.get())) {
            if (annotDecl->processor) {
                processorAnnotation = annotDecl;
                break;
            }
        }
    }

    if (!processorAnnotation) {
        emitLine("; No processor block found in annotation");
        return;
    }

    std::string annotationName = processorAnnotationName_.empty() ?
        processorAnnotation->name : processorAnnotationName_;

    // Generate annotation name string constant
    emitLine(format("@__processor_annotation_name = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                   annotationName.length() + 1, annotationName));
    emitLine("");

    // Generate __xxml_processor_annotation_name function (returns annotation name)
    emitLine("; Returns the annotation name this processor handles");
    emitLine("define dllexport ptr @__xxml_processor_annotation_name() {");
    emitLine(format("  ret ptr @__processor_annotation_name"));
    emitLine("}");
    emitLine("");

    // Generate __xxml_processor_process entry point
    // This wrapper function calls the processor's onAnnotate method
    emitLine("; Processor entry point - called by compiler for each annotation usage");
    emitLine("define dllexport void @__xxml_processor_process(ptr %reflection, ptr %compilation, ptr %args) {");

    // Find the onAnnotate method in the processor block
    bool hasOnAnnotate = false;
    Parser::MethodDecl* onAnnotateMethod = nullptr;
    std::string processorClassName = annotationName + "_Processor";

    if (processorAnnotation->processor) {
        for (const auto& section : processorAnnotation->processor->sections) {
            for (const auto& decl : section->declarations) {
                if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                    if (methodDecl->name == "onAnnotate") {
                        hasOnAnnotate = true;
                        onAnnotateMethod = methodDecl;
                        break;
                    }
                }
            }
            if (hasOnAnnotate) break;
        }
    }

    if (hasOnAnnotate && onAnnotateMethod) {
        // Generate call to the onAnnotate method
        // The generated method has signature: onAnnotate(this, ctx, comp) where:
        // - this: implicit self pointer for the Processor class (can be null for processors)
        // - ctx: ReflectionContext (the %reflection parameter)
        // - comp: CompilationContext (the %compilation parameter)
        std::string methodName = getQualifiedName(processorClassName, "onAnnotate");

        // Call the generated onAnnotate method
        // Pass null for %this since processors don't need a real instance
        emitLine(format("  call void @{}(ptr null, ptr %reflection, ptr %compilation)", methodName));
    } else {
        emitLine("  ; No onAnnotate method found in processor");
    }

    emitLine("  ret void");

    emitLine("}");
    emitLine("");
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

    // Specify target to match the IR target triple
    cmd << " -target " << getTargetTriple();

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

void LLVMBackend::generateNativeMethodThunk(Parser::MethodDecl& node) {
    // Generate a thunk function that loads a DLL and calls the native function
    // The @NativeFunction annotation provides: path, name, convention

    std::string dllPath = node.nativePath;
    std::string symbolName = node.nativeSymbol;
    Parser::CallingConvention convention = node.callingConvention;

    // Determine function name
    std::string funcName;
    if (!currentClassName_.empty()) {
        funcName = getQualifiedName(currentClassName_, node.name);
    } else {
        funcName = node.name;
    }

    // Build return type FIRST (needed for nativeMethods_ registration)
    std::string returnLLVMType = "void";
    std::string nativeReturnType = "void";
    if (node.returnType) {
        std::string typeName = node.returnType->typeName;
        // Check for NativeType
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

    // Build parameter info for nativeMethods_ registration
    bool isInstanceMethod = !currentClassName_.empty();
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

    // ALWAYS register native method info (even if already generated) for call site use
    NativeMethodInfo nativeInfo;
    size_t paramIndex = 0;
    for (size_t i = (isInstanceMethod ? 1 : 0); i < params.size(); ++i) {
        nativeInfo.paramTypes.push_back(params[i].second);
        nativeInfo.isStringPtr.push_back(params[i].second == "ptr");

        // Track original XXML type and check if it's a callback type
        std::string xxmlType = "";
        bool isCallback = false;
        if (paramIndex < node.parameters.size() && node.parameters[paramIndex]->type) {
            xxmlType = node.parameters[paramIndex]->type->typeName;
            // Strip ownership modifiers using TypeNormalizer
            std::string baseType = TypeNormalizer::stripOwnershipMarker(xxmlType);
            // Check if this type is a registered callback type
            // Try both unqualified and namespace-qualified names
            if (callbackThunks_.find(baseType) != callbackThunks_.end()) {
                isCallback = true;
            } else if (!currentNamespace_.empty()) {
                std::string qualifiedType = currentNamespace_ + "::" + baseType;
                if (callbackThunks_.find(qualifiedType) != callbackThunks_.end()) {
                    isCallback = true;
                    xxmlType = qualifiedType;  // Use qualified name for lookup
                }
            }
        }
        nativeInfo.xxmlParamTypes.push_back(xxmlType);
        nativeInfo.isCallback.push_back(isCallback);
        paramIndex++;
    }
    nativeInfo.returnType = returnLLVMType;
    nativeInfo.xxmlReturnType = node.returnType ? node.returnType->typeName : "None";
    nativeMethods_[funcName] = nativeInfo;

    // Check if already generated (thunk code only)
    if (generatedFunctions_.count(funcName) > 0) {
        return;
    }
    generatedFunctions_.insert(funcName);

    // Build parameter list for function signature
    std::string paramList;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) paramList += ", ";
        paramList += params[i].second + " %" + std::to_string(i);
    }

    // Emit function header with internal linkage to avoid conflicts with system libs
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
    } else if (returnLLVMType == "ptr") {
        emitLine("  ret ptr null");
    } else if (returnLLVMType == "i64" || returnLLVMType == "i32" || returnLLVMType == "i16" || returnLLVMType == "i8") {
        emitLine("  ret " + returnLLVMType + " 0");
    } else if (returnLLVMType == "float") {
        emitLine("  ret float 0.0");
    } else if (returnLLVMType == "double") {
        emitLine("  ret double 0.0");
    } else {
        emitLine("  ret " + returnLLVMType + " zeroinitializer");
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
    } else if (returnLLVMType == "ptr") {
        emitLine("  ret ptr null");
    } else if (returnLLVMType == "i64" || returnLLVMType == "i32" || returnLLVMType == "i16" || returnLLVMType == "i8") {
        emitLine("  ret " + returnLLVMType + " 0");
    } else if (returnLLVMType == "float") {
        emitLine("  ret float 0.0");
    } else if (returnLLVMType == "double") {
        emitLine("  ret double 0.0");
    } else {
        emitLine("  ret " + returnLLVMType + " zeroinitializer");
    }

    // Call block - ready to call the native function
    emitLine(callLabel + ":");

    // === Step 3: Build function type and call ===
    // Determine calling convention attribute
    std::string ccAttr = "";
    switch (convention) {
        case Parser::CallingConvention::CDecl:
            ccAttr = "ccc";
            break;
        case Parser::CallingConvention::StdCall:
            ccAttr = "x86_stdcallcc";
            break;
        case Parser::CallingConvention::FastCall:
            ccAttr = "x86_fastcallcc";
            break;
        case Parser::CallingConvention::Auto:
        default:
            ccAttr = "ccc";  // Default to C calling convention
            break;
    }

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

    // NOTE: Do NOT free the library after each call!
    // Libraries like GLFW need to stay loaded for the duration of the program.
    // Resources created by the library (windows, contexts, etc.) are invalidated
    // when the library is unloaded. Libraries will be freed when the process exits.

    // Return
    if (returnLLVMType == "void") {
        emitLine("  ret void");
    } else {
        emitLine("  ret " + returnLLVMType + " " + resultReg);
    }

    emitLine("}");
    emitLine("");
}

} // namespace XXML::Backends
