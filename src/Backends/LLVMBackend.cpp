#include "Backends/LLVMBackend.h"
#include "Backends/IR/IR.h"  // New IR infrastructure
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

#ifdef _WIN32
#include <windows.h>  // For GetShortPathNameA
#endif

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

// Helper to sanitize method names for LLVM IR
// Replaces template characters <, >, , with valid LLVM identifier characters
static std::string sanitizeMethodNameForLLVM(const std::string& methodName) {
    std::string result = methodName;
    size_t pos = 0;
    // Replace < with _LT_
    while ((pos = result.find("<")) != std::string::npos) {
        result.replace(pos, 1, "_LT_");
    }
    // Replace > with _GT_
    while ((pos = result.find(">")) != std::string::npos) {
        result.replace(pos, 1, "_GT_");
    }
    // Replace , with _C_
    while ((pos = result.find(",")) != std::string::npos) {
        result.replace(pos, 1, "_C_");
    }
    // Remove spaces
    while ((pos = result.find(" ")) != std::string::npos) {
        result.erase(pos, 1);
    }
    return result;
}

LLVMBackend::LLVMBackend(Core::CompilationContext* context)
    : BackendBase() {
    context_ = context;

    // Set capabilities
    addCapability(Core::BackendCapability::Optimizations);
    addCapability(Core::BackendCapability::ValueSemantics);

    // Initialize modular codegen (type-safe IR generation)
    modularCodegen_ = std::make_unique<Codegen::ModularCodegen>(context);
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
    // === Initialize IR Infrastructure ===
    module_ = std::make_unique<IR::Module>("xxml_module");
    module_->setTargetTriple(getTargetTriple());
    module_->setDataLayout(getDataLayout());
    builder_ = std::make_unique<IR::IRBuilder>(*module_);

    // Initialize IR module with built-in types and runtime function declarations
    initializeIRModule();

    // Clear tracking state
    irValues_.clear();
    localAllocas_.clear();
    currentFunction_ = nullptr;
    currentBlock_ = nullptr;

    // === Legacy initialization (for compatibility during transition) ===
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
    pendingLambdaDefinitions_.clear();  // Clear pending lambda definitions
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
    generateTemplateInstantiations();
    generateMethodTemplateInstantiations();
    generateLambdaTemplateInstantiations();

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
            collectReflectionMetadataFromModule(*importedModule);

            if (!isRuntimeModule) {
                // Only generate code for non-runtime modules (user-defined modules)
                generateImportedModuleCode(*importedModule, moduleName);
            }
        }
    }

    // Visit main program (this collects string literals and reflection metadata)
    if (useModularCodegen_ && modularCodegen_) {
        // Use the new type-safe modular codegen system
        modularCodegen_->generateProgramDecls(program.declarations);

        // Also run legacy visitor for reflection/annotation metadata collection
        // TODO: Move reflection metadata collection to modular codegen
        program.accept(*this);
    } else {
        // Legacy code generation via visitor pattern
        program.accept(*this);
    }

    // Generate reflection metadata after all classes are visited
    generateReflectionMetadata();

    // Generate annotation metadata for retained annotations
    generateAnnotationMetadata();

    // Generate processor entry points if in processor mode
    if (processorMode_) {
        generateProcessorEntryPoints(program);
    }

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
        std::unordered_set<std::string> emittedLabels;  // Track emitted labels to avoid duplicates
        for (const auto& [label, content] : stringLiterals_) {
            // Skip if we've already emitted this label
            if (emittedLabels.count(label)) continue;
            emittedLabels.insert(label);
            // Properly escape string for LLVM IR and count actual bytes
            size_t byteCount = 0;
            std::string escapedContent;

            for (size_t i = 0; i < content.length(); ++i) {
                char c = content[i];

                // Handle literal special characters that need escaping
                if (c == '\n') {
                    escapedContent += "\\0A";  // LLVM IR hex escape for newline
                    byteCount++;
                } else if (c == '\t') {
                    escapedContent += "\\09";  // LLVM IR hex escape for tab
                    byteCount++;
                } else if (c == '\r') {
                    escapedContent += "\\0D";  // LLVM IR hex escape for CR
                    byteCount++;
                } else if (c == '"') {
                    escapedContent += "\\22";  // LLVM IR hex escape for quote
                    byteCount++;
                } else if (c == '\\') {
                    // Check if this is already an escape sequence
                    if (i + 1 < content.length()) {
                        char next = content[i + 1];
                        if (next == 'n' || next == 't' || next == 'r' || next == '\\' || next == '"' || next == '0') {
                            // Keep the escape sequence as-is
                            escapedContent += c;
                            escapedContent += next;
                            i++; // Skip the next character
                            byteCount++; // Escape sequence = 1 byte
                        } else {
                            escapedContent += "\\5C";  // LLVM IR hex escape for backslash
                            byteCount++;
                        }
                    } else {
                        escapedContent += "\\5C";  // LLVM IR hex escape for backslash
                        byteCount++;
                    }
                } else {
                    escapedContent += c;
                    byteCount++;
                }
            }

            size_t length = byteCount + 1;  // +1 for null terminator
            finalOutput << "@." << label << " = private unnamed_addr constant ["
                       << length << " x i8] c\"" << escapedContent << "\\00\"\n";
        }
        finalOutput << "\n";
    }

    finalOutput << generatedCode;

    // Emit pending lambda function definitions
    if (!pendingLambdaDefinitions_.empty()) {
        finalOutput << "\n; ============================================\n";
        finalOutput << "; Lambda Function Definitions\n";
        finalOutput << "; ============================================\n";
        for (const auto& lambdaDef : pendingLambdaDefinitions_) {
            finalOutput << lambdaDef;
        }
    }

    // === IR Infrastructure Debug Output ===
    // When useNewIR_ is enabled, we're currently still using the legacy string-based output
    // but with type-safe validation. The new IR module is built in parallel but not used
    // for final output yet. Once full migration is complete, we'll switch to using
    // IR::Emitter::emit(*module_) as the sole output.
    //
    // NOTE: Appending the new IR output here causes duplicate module headers which breaks
    // LLVM parsing. Keep this disabled until we're ready to switch entirely to new IR.
    // if (useNewIR_ && module_) {
    //     std::string irOutput = IR::emitModuleUnchecked(*module_);
    //     return irOutput;  // Replace legacy output with new IR
    // }

    // Note: Once the full migration is complete, we can replace the entire method with:
    // IR::Emitter emitter;
    // return emitter.emit(*module_);

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

        // ✅ SAFE: Access TemplateClassInfo struct (copied data, not raw pointer)
        const auto& templateInfo = it->second;
        Parser::ClassDecl* templateClassDecl = templateInfo.astNode;
        if (!templateClassDecl) {
            continue; // AST node not available (cross-module access without astNode)
        }

        // Skip if any type argument is itself a template parameter (not a concrete type)
        // This prevents generating code for self-referential types like HashMap<K, V>
        // where K and V are unbound template parameters
        bool hasTemplateParamArg = false;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                // Check if this type argument matches any template parameter name
                for (const auto& [tplName, tplInfo] : templateClasses) {
                    for (const auto& param : tplInfo.templateParams) {
                        if (param.name == arg.typeArg) {
                            hasTemplateParamArg = true;
                            std::cerr << "[DEBUG] Skipping template instantiation " << inst.templateName
                                      << " because arg '" << arg.typeArg << "' matches template param '"
                                      << param.name << "'\n";
                            break;
                        }
                    }
                    if (hasTemplateParamArg) break;
                }
                if (hasTemplateParamArg) break;
            }
        }
        if (hasTemplateParamArg) {
            continue; // Skip instantiation with unbound template parameters
        }

        std::cerr << "[DEBUG] Generating template instantiation: " << inst.templateName << "<";
        for (size_t i = 0; i < inst.arguments.size(); ++i) {
            if (i > 0) std::cerr << ", ";
            if (inst.arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                std::cerr << inst.arguments[i].typeArg;
            } else {
                std::cerr << "[value]";
            }
        }
        std::cerr << ">\n";

        // Generate mangled class name for LLVM
        // Replace < > and , with underscores: SomeClass<Integer> -> SomeClass_Integer
        std::string mangledName = inst.templateName;
        size_t valueIndex = 0;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                // Replace namespace separators with underscores using TypeNormalizer
                std::string cleanType = TypeNormalizer::mangleForLLVM(arg.typeArg);
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
        // ✅ SAFE: Use copied templateParams from TemplateClassInfo
        std::unordered_map<std::string, std::string> typeMap;
        valueIndex = 0;
        for (size_t i = 0; i < templateInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = templateInfo.templateParams[i];
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
        auto instantiated = cloneAndSubstituteClassDecl(templateClassDecl, mangledName, typeMap);

        // Generate code for the instantiated class
        if (instantiated) {
            instantiated->accept(*this);
        }
    }
}

void LLVMBackend::generateMethodTemplateInstantiations() {
    if (!semanticAnalyzer_) {
        return; // No semantic analyzer, skip template generation
    }

    const auto& instantiations = semanticAnalyzer_->getMethodTemplateInstantiations();
    const auto& templateMethods = semanticAnalyzer_->getTemplateMethods();

    for (const auto& inst : instantiations) {
        std::string methodKey = inst.className + "::" + inst.methodName;
        auto it = templateMethods.find(methodKey);
        if (it == templateMethods.end()) {
            continue; // Template method not found, skip
        }

        const auto& methodInfo = it->second;
        Parser::MethodDecl* templateMethodDecl = methodInfo.astNode;
        if (!templateMethodDecl) {
            continue; // AST node not available
        }

        // Generate mangled name matching the call site format:
        // Note: The method name should NOT include the class prefix here,
        // because visit(Parser::MethodDecl) will add currentClassName_ as prefix.
        // Method name should be: MethodName_LT_TypeArg1_GT_
        std::string mangledName = inst.methodName + "_LT_";
        bool first = true;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                if (!first) mangledName += "_C_";  // Comma separator
                first = false;
                std::string cleanType = arg.typeArg;
                size_t pos = 0;
                while ((pos = cleanType.find("::")) != std::string::npos) {
                    cleanType.replace(pos, 2, "_");
                }
                mangledName += cleanType;
            }
        }
        mangledName += "_GT_";

        // Skip if already generated
        if (generatedMethodInstantiations_.find(mangledName) != generatedMethodInstantiations_.end()) {
            continue;
        }
        generatedMethodInstantiations_.insert(mangledName);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        size_t valueIndex = 0;
        for (size_t i = 0; i < methodInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = methodInfo.templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone method and substitute types
        auto clonedMethod = cloneAndSubstituteMethodDecl(templateMethodDecl, mangledName, typeMap);
        if (clonedMethod) {
            // Set the current class context for the method
            // Use instantiatedClassName if available (e.g., "Holder<Integer>" -> "Holder_Integer")
            std::string savedClassName = currentClassName_;
            std::string classNameForMethod = inst.instantiatedClassName.empty() ? inst.className : inst.instantiatedClassName;

            // Mangle the class name: replace < with _, > with nothing, and , with _
            size_t pos;
            while ((pos = classNameForMethod.find('<')) != std::string::npos) {
                classNameForMethod.replace(pos, 1, "_");
            }
            while ((pos = classNameForMethod.find('>')) != std::string::npos) {
                classNameForMethod.erase(pos, 1);
            }
            while ((pos = classNameForMethod.find(',')) != std::string::npos) {
                classNameForMethod.replace(pos, 1, "_");
            }
            while ((pos = classNameForMethod.find(' ')) != std::string::npos) {
                classNameForMethod.erase(pos, 1);
            }

            currentClassName_ = classNameForMethod;

            // Generate the instantiated method
            clonedMethod->accept(*this);

            currentClassName_ = savedClassName;
        }
    }
}

void LLVMBackend::generateLambdaTemplateInstantiations() {
    if (!semanticAnalyzer_) {
        return; // No semantic analyzer, skip template generation
    }

    const auto& instantiations = semanticAnalyzer_->getLambdaTemplateInstantiations();
    const auto& templateLambdas = semanticAnalyzer_->getTemplateLambdas();

    for (const auto& inst : instantiations) {
        // Find the template lambda by variable name
        auto it = templateLambdas.find(inst.variableName);
        if (it == templateLambdas.end()) {
            continue; // Template lambda not found, skip
        }

        const auto& lambdaInfo = it->second;
        Parser::LambdaExpr* templateLambdaExpr = lambdaInfo.astNode;
        if (!templateLambdaExpr) {
            continue; // AST node not available
        }

        // Generate mangled name: variableName_LT_Type_GT_
        std::string mangledName = inst.variableName + "_LT_";
        bool first = true;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                if (!first) mangledName += "_C_";  // Comma separator
                first = false;
                std::string cleanType = arg.typeArg;
                // Replace :: with _
                size_t pos = 0;
                while ((pos = cleanType.find("::")) != std::string::npos) {
                    cleanType.replace(pos, 2, "_");
                }
                mangledName += cleanType;
            }
        }
        mangledName += "_GT_";

        // Skip if already generated
        if (generatedLambdaInstantiations_.find(mangledName) != generatedLambdaInstantiations_.end()) {
            continue;
        }
        generatedLambdaInstantiations_.insert(mangledName);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        size_t valueIndex = 0;
        for (size_t i = 0; i < lambdaInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = lambdaInfo.templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone lambda with type substitutions
        auto clonedLambda = cloneLambdaExpr(templateLambdaExpr, typeMap);
        if (clonedLambda) {
            // Generate unique function name for this instantiation
            int lambdaId = lambdaCounter_++;
            std::string lambdaFuncName = "@lambda.template." + mangledName;

            emitLine(format("; Lambda template instantiation: {}", mangledName));

            // Determine return type
            std::string returnType = "ptr";  // Default to ptr for objects
            if (clonedLambda->returnType) {
                returnType = getLLVMType(clonedLambda->returnType->typeName);
            }

            // Build parameter types list (closure ptr + actual params)
            std::vector<std::string> paramTypes;
            paramTypes.push_back("ptr");  // First param is always closure pointer
            for (const auto& param : clonedLambda->parameters) {
                std::string paramType = getLLVMType(param->type->typeName);
                paramTypes.push_back(paramType);
            }

            // Build closure struct type: { ptr (func), ptr (capture0), ptr (capture1), ... }
            std::string closureTypeStr = "{ ptr";
            for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
                closureTypeStr += ", ptr";
            }
            closureTypeStr += " }";

            // Generate lambda function definition
            std::stringstream lambdaDef;
            lambdaDef << "\n; Lambda template function: " << mangledName << "\n";
            lambdaDef << "define " << returnType << " " << lambdaFuncName << "(ptr %closure";
            for (size_t i = 0; i < clonedLambda->parameters.size(); ++i) {
                lambdaDef << ", " << paramTypes[i + 1] << " %" << clonedLambda->parameters[i]->name;
            }
            lambdaDef << ") {\n";
            lambdaDef << "entry:\n";

            // Save current context
            auto savedVariables = variables_;
            auto savedValueMap = valueMap_;
            auto savedRegisterTypes = registerTypes_;
            auto savedOutput = output_.str();
            auto savedReturnType = currentFunctionReturnType_;
            output_.str("");

            // Set return type for lambda body
            currentFunctionReturnType_ = returnType;

            // Set up parameter mappings for lambda body
            valueMap_.clear();
            variables_.clear();

            // Map parameters with ownership modifiers
            for (const auto& param : clonedLambda->parameters) {
                valueMap_[param->name] = "%" + param->name;
                std::string typeWithOwnership = param->type->typeName;
                switch (param->type->ownership) {
                    case Parser::OwnershipType::Reference: typeWithOwnership += "&"; break;
                    case Parser::OwnershipType::Owned: typeWithOwnership += "^"; break;
                    case Parser::OwnershipType::Copy: typeWithOwnership += "%"; break;
                    default: break;
                }
                registerTypes_["%" + param->name] = typeWithOwnership;
            }

            // Load captured variables from closure struct and map them
            for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
                const auto& capture = clonedLambda->captures[i];
                std::string captureReg = "%capture." + capture.varName;
                // GEP to get pointer to capture field (index i+1, since index 0 is function ptr)
                lambdaDef << "  " << captureReg << ".ptr = getelementptr inbounds "
                          << closureTypeStr << ", ptr %closure, i32 0, i32 " << (i + 1) << "\n";
                // Load the captured value pointer
                lambdaDef << "  " << captureReg << " = load ptr, ptr " << captureReg << ".ptr\n";
                // Map the captured variable name to the loaded register
                valueMap_[capture.varName] = captureReg;
                // Get captured variable type from semantic analyzer info
                auto capturedTypeIt = lambdaInfo.capturedVarTypes.find(capture.varName);
                if (capturedTypeIt != lambdaInfo.capturedVarTypes.end()) {
                    registerTypes_[captureReg] = capturedTypeIt->second;
                } else {
                    registerTypes_[captureReg] = "ptr";
                }
            }

            // Check if body contains a return statement
            bool hasReturn = false;
            for (const auto& stmt : clonedLambda->body) {
                if (dynamic_cast<Parser::ReturnStmt*>(stmt.get())) {
                    hasReturn = true;
                    break;
                }
            }

            // Generate lambda body statements
            for (const auto& stmt : clonedLambda->body) {
                stmt->accept(*this);
            }

            // Get generated body and add to lambda definition
            std::string bodyCode = output_.str();
            std::istringstream iss(bodyCode);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    lambdaDef << "  " << line << "\n";
                }
            }

            // Only add default return if body doesn't have one
            if (!hasReturn) {
                if (returnType == "ptr") {
                    lambdaDef << "  ret ptr null\n";
                } else if (returnType == "void") {
                    lambdaDef << "  ret void\n";
                } else if (returnType == "i64") {
                    lambdaDef << "  ret i64 0\n";
                } else if (returnType == "i1") {
                    lambdaDef << "  ret i1 false\n";
                } else {
                    lambdaDef << "  ret " << returnType << " zeroinitializer\n";
                }
            }
            lambdaDef << "}\n";

            // Store lambda definition for later emission
            pendingLambdaDefinitions_.push_back(lambdaDef.str());

            // Restore context
            output_.str(savedOutput);
            output_.clear();
            output_.seekp(0, std::ios_base::end);
            variables_ = savedVariables;
            valueMap_ = savedValueMap;
            registerTypes_ = savedRegisterTypes;
            currentFunctionReturnType_ = savedReturnType;

            // Build the key for lookup: "identity<Integer>"
            std::string lookupKey = inst.variableName + "<";
            bool firstArg = true;
            for (const auto& arg : inst.arguments) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    if (!firstArg) lookupKey += ", ";
                    firstArg = false;
                    lookupKey += arg.typeArg;
                }
            }
            lookupKey += ">";

            // Store function mapping for call site lookup
            templateLambdaFunctions_[lookupKey] = lambdaFuncName;
            templateLambdaFunctions_[mangledName] = lambdaFuncName;

            // Also store LambdaInfo for parameter types etc.
            LambdaInfo info;
            info.functionName = lambdaFuncName;
            info.returnType = returnType;
            info.paramTypes = paramTypes;
            templateLambdaInfos_[lookupKey] = info;
            templateLambdaInfos_[mangledName] = info;
        }
    }
}

void LLVMBackend::generateFunctionDeclarations(Parser::Program& program) {
    // Forward declare all user-defined functions to avoid "undefined value" errors
    // when the main function calls methods before their definitions are emitted

    // Only emit header once (check if any declarations have been made)
    if (declaredFunctions_.empty()) {
        emitLine("; ============================================");
        emitLine("; User-defined function declarations");
        emitLine("; ============================================");
    }

    // Helper lambda to process a class and emit declarations for its methods
    std::function<void(Parser::ClassDecl*, const std::string&)> processClass =
        [&](Parser::ClassDecl* classDecl, const std::string& namespacePrefix) {
        if (!classDecl) return;

        // Skip template declarations (only process instantiations)
        if (!classDecl->templateParams.empty() && classDecl->name.find('<') == std::string::npos) {
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

                        // IR: Declare constructor function
                        if (module_) {
                            std::vector<IR::Type*> paramTypes;
                            paramTypes.push_back(builder_->getPtrTy());  // this
                            for (const auto& param : ctor->parameters) {
                                paramTypes.push_back(getIRType(param->type->typeName));
                            }
                            auto& ctx = module_->getContext();
                            auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), paramTypes, false);
                            module_->createFunction(funcTy, funcName, IR::Function::Linkage::External);
                        }
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

                        // IR: Declare method function
                        if (module_) {
                            std::vector<IR::Type*> paramTypes;
                            paramTypes.push_back(builder_->getPtrTy());  // this
                            for (const auto& param : method->parameters) {
                                paramTypes.push_back(getIRType(param->type->typeName));
                            }
                            auto& ctx = module_->getContext();
                            IR::Type* retTy = getIRType(method->returnType->typeName);
                            auto* funcTy = ctx.getFunctionTy(retTy, paramTypes, false);
                            module_->createFunction(funcTy, funcName, IR::Function::Linkage::External);
                        }
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

void LLVMBackend::generateImportedModuleCode(Parser::Program& program, const std::string& moduleName) {
    // Generate code for imported modules safely by processing classes directly.
    // This avoids issues with the full visitor pattern by:
    // 1. Skipping entrypoints (imported modules shouldn't have them)
    // 2. Properly managing state between class generations
    // 3. Not relying on the visitor pattern for top-level iteration
    // 4. Using moduleName as namespace for modules without explicit namespace wrappers


    emitLine("; ============================================");
    emitLine(format("; Imported module code: {}", moduleName));
    emitLine("; ============================================");

    // Save current state
    std::string savedNamespace = currentNamespace_;
    std::string savedClassName = currentClassName_;

    // Determine the default namespace for this module
    // For modules like "GLFW::GLFW" (subdir/file naming), use just the first component "GLFW"
    // For modules like "Language::Core::String", this code path isn't hit (they have explicit namespace wrappers)
    std::string defaultModuleNamespace = moduleName;
    size_t colonPos = moduleName.find("::");
    if (colonPos != std::string::npos) {
        defaultModuleNamespace = moduleName.substr(0, colonPos);
    }

    // Helper to process a namespace (recursively visits nested namespaces and classes)
    std::function<void(Parser::NamespaceDecl*)> processNamespace;
    processNamespace = [&](Parser::NamespaceDecl* ns) {
        if (!ns) {
            return;
        }


        // Set namespace context
        std::string previousNamespace = currentNamespace_;
        if (currentNamespace_.empty()) {
            currentNamespace_ = ns->name;
        } else {
            currentNamespace_ = currentNamespace_ + "::" + ns->name;
        }

        emitLine(format("; namespace {}", ns->name));

        // IR: Namespace processing marker
        if (builder_) {
            (void)builder_->getPtrTy();
        }

        for (auto& decl : ns->declarations) {
            if (!decl) {
                continue;
            }

            if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
                // Process class - use the visitor for class declarations
                // Reset class-specific state before visiting
                currentClassName_ = "";
                variables_.clear();
                try {
                    classDecl->accept(*this);
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Exception visiting class " << classDecl->name << ": " << e.what() << "\n";
                }
            } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                // Recursively process nested namespace
                processNamespace(nestedNs);
            }
            // Skip ImportDecl and EntrypointDecl
        }

        // Restore namespace context
        currentNamespace_ = previousNamespace;
    };

    // Process all declarations in the program
    for (auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }

        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            // Process top-level class - use module name as namespace
            currentNamespace_ = defaultModuleNamespace;
            currentClassName_ = "";
            variables_.clear();
            try {
                classDecl->accept(*this);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting class " << classDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            // Process namespace - explicit namespace in file, start fresh
            currentNamespace_ = "";
            processNamespace(ns);
        } else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
            // Process top-level method (including @NativeFunction FFI methods) - use module name as namespace
            currentNamespace_ = defaultModuleNamespace;
            currentClassName_ = "";
            try {
                methodDecl->accept(*this);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting method " << methodDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* nativeStruct = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            // Process NativeStructure (opaque FFI handles) - use module name as namespace
            currentNamespace_ = defaultModuleNamespace;
            currentClassName_ = "";
            try {
                nativeStruct->accept(*this);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting NativeStructure " << nativeStruct->name << ": " << e.what() << "\n";
            }
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            // Process top-level Enumeration - use module name as namespace
            currentNamespace_ = defaultModuleNamespace;
            currentClassName_ = "";
            try {
                enumDecl->accept(*this);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting Enumeration " << enumDecl->name << ": " << e.what() << "\n";
            }
        }
        // Skip ImportDecl and EntrypointDecl
    }

    // Restore saved state
    currentNamespace_ = savedNamespace;
    currentClassName_ = savedClassName;

    emitLine("");
}

void LLVMBackend::collectReflectionMetadataFromModule(Parser::Program& program) {
    // Collect reflection metadata from a module WITHOUT generating any code.
    // This is used for runtime modules (Language::*, System::*, etc.) where we need
    // reflection info but the actual implementations are provided by the runtime library.

    // Helper to convert OwnershipType to string character
    auto ownershipToString = [](Parser::OwnershipType ownership) -> std::string {
        switch (ownership) {
            case Parser::OwnershipType::Owned: return "^";
            case Parser::OwnershipType::Reference: return "&";
            case Parser::OwnershipType::Copy: return "%";
            case Parser::OwnershipType::None: return "";
            default: return "";
        }
    };

    // Helper to collect metadata from a class
    auto collectClassMetadata = [&](Parser::ClassDecl* classDecl, const std::string& namespaceName) {
        if (!classDecl) return;

        ReflectionClassMetadata metadata;
        metadata.name = classDecl->name;
        metadata.namespaceName = namespaceName;
        if (!namespaceName.empty()) {
            metadata.fullName = namespaceName + "::" + classDecl->name;
        } else {
            metadata.fullName = classDecl->name;
        }
        metadata.isTemplate = !classDecl->templateParams.empty();
        for (const auto& param : classDecl->templateParams) {
            metadata.templateParams.push_back(param.name);
        }
        metadata.instanceSize = 0;  // Will be calculated later if needed
        metadata.astNode = classDecl;

        // Collect properties with ownership
        for (auto& section : classDecl->sections) {
            for (auto& decl : section->declarations) {
                if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                    metadata.properties.push_back({prop->name, prop->type->typeName});
                    metadata.propertyOwnerships.push_back(ownershipToString(prop->type->ownership));
                }
            }
        }

        // Collect methods with parameters
        for (auto& section : classDecl->sections) {
            for (auto& decl : section->declarations) {
                if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                    metadata.methods.push_back({method->name, method->returnType ? method->returnType->typeName : "None"});
                    metadata.methodReturnOwnerships.push_back(method->returnType ? ownershipToString(method->returnType->ownership) : "");

                    std::vector<std::tuple<std::string, std::string, std::string>> params;
                    for (const auto& param : method->parameters) {
                        params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                    }
                    metadata.methodParameters.push_back(params);
                } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                    // Constructors are special methods
                    metadata.methods.push_back({"Constructor", classDecl->name});
                    metadata.methodReturnOwnerships.push_back("^");

                    std::vector<std::tuple<std::string, std::string, std::string>> params;
                    for (const auto& param : ctor->parameters) {
                        params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                    }
                    metadata.methodParameters.push_back(params);
                }
            }
        }

        // Store metadata (don't overwrite if already exists)
        if (reflectionMetadata_.find(metadata.fullName) == reflectionMetadata_.end()) {
            reflectionMetadata_[metadata.fullName] = metadata;
        }
    };

    // Recursive helper to process namespaces
    std::function<void(Parser::NamespaceDecl*, const std::string&)> processNamespace;
    processNamespace = [&](Parser::NamespaceDecl* ns, const std::string& parentNamespace) {
        if (!ns) return;

        std::string currentNs = parentNamespace.empty() ? ns->name : (parentNamespace + "::" + ns->name);

        for (auto& decl : ns->declarations) {
            if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
                collectClassMetadata(classDecl, currentNs);
            } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                processNamespace(nestedNs, currentNs);
            }
        }
    };

    // Process all declarations in the program
    for (auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            collectClassMetadata(classDecl, "");
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            processNamespace(ns, "");
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
    #if defined(__MINGW32__) || defined(__MINGW64__)
            return "x86_64-w64-windows-gnu";  // MinGW uses GNU ABI with ___chkstk (3 underscores)
    #else
            return "x86_64-pc-windows-msvc";  // MSVC uses __chkstk (2 underscores)
    #endif
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
    preamble << "declare i64 @xxml_int64_read(ptr)\n";
    preamble << "declare void @xxml_int64_write(ptr, i64)\n";
    preamble << "\n";

    // Integer Operations
    preamble << "; Integer Operations\n";
    preamble << "declare ptr @Integer_Constructor(i64)\n";
    preamble << "declare i64 @Integer_getValue(ptr)\n";
    preamble << "declare ptr @Integer_add(ptr, ptr)\n";
    preamble << "declare ptr @Integer_sub(ptr, ptr)\n";
    preamble << "declare ptr @Integer_mul(ptr, ptr)\n";
    preamble << "declare ptr @Integer_div(ptr, ptr)\n";
    preamble << "declare ptr @Integer_negate(ptr)\n";
    preamble << "declare ptr @Integer_mod(ptr, ptr)\n";
    preamble << "declare i1 @Integer_eq(ptr, ptr)\n";
    preamble << "declare i1 @Integer_ne(ptr, ptr)\n";
    preamble << "declare i1 @Integer_lt(ptr, ptr)\n";
    preamble << "declare i1 @Integer_le(ptr, ptr)\n";
    preamble << "declare i1 @Integer_gt(ptr, ptr)\n";
    preamble << "declare i1 @Integer_ge(ptr, ptr)\n";
    preamble << "declare i64 @Integer_toInt64(ptr)\n";
    preamble << "declare ptr @Integer_toString(ptr)\n";
    preamble << "declare ptr @Integer_addAssign(ptr, ptr)\n";
    preamble << "declare ptr @Integer_subtractAssign(ptr, ptr)\n";
    preamble << "declare ptr @Integer_multiplyAssign(ptr, ptr)\n";
    preamble << "declare ptr @Integer_divideAssign(ptr, ptr)\n";
    preamble << "declare ptr @Integer_moduloAssign(ptr, ptr)\n";
    preamble << "\n";

    // Float Operations
    preamble << "; Float Operations\n";
    preamble << "declare ptr @Float_Constructor(float)\n";
    preamble << "declare float @Float_getValue(ptr)\n";
    preamble << "declare ptr @Float_toString(ptr)\n";
    preamble << "declare ptr @xxml_float_to_string(float)\n";
    preamble << "declare ptr @Float_addAssign(ptr, ptr)\n";
    preamble << "declare ptr @Float_subtractAssign(ptr, ptr)\n";
    preamble << "declare ptr @Float_multiplyAssign(ptr, ptr)\n";
    preamble << "declare ptr @Float_divideAssign(ptr, ptr)\n";
    preamble << "\n";

    // Double Operations
    preamble << "; Double Operations\n";
    preamble << "declare ptr @Double_Constructor(double)\n";
    preamble << "declare double @Double_getValue(ptr)\n";
    preamble << "declare ptr @Double_toString(ptr)\n";
    preamble << "declare ptr @Double_addAssign(ptr, ptr)\n";
    preamble << "declare ptr @Double_subtractAssign(ptr, ptr)\n";
    preamble << "declare ptr @Double_multiplyAssign(ptr, ptr)\n";
    preamble << "declare ptr @Double_divideAssign(ptr, ptr)\n";
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
    preamble << "declare i1 @String_isEmpty(ptr)\n";
    preamble << "declare void @String_destroy(ptr)\n";
    preamble << "\n";

    // Bool Operations
    preamble << "; Bool Operations\n";
    preamble << "declare ptr @Bool_Constructor(i1)\n";
    preamble << "declare i1 @Bool_getValue(ptr)\n";
    preamble << "declare ptr @Bool_and(ptr, ptr)\n";
    preamble << "declare ptr @Bool_or(ptr, ptr)\n";
    preamble << "declare ptr @Bool_not(ptr)\n";
    preamble << "declare ptr @Bool_xor(ptr, ptr)\n";
    preamble << "declare ptr @Bool_toInteger(ptr)\n";
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

    // Reflection Runtime Functions
    preamble << "; Reflection Runtime Functions\n";
    preamble << "declare void @Reflection_registerType(ptr)\n";
    preamble << "declare ptr @Reflection_getTypeInfo(ptr)\n";
    preamble << "declare i32 @Reflection_getTypeCount()\n";
    preamble << "declare ptr @Reflection_getAllTypeNames()\n";
    preamble << "\n";

    // Reflection Syscall Functions (xxml_ prefixed)
    preamble << "; Reflection Syscall Functions\n";
    preamble << "declare ptr @xxml_reflection_getTypeByName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_type_getName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_type_getFullName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_type_getNamespace(ptr)\n";
    preamble << "declare i64 @xxml_reflection_type_isTemplate(ptr)\n";
    preamble << "declare i64 @xxml_reflection_type_getTemplateParamCount(ptr)\n";
    preamble << "declare i64 @xxml_reflection_type_getPropertyCount(ptr)\n";
    preamble << "declare ptr @xxml_reflection_type_getProperty(ptr, i64)\n";
    preamble << "declare ptr @xxml_reflection_type_getPropertyByName(ptr, ptr)\n";
    preamble << "declare i64 @xxml_reflection_type_getMethodCount(ptr)\n";
    preamble << "declare ptr @xxml_reflection_type_getMethod(ptr, i64)\n";
    preamble << "declare ptr @xxml_reflection_type_getMethodByName(ptr, ptr)\n";
    preamble << "declare i64 @xxml_reflection_type_getInstanceSize(ptr)\n";
    preamble << "declare ptr @xxml_reflection_property_getName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_property_getTypeName(ptr)\n";
    preamble << "declare i64 @xxml_reflection_property_getOwnership(ptr)\n";
    preamble << "declare i64 @xxml_reflection_property_getOffset(ptr)\n";
    preamble << "declare ptr @xxml_reflection_method_getName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_method_getReturnType(ptr)\n";
    preamble << "declare i64 @xxml_reflection_method_getReturnOwnership(ptr)\n";
    preamble << "declare i64 @xxml_reflection_method_getParameterCount(ptr)\n";
    preamble << "declare ptr @xxml_reflection_method_getParameter(ptr, i64)\n";
    preamble << "declare i64 @xxml_reflection_method_isStatic(ptr)\n";
    preamble << "declare i64 @xxml_reflection_method_isConstructor(ptr)\n";
    preamble << "declare ptr @xxml_reflection_parameter_getName(ptr)\n";
    preamble << "declare ptr @xxml_reflection_parameter_getTypeName(ptr)\n";
    preamble << "declare i64 @xxml_reflection_parameter_getOwnership(ptr)\n";
    preamble << "\n";

    // Annotation runtime functions
    preamble << "; Annotation Runtime Functions\n";
    preamble << "declare void @Annotation_registerForType(ptr, i32, ptr)\n";
    preamble << "declare void @Annotation_registerForMethod(ptr, ptr, i32, ptr)\n";
    preamble << "declare void @Annotation_registerForProperty(ptr, ptr, i32, ptr)\n";
    preamble << "declare i32 @Annotation_getCountForType(ptr)\n";
    preamble << "declare ptr @Annotation_getForType(ptr, i32)\n";
    preamble << "declare i32 @Annotation_getCountForMethod(ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getForMethod(ptr, ptr, i32)\n";
    preamble << "declare i32 @Annotation_getCountForProperty(ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getForProperty(ptr, ptr, i32)\n";
    preamble << "declare i1 @Annotation_typeHas(ptr, ptr)\n";
    preamble << "declare i1 @Annotation_methodHas(ptr, ptr, ptr)\n";
    preamble << "declare i1 @Annotation_propertyHas(ptr, ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getByNameForType(ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getByNameForMethod(ptr, ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getByNameForProperty(ptr, ptr, ptr)\n";
    preamble << "declare ptr @Annotation_getArgument(ptr, ptr)\n";
    preamble << "declare i64 @Annotation_getIntArg(ptr, ptr, i64)\n";
    preamble << "declare ptr @Annotation_getStringArg(ptr, ptr, ptr)\n";
    preamble << "declare i1 @Annotation_getBoolArg(ptr, ptr, i1)\n";
    preamble << "declare double @Annotation_getDoubleArg(ptr, ptr, double)\n";
    preamble << "\n";

    // Language::Reflection::AnnotationInfo class bindings
    preamble << "; Annotation Info Language Bindings\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_Constructor(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_getName(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_getArgumentCount(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_getArgument(ptr, ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_getArgumentByName(ptr, ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationInfo_hasArgument(ptr, ptr)\n";
    preamble << "\n";

    // Language::Reflection::AnnotationArg class bindings
    preamble << "; Annotation Arg Language Bindings\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_Constructor(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_getName(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_getType(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_asInteger(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_asString(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_asBool(ptr)\n";
    preamble << "declare ptr @Language_Reflection_AnnotationArg_asDouble(ptr)\n";
    preamble << "\n";

    // Annotation Processor API bindings (for user-defined processors)
    preamble << "; Annotation Processor API\n";
    // ReflectionContext basic methods
    preamble << "declare ptr @Processor_getTargetKind(ptr)\n";
    preamble << "declare ptr @Processor_getTargetName(ptr)\n";
    preamble << "declare ptr @Processor_getTypeName(ptr)\n";
    preamble << "declare ptr @Processor_getClassName(ptr)\n";
    preamble << "declare ptr @Processor_getNamespaceName(ptr)\n";
    preamble << "declare ptr @Processor_getSourceFile(ptr)\n";
    preamble << "declare i64 @Processor_getLineNumber(ptr)\n";
    preamble << "declare i64 @Processor_getColumnNumber(ptr)\n";
    // ReflectionContext class inspection methods
    preamble << "declare i64 @Processor_getPropertyCount(ptr)\n";
    preamble << "declare ptr @Processor_getPropertyNameAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getPropertyTypeAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getPropertyOwnershipAt(ptr, i64)\n";
    preamble << "declare i64 @Processor_getMethodCount(ptr)\n";
    preamble << "declare ptr @Processor_getMethodNameAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getMethodReturnTypeAt(ptr, i64)\n";
    preamble << "declare i64 @Processor_hasMethod(ptr, ptr)\n";
    preamble << "declare i64 @Processor_hasProperty(ptr, ptr)\n";
    preamble << "declare ptr @Processor_getBaseClassName(ptr)\n";
    preamble << "declare i64 @Processor_isClassFinal(ptr)\n";
    // ReflectionContext method inspection methods
    preamble << "declare i64 @Processor_getParameterCount(ptr)\n";
    preamble << "declare ptr @Processor_getParameterNameAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getParameterTypeAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getReturnTypeName(ptr)\n";
    preamble << "declare i64 @Processor_isMethodStatic(ptr)\n";
    // ReflectionContext property inspection methods
    preamble << "declare i64 @Processor_hasDefaultValue(ptr)\n";
    preamble << "declare ptr @Processor_getOwnership(ptr)\n";
    // Target value access
    preamble << "declare ptr @Processor_getTargetValue(ptr)\n";
    preamble << "declare i64 @Processor_hasTargetValue(ptr)\n";
    preamble << "declare i64 @Processor_getTargetValueType(ptr)\n";
    // CompilationContext methods
    preamble << "declare void @Processor_message(ptr, ptr)\n";
    preamble << "declare void @Processor_warning(ptr, ptr)\n";
    preamble << "declare void @Processor_warningAt(ptr, ptr, ptr, i64, i64)\n";
    preamble << "declare void @Processor_error(ptr, ptr)\n";
    preamble << "declare void @Processor_errorAt(ptr, ptr, ptr, i64, i64)\n";
    // AnnotationArg methods
    preamble << "declare ptr @Processor_argGetName(ptr)\n";
    preamble << "declare i64 @Processor_argAsInt(ptr)\n";
    preamble << "declare ptr @Processor_argAsString(ptr)\n";
    preamble << "declare i64 @Processor_argAsBool(ptr)\n";
    preamble << "declare double @Processor_argAsDouble(ptr)\n";
    // AnnotationArgs methods
    preamble << "declare i64 @Processor_getArgCount(ptr)\n";
    preamble << "declare ptr @Processor_getArgAt(ptr, i64)\n";
    preamble << "declare ptr @Processor_getArg(ptr, ptr)\n";
    // Annotation argument access through ReflectionContext
    preamble << "declare i64 @Processor_getAnnotationArgCount(ptr)\n";
    preamble << "declare ptr @Processor_getAnnotationArg(ptr, ptr)\n";  // Type-aware generic access
    preamble << "declare ptr @Processor_getAnnotationArgNameAt(ptr, i64)\n";
    preamble << "declare i64 @Processor_getAnnotationArgTypeAt(ptr, i64)\n";
    preamble << "declare i64 @Processor_hasAnnotationArg(ptr, ptr)\n";
    preamble << "declare i64 @Processor_getAnnotationIntArg(ptr, ptr, i64)\n";
    preamble << "declare ptr @Processor_getAnnotationStringArg(ptr, ptr, ptr)\n";
    preamble << "declare i64 @Processor_getAnnotationBoolArg(ptr, ptr, i64)\n";
    preamble << "declare double @Processor_getAnnotationDoubleArg(ptr, ptr, double)\n";
    preamble << "\n";

    // Dynamic value methods (for __DynamicValue type from processor target values)
    // These perform runtime type dispatch to call the appropriate method
    preamble << "; Dynamic Value Methods (runtime type dispatch)\n";
    preamble << "declare ptr @__DynamicValue_toString(ptr)\n";
    preamble << "declare i1 @__DynamicValue_greaterThan(ptr, ptr)\n";
    preamble << "declare i1 @__DynamicValue_lessThan(ptr, ptr)\n";
    preamble << "declare i1 @__DynamicValue_equals(ptr, ptr)\n";
    preamble << "declare ptr @__DynamicValue_add(ptr, ptr)\n";
    preamble << "declare ptr @__DynamicValue_sub(ptr, ptr)\n";
    preamble << "declare ptr @__DynamicValue_mul(ptr, ptr)\n";
    preamble << "declare ptr @__DynamicValue_div(ptr, ptr)\n";
    preamble << "\n";

    // Utility functions for Syscall:: namespace (xxml_ prefixed)
    preamble << "; Utility Functions\n";
    preamble << "declare ptr @xxml_string_create(ptr)\n";
    preamble << "declare ptr @xxml_string_concat(ptr, ptr)\n";
    preamble << "declare i64 @xxml_string_hash(ptr)\n";
    preamble << "declare i64 @xxml_ptr_is_null(ptr)\n";
    preamble << "declare ptr @xxml_ptr_null()\n";
    preamble << "\n";

    // Threading runtime functions (xxml_ prefix added by Syscall:: translation)
    preamble << "; Threading functions\n";
    preamble << "declare ptr @xxml_Thread_create(ptr, ptr)\n";
    preamble << "declare i64 @xxml_Thread_join(ptr)\n";
    preamble << "declare i64 @xxml_Thread_detach(ptr)\n";
    preamble << "declare i1 @xxml_Thread_isJoinable(ptr)\n";
    preamble << "declare void @xxml_Thread_sleep(i64)\n";
    preamble << "declare void @xxml_Thread_yield()\n";
    preamble << "declare i64 @xxml_Thread_currentId()\n";
    preamble << "declare ptr @xxml_Thread_spawn_lambda(ptr)\n";
    preamble << "\n";

    // Mutex runtime functions
    preamble << "; Mutex functions\n";
    preamble << "declare ptr @xxml_Mutex_create()\n";
    preamble << "declare void @xxml_Mutex_destroy(ptr)\n";
    preamble << "declare i64 @xxml_Mutex_lock(ptr)\n";
    preamble << "declare i64 @xxml_Mutex_unlock(ptr)\n";
    preamble << "declare i1 @xxml_Mutex_tryLock(ptr)\n";
    preamble << "\n";

    // Condition variable runtime functions
    preamble << "; Condition variable functions\n";
    preamble << "declare ptr @xxml_CondVar_create()\n";
    preamble << "declare void @xxml_CondVar_destroy(ptr)\n";
    preamble << "declare i64 @xxml_CondVar_wait(ptr, ptr)\n";
    preamble << "declare i64 @xxml_CondVar_waitTimeout(ptr, ptr, i64)\n";
    preamble << "declare i64 @xxml_CondVar_signal(ptr)\n";
    preamble << "declare i64 @xxml_CondVar_broadcast(ptr)\n";
    preamble << "\n";

    // Atomic runtime functions
    preamble << "; Atomic functions\n";
    preamble << "declare ptr @xxml_Atomic_create(i64)\n";
    preamble << "declare void @xxml_Atomic_destroy(ptr)\n";
    preamble << "declare i64 @xxml_Atomic_load(ptr)\n";
    preamble << "declare void @xxml_Atomic_store(ptr, i64)\n";
    preamble << "declare i64 @xxml_Atomic_add(ptr, i64)\n";
    preamble << "declare i64 @xxml_Atomic_sub(ptr, i64)\n";
    preamble << "declare i1 @xxml_Atomic_compareAndSwap(ptr, i64, i64)\n";
    preamble << "declare i64 @xxml_Atomic_exchange(ptr, i64)\n";
    preamble << "\n";

    // Thread-local storage runtime functions
    preamble << "; TLS functions\n";
    preamble << "declare ptr @xxml_TLS_create()\n";
    preamble << "declare void @xxml_TLS_destroy(ptr)\n";
    preamble << "declare ptr @xxml_TLS_get(ptr)\n";
    preamble << "declare void @xxml_TLS_set(ptr, ptr)\n";
    preamble << "\n";

    // File I/O runtime functions
    preamble << "; File I/O functions\n";
    preamble << "declare ptr @xxml_File_open(ptr, ptr)\n";
    preamble << "declare void @xxml_File_close(ptr)\n";
    preamble << "declare i64 @xxml_File_read(ptr, ptr, i64)\n";
    preamble << "declare i64 @xxml_File_write(ptr, ptr, i64)\n";
    preamble << "declare ptr @xxml_File_readLine(ptr)\n";
    preamble << "declare i64 @xxml_File_writeString(ptr, ptr)\n";
    preamble << "declare i64 @xxml_File_writeLine(ptr, ptr)\n";
    preamble << "declare ptr @xxml_File_readAll(ptr)\n";
    preamble << "declare i64 @xxml_File_seek(ptr, i64, i64)\n";
    preamble << "declare i64 @xxml_File_tell(ptr)\n";
    preamble << "declare i64 @xxml_File_size(ptr)\n";
    preamble << "declare i1 @xxml_File_eof(ptr)\n";
    preamble << "declare i64 @xxml_File_flush(ptr)\n";
    preamble << "declare i1 @xxml_File_exists(ptr)\n";
    preamble << "declare i1 @xxml_File_delete(ptr)\n";
    preamble << "declare i1 @xxml_File_rename(ptr, ptr)\n";
    preamble << "declare i1 @xxml_File_copy(ptr, ptr)\n";
    preamble << "declare i64 @xxml_File_sizeByPath(ptr)\n";
    preamble << "declare ptr @xxml_File_readAllByPath(ptr)\n";
    preamble << "\n";

    // Directory functions
    preamble << "; Directory functions\n";
    preamble << "declare i1 @xxml_Dir_create(ptr)\n";
    preamble << "declare i1 @xxml_Dir_exists(ptr)\n";
    preamble << "declare i1 @xxml_Dir_delete(ptr)\n";
    preamble << "declare ptr @xxml_Dir_getCurrent()\n";
    preamble << "declare i1 @xxml_Dir_setCurrent(ptr)\n";
    preamble << "\n";

    // Path utility functions
    preamble << "; Path utility functions\n";
    preamble << "declare ptr @xxml_Path_join(ptr, ptr)\n";
    preamble << "declare ptr @xxml_Path_getFileName(ptr)\n";
    preamble << "declare ptr @xxml_Path_getDirectory(ptr)\n";
    preamble << "declare ptr @xxml_Path_getExtension(ptr)\n";
    preamble << "declare i1 @xxml_Path_isAbsolute(ptr)\n";
    preamble << "declare ptr @xxml_Path_getAbsolute(ptr)\n";
    preamble << "\n";

    // Optimization attributes
    preamble << "; Optimization attributes\n";
    preamble << "attributes #0 = { noinline nounwind optnone uwtable }\n";
    preamble << "attributes #1 = { nounwind uwtable }\n";
    // FFI (Foreign Function Interface) Runtime
    preamble << "; FFI Runtime Functions\n";
    preamble << "declare ptr @xxml_FFI_loadLibrary(ptr)\n";
    preamble << "declare ptr @xxml_FFI_getSymbol(ptr, ptr)\n";
    preamble << "declare void @xxml_FFI_freeLibrary(ptr)\n";
    preamble << "declare ptr @xxml_FFI_getError()\n";
    preamble << "declare i1 @xxml_FFI_libraryExists(ptr)\n";
    preamble << "\n";

    // FFI Type Conversion Functions
    preamble << "; FFI Type Conversion\n";
    preamble << "declare ptr @xxml_FFI_stringToCString(ptr)\n";
    preamble << "declare ptr @xxml_FFI_cstringToString(ptr)\n";
    preamble << "declare i64 @xxml_FFI_integerToInt64(ptr)\n";
    preamble << "declare ptr @xxml_FFI_int64ToInteger(i64)\n";
    preamble << "declare float @xxml_FFI_floatToC(ptr)\n";
    preamble << "declare ptr @xxml_FFI_cToFloat(float)\n";
    preamble << "declare double @xxml_FFI_doubleToC(ptr)\n";
    preamble << "declare ptr @xxml_FFI_cToDouble(double)\n";
    preamble << "declare i1 @xxml_FFI_boolToC(ptr)\n";
    preamble << "declare ptr @xxml_FFI_cToBool(i1)\n";
    preamble << "\n";

    // Windows DLL loading (for direct dllimport if needed)
    preamble << "; Windows kernel32 functions for FFI\n";
    preamble << "declare dllimport ptr @LoadLibraryA(ptr) #1\n";
    preamble << "declare dllimport ptr @GetProcAddress(ptr, ptr) #1\n";
    preamble << "declare dllimport i32 @FreeLibrary(ptr) #1\n";
    preamble << "\n";

    preamble << "attributes #2 = { alwaysinline nounwind uwtable }\n";
    preamble << "\n";

    return preamble.str();
}

void LLVMBackend::initializeIRModule() {
    if (!module_ || !builder_) return;
    auto& ctx = module_->getContext();

    // Create built-in struct types
    IR::StructType* integerTy = module_->createStructType("Integer");
    integerTy->setBody({ctx.getInt64Ty()});
    IR::StructType* stringTy = module_->createStructType("String");
    stringTy->setBody({ctx.getPtrTy(), ctx.getInt64Ty()});
    IR::StructType* boolTy = module_->createStructType("Bool");
    boolTy->setBody({ctx.getInt1Ty()});
    IR::StructType* floatTy = module_->createStructType("Float");
    floatTy->setBody({ctx.getFloatTy()});
    IR::StructType* doubleTy = module_->createStructType("Double");
    doubleTy->setBody({ctx.getDoubleTy()});

    // Declare runtime functions
    auto declareFunc = [&](const std::string& name, IR::Type* retTy, std::vector<IR::Type*> paramTys) {
        auto* funcTy = ctx.getFunctionTy(retTy, paramTys, false);
        module_->declareFunction(name, funcTy);
    };

    IR::Type* ptrTy = ctx.getPtrTy();
    IR::Type* i1Ty = ctx.getInt1Ty();
    IR::Type* i32Ty = ctx.getInt32Ty();
    IR::Type* i64Ty = ctx.getInt64Ty();
    IR::Type* voidTy = ctx.getVoidTy();

    // Memory Management
    declareFunc("xxml_malloc", ptrTy, {i64Ty});
    declareFunc("xxml_free", voidTy, {ptrTy});

    // Integer Operations
    declareFunc("Integer_Constructor", ptrTy, {i64Ty});
    declareFunc("Integer_getValue", i64Ty, {ptrTy});
    declareFunc("Integer_add", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_sub", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_mul", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_div", ptrTy, {ptrTy, ptrTy});
    declareFunc("Integer_lt", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_le", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_gt", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_ge", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_eq", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_ne", i1Ty, {ptrTy, ptrTy});
    declareFunc("Integer_toString", ptrTy, {ptrTy});

    // String Operations
    declareFunc("String_Constructor", ptrTy, {ptrTy});
    declareFunc("String_concat", ptrTy, {ptrTy, ptrTy});
    declareFunc("String_equals", i1Ty, {ptrTy, ptrTy});
    declareFunc("String_length", i64Ty, {ptrTy});

    // Bool Operations
    declareFunc("Bool_Constructor", ptrTy, {i1Ty});
    declareFunc("Bool_getValue", i1Ty, {ptrTy});
    declareFunc("Bool_not", ptrTy, {ptrTy});
    declareFunc("Bool_and", ptrTy, {ptrTy, ptrTy});
    declareFunc("Bool_or", ptrTy, {ptrTy, ptrTy});

    // Console I/O
    declareFunc("Console_print", voidTy, {ptrTy});
    declareFunc("Console_printLine", voidTy, {ptrTy});

    // System Functions
    declareFunc("xxml_exit", voidTy, {i32Ty});
    declareFunc("xxml_string_concat", ptrTy, {ptrTy, ptrTy});
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
    // by looking at the reflection metadata
    std::string qualifiedName = currentNamespace_ + "::" + baseType;
    if (reflectionMetadata_.find(qualifiedName) != reflectionMetadata_.end()) {
        return qualifiedName + suffix;
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

// Template helper methods
std::string LLVMBackend::mangleTemplateName(const std::string& baseName,
                                            const std::vector<std::string>& typeArgs) const {
    if (typeArgs.empty()) {
        return baseName;
    }

    std::string mangled = baseName;
    for (const auto& arg : typeArgs) {
        // Replace problematic characters in type names using TypeNormalizer
        std::string cleanArg = TypeNormalizer::mangleForLLVM(arg);

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
    emitLine(format("; import {}", node.modulePath));

    // IR: Import marker (compile-time only)
    if (builder_) {
        (void)builder_->getVoidTy();  // Access type to maintain IR state
    }
}

void LLVMBackend::visit(Parser::NamespaceDecl& node) {
    emitLine(format("; namespace {}", node.name));

    // IR: Namespace declaration marker
    if (builder_) {
        (void)builder_->getInt8Ty();  // Access type to maintain IR state
    }

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
    std::cerr.flush();

    // DEBUG: Track recursion depth
    static int classDepth = 0;
    classDepth++;
    if (classDepth > 50) {
        std::cerr << "ERROR: ClassDecl recursion depth exceeded 50! Class: " << node.name << "\n";
        classDepth--;
        return;
    }
    struct ClassDepthGuard { ~ClassDepthGuard() { classDepth--; } } classGuard;

    // Skip template declarations (only generate code for instantiations)
    // Template declarations have templateParams but no '<' in name (e.g., "SomeClass<T>")
    // Template instantiations have '<' in name (e.g., "SomeClass<Integer>")
    if (!node.templateParams.empty() && node.name.find('<') == std::string::npos) {
        return;  // This is a template declaration, skip it - monomorphization handles it
    }

    // Check if this is a template instantiation (avoid duplicate generation)
    if (node.name.find('<') != std::string::npos) {
        std::string mangledName = TypeNormalizer::mangleForLLVM(node.name);

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

    // Check for duplicate class generation (can happen with imported modules)
    // Use the qualified class name to check for duplicates
    if (generatedClasses_.find(currentClassName_) != generatedClasses_.end()) {
        // Class already generated, skip to avoid redefinition errors
        currentClassName_ = "";
        return;
    }
    generatedClasses_.insert(currentClassName_);

    // Create class info entry - use currentClassName_ which includes namespace
    ClassInfo& classInfo = classes_[currentClassName_];
    classInfo.properties.clear();

    // Collect retained annotations for this class
    collectRetainedAnnotations(node);

    // First pass: collect properties from all access sections
    for (auto& section : node.sections) {
        if (!section) {
            continue;
        }
        for (auto& decl : section->declarations) {
            if (!decl) {
                continue;
            }
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                std::string llvmType = getLLVMType(prop->type->typeName);
                std::string xxmlType = prop->type->typeName;  // Store original XXML type
                classInfo.properties.push_back(std::make_tuple(prop->name, llvmType, xxmlType));
            }
        }
    }

    // Generate struct type definition
    std::stringstream structDef;
    // Mangle class name to remove :: (invalid in LLVM type names) using TypeNormalizer
    // Use currentClassName_ which includes namespace, not node.name
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(currentClassName_);
    structDef << "%class." << mangledClassName << " = type { ";
    for (size_t i = 0; i < classInfo.properties.size(); ++i) {
        if (i > 0) structDef << ", ";
        structDef << std::get<1>(classInfo.properties[i]);  // llvmType
    }
    if (classInfo.properties.empty()) {
        structDef << "i8";  // Empty struct needs at least one field in LLVM
    }
    structDef << " }";
    emitLine(structDef.str());
    emitLine("");

    // IR: Create struct type for class
    if (module_) {
        std::vector<IR::Type*> fieldTypes;
        for (const auto& prop : classInfo.properties) {
            const std::string& llvmType = std::get<1>(prop);
            if (llvmType == "ptr") {
                fieldTypes.push_back(builder_->getPtrTy());
            } else if (llvmType == "i64") {
                fieldTypes.push_back(builder_->getInt64Ty());
            } else if (llvmType == "i32") {
                fieldTypes.push_back(builder_->getInt32Ty());
            } else if (llvmType == "float") {
                fieldTypes.push_back(builder_->getFloatTy());
            } else if (llvmType == "double") {
                fieldTypes.push_back(builder_->getDoubleTy());
            } else {
                fieldTypes.push_back(builder_->getPtrTy());  // Default
            }
        }
        if (fieldTypes.empty()) {
            fieldTypes.push_back(builder_->getInt8Ty());  // Empty struct
        }
        module_->createStructType("class." + mangledClassName);
    }

    // Second pass: generate methods and constructors
    bool hasConstructor = false;
    for (auto& section : node.sections) {
        if (!section) {
            continue;
        }
        // Check if this section has a constructor
        for (const auto& decl : section->declarations) {
            if (!decl) continue;
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

        // IR: Create default constructor function with return
        if (useNewIR_ && builder_ && module_) {
            std::vector<std::pair<std::string, IR::Type*>> irParams;
            irParams.push_back({"this", builder_->getPtrTy()});
            auto* ctorFunc = createIRFunction(funcName, builder_->getPtrTy(), irParams);
            if (ctorFunc) {
                auto* entryBlock = ctorFunc->createBasicBlock("entry");
                builder_->setInsertPoint(entryBlock);
                builder_->CreateRet(ctorFunc->getArg(0));  // Return 'this'
            }
        }

        // IR: Create default constructor function
        if (useNewIR_ && builder_ && module_) {
            std::vector<std::pair<std::string, IR::Type*>> irParams;
            irParams.push_back({"this", builder_->getPtrTy()});
            auto* defCtorFunc = createIRFunction(funcName, builder_->getPtrTy(), irParams);
            if (defCtorFunc) {
                auto* entryBlock = defCtorFunc->createBasicBlock("entry");
                builder_->setInsertPoint(entryBlock);
                builder_->CreateRet(defCtorFunc->getArg(0));
            }
        }
    }

    // Collect reflection metadata for this class
    ReflectionClassMetadata metadata;
    metadata.name = node.name;
    metadata.namespaceName = currentNamespace_;
    if (!currentNamespace_.empty()) {
        metadata.fullName = currentNamespace_ + "::" + node.name;
    } else {
        metadata.fullName = node.name;
    }
    metadata.isTemplate = !node.templateParams.empty();
    for (const auto& param : node.templateParams) {
        metadata.templateParams.push_back(param.name);
    }
    metadata.instanceSize = calculateClassSize(node.name);
    metadata.astNode = &node;

    // Helper to convert OwnershipType to string character
    auto ownershipToString = [](Parser::OwnershipType ownership) -> std::string {
        switch (ownership) {
            case Parser::OwnershipType::Owned: return "^";
            case Parser::OwnershipType::Reference: return "&";
            case Parser::OwnershipType::Copy: return "%";
            case Parser::OwnershipType::None: return "";
            default: return "";
        }
    };

    // Collect properties with ownership
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                metadata.properties.push_back({prop->name, prop->type->typeName});
                metadata.propertyOwnerships.push_back(ownershipToString(prop->type->ownership));
            }
        }
    }

    // Collect methods with parameters
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                metadata.methods.push_back({method->name, method->returnType ? method->returnType->typeName : "None"});
                metadata.methodReturnOwnerships.push_back(method->returnType ? ownershipToString(method->returnType->ownership) : "");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : method->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);
            } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Constructors are special methods
                metadata.methods.push_back({"Constructor", currentClassName_});
                metadata.methodReturnOwnerships.push_back("^");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : ctor->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);
            }
        }
    }

    // Store metadata for later generation
    reflectionMetadata_[metadata.fullName] = metadata;

    // Clear current class context
    currentClassName_ = "";
    emitLine("");
}

void LLVMBackend::visit(Parser::NativeStructureDecl& node) {
    // For now, NativeStructure just generates a struct type definition
    // It's a C-compatible struct with fixed layout for FFI
    emitLine("; NativeStructure " + node.name + " (alignment: " + std::to_string(node.alignment) + ")");

    // Generate LLVM struct type
    // e.g., %POINT = type { i32, i32 }
    std::string structDef = "%" + node.name + " = type { ";
    bool first = true;
    for (const auto& prop : node.properties) {
        if (!first) structDef += ", ";
        first = false;

        // Map NativeType to LLVM type
        if (prop->type->typeName.find("NativeType<") == 0) {
            std::string nativeType = prop->type->typeName;
            size_t start = nativeType.find("\"") + 1;
            size_t end = nativeType.rfind("\"");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                std::string typeName = nativeType.substr(start, end - start);
                structDef += mapNativeTypeToLLVM(typeName);
            } else {
                structDef += "ptr";  // Default to pointer
            }
        } else {
            structDef += "ptr";  // Non-native types are pointers
        }
    }
    structDef += " }";
    emitLine(structDef);

    // IR: Verify struct type created
    if (builder_) {
        (void)builder_->getInt64Ty();
    }

    emitLine("");

    // IR: Create struct type for NativeStructure
    if (module_) {
        std::string mangledName = TypeNormalizer::mangleForLLVM(node.name);
        module_->createStructType("class." + mangledName);
    }
}

void LLVMBackend::visit(Parser::CallbackTypeDecl& node) {
    // Generate callback type info and thunk function for FFI callbacks
    std::string fullName = currentNamespace_.empty() ? node.name : currentNamespace_ + "::" + node.name;

    emitLine("; CallbackType " + fullName);

    // Build thunk info
    CallbackThunkInfo thunkInfo;
    thunkInfo.callbackTypeName = fullName;
    thunkInfo.thunkFunctionName = "@xxml_callback_thunk_" + std::to_string(callbackThunkCounter_++);
    thunkInfo.convention = node.convention;

    // Convert return type
    if (node.returnType) {
        thunkInfo.returnLLVMType = getLLVMType(node.returnType->typeName);
    } else {
        thunkInfo.returnLLVMType = "void";
    }

    // Convert parameter types
    for (const auto& param : node.parameters) {
        if (param->type) {
            thunkInfo.paramLLVMTypes.push_back(getLLVMType(param->type->typeName));
            thunkInfo.paramNames.push_back(param->name);
        }
    }

    // Store in registry
    callbackThunks_[fullName] = thunkInfo;

    // Generate the callback thunk function
    generateCallbackThunk(fullName);

    emitLine("");
}

std::string LLVMBackend::getLLVMCallingConvention(Parser::CallingConvention conv) const {
    switch (conv) {
        case Parser::CallingConvention::CDecl:
            return "ccc";  // C calling convention (default)
        case Parser::CallingConvention::StdCall:
            return "x86_stdcallcc";  // Win32 stdcall
        case Parser::CallingConvention::FastCall:
            return "x86_fastcallcc";  // fastcall
        default:
            return "ccc";
    }
}

void LLVMBackend::generateCallbackThunk(const std::string& callbackTypeName) {
    auto it = callbackThunks_.find(callbackTypeName);
    if (it == callbackThunks_.end()) {
        emitLine("; ERROR: Unknown callback type: " + callbackTypeName);
        return;
    }

    const CallbackThunkInfo& info = it->second;

    // Mangle the callback type name for the global context variable using TypeNormalizer
    std::string mangledName = TypeNormalizer::mangleForLLVM(callbackTypeName);

    // Declare global variable to hold the lambda closure pointer
    std::string closureGlobalName = "@xxml_callback_closure_" + mangledName;
    emitLine(closureGlobalName + " = global ptr null");

    // Generate the thunk function with the native calling convention
    std::string callingConv = getLLVMCallingConvention(info.convention);

    // Build parameter list for thunk function
    std::stringstream params;
    for (size_t i = 0; i < info.paramLLVMTypes.size(); ++i) {
        if (i > 0) params << ", ";
        params << info.paramLLVMTypes[i] << " %param" << i;
    }

    // Function definition with calling convention
    emitLine("define " + callingConv + " " + info.returnLLVMType + " " +
             info.thunkFunctionName + "(" + params.str() + ") {");
    emitLine("entry:");

    // Load the closure pointer from global
    std::string closureReg = allocateRegister();
    emitLine("  " + closureReg + " = load ptr, ptr " + closureGlobalName);

    // Check if closure is null (safety check)
    std::string isNullReg = allocateRegister();
    emitLine("  " + isNullReg + " = icmp eq ptr " + closureReg + ", null");
    std::string labelIfNull = allocateLabel("if_null");
    std::string labelIfValid = allocateLabel("if_valid");
    emitLine("  br i1 " + isNullReg + ", label %" + labelIfNull + ", label %" + labelIfValid);

    // If null, return default value
    emitLine(labelIfNull + ":");
    if (info.returnLLVMType == "void") {
        emitLine("  ret void");
    } else {
        emitLine("  ret " + info.returnLLVMType + " " + getDefaultValueForType(info.returnLLVMType));
    }

    // If valid, call the lambda
    emitLine(labelIfValid + ":");

    // Load the lambda function pointer from the closure
    // Lambda closures store: { ptr function, ...captures }
    // The function pointer is at offset 0
    std::string funcPtrReg = allocateRegister();
    emitLine("  " + funcPtrReg + " = load ptr, ptr " + closureReg);

    // Build argument list for lambda call
    // Lambda expects: (closure_ptr, param0, param1, ...)
    std::stringstream callArgs;
    callArgs << "ptr " << closureReg;  // First arg is closure itself
    for (size_t i = 0; i < info.paramLLVMTypes.size(); ++i) {
        callArgs << ", " << info.paramLLVMTypes[i] << " %param" << i;
    }

    // Build the lambda function type
    std::stringstream funcType;
    funcType << info.returnLLVMType << " (ptr";  // closure ptr
    for (const auto& paramType : info.paramLLVMTypes) {
        funcType << ", " << paramType;
    }
    funcType << ")";

    // Call the lambda function
    if (info.returnLLVMType == "void") {
        emitLine("  call " + funcType.str() + " " + funcPtrReg + "(" + callArgs.str() + ")");
        emitLine("  ret void");
    } else {
        std::string resultReg = allocateRegister();
        emitLine("  " + resultReg + " = call " + funcType.str() + " " + funcPtrReg + "(" + callArgs.str() + ")");
        emitLine("  ret " + info.returnLLVMType + " " + resultReg);
    }

    emitLine("}");
}

void LLVMBackend::visit(Parser::EnumValueDecl& node) {
    // Individual enum values are handled by EnumerationDecl
}

void LLVMBackend::visit(Parser::EnumerationDecl& node) {
    // Register all enum values in the enum registry
    std::string enumName = currentNamespace_.empty() ? node.name : currentNamespace_ + "::" + node.name;

    emitLine("; Enumeration " + enumName);
    for (const auto& val : node.values) {
        std::string key = enumName + "::" + val->name;
        enumValues_[key] = val->value;
        emitLine(";   " + val->name + " = " + std::to_string(val->value));
    }
    emitLine("");
}

void LLVMBackend::visit(Parser::AccessSection& node) {
    for (auto& decl : node.declarations) {
        if (!decl) {
            continue;
        }
        // Debug: identify which type of declaration
        if (dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
        } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
        } else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
        } else {
        }
        decl->accept(*this);
    }
}

void LLVMBackend::visit(Parser::PropertyDecl& node) {
    // Collect retained annotations for this property
    if (!currentClassName_.empty()) {
        collectRetainedAnnotations(node, currentClassName_);
    }

    // Properties are already collected in ClassDecl
    // Nothing else to emit here
}

void LLVMBackend::visit(Parser::ConstructorDecl& node) {
    // Clear local variables from previous method
    variables_.clear();
    valueMap_.clear();  // Also clear valueMap to avoid stale entries
    registerTypes_.clear();  // Clear register types to avoid stale type information

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
        // Use qualifyTypeName to get the full namespace-qualified type
        valueMap_[param->name] = "%" + param->name;
        registerTypes_["%" + param->name] = qualifyTypeName(param->type->typeName);
    }

    // Mangle constructor name to support overloading
    // LLVM doesn't support function overloading, so add parameter count suffix
    // BUT skip mangling for standard library types (declared in preamble or runtime)
    static const std::unordered_set<std::string> builtinTypes = {
        "Integer", "String", "Bool", "Float", "Double", "None"
    };

    // Extract base class name (without namespace/template)
    std::string baseClassName = currentClassName_;
    size_t colonPos = baseClassName.rfind("::");
    if (colonPos != std::string::npos) {
        baseClassName = baseClassName.substr(colonPos + 2);
    }
    size_t templatePos = baseClassName.find('_');
    if (templatePos != std::string::npos) {
        baseClassName = baseClassName.substr(0, templatePos);
    }

    if (builtinTypes.find(baseClassName) == builtinTypes.end()) {
        // Only mangle user-defined types, not built-in types
        funcName += "_" + std::to_string(node.parameters.size());
    }

    // Check for duplicate function definition (can happen with imported modules)
    if (generatedFunctions_.find(funcName) != generatedFunctions_.end()) {
        // Constructor already generated, skip to avoid redefinition error
        return;
    }
    generatedFunctions_.insert(funcName);

    // Store 'this' in value map
    valueMap_["this"] = "%this";

    // Constructors return the initialized object pointer
    currentFunctionReturnType_ = "ptr";  // Track for return statements
    emitLine("; Constructor for " + currentClassName_);
    emitLine("define ptr @" + funcName + "(" + params.str() + ") #1 {");
    indent();

    // IR generation: Create constructor function
    if (useNewIR_ && builder_ && module_) {
        std::vector<std::pair<std::string, IR::Type*>> irParams;
        irParams.push_back({"this", builder_->getPtrTy()});
        for (auto& param : node.parameters) {
            irParams.push_back({param->name, getIRType(param->type->typeName)});
        }
        auto* ctorFunc = createIRFunction(funcName, builder_->getPtrTy(), irParams);
        if (ctorFunc) {
            auto* entryBlock = ctorFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entryBlock);
            currentFunction_ = ctorFunc;
            builder_->setCurrentFunction(currentFunction_);  // For type validation
            currentBlock_ = entryBlock;
            // Map parameters to IR values
            for (size_t i = 0; i < ctorFunc->getNumArgs(); ++i) {
                irValues_[ctorFunc->getArg(i)->getName()] = ctorFunc->getArg(i);
            }
        }
    }

    // CRITICAL FIX: For default constructors (empty body), initialize all fields to zero/null
    if (node.body.empty()) {
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            const ClassInfo& classInfo = classIt->second;
            int fieldIndex = 0;

            // Mangle class name for LLVM type using TypeNormalizer
            std::string mangledClassName = TypeNormalizer::mangleForLLVM(currentClassName_);

            for (const auto& prop : classInfo.properties) {
                // prop is tuple<propName, llvmType, xxmlType>
                const std::string& propName = std::get<0>(prop);
                const std::string& propType = std::get<1>(prop);  // This is already LLVM type

                // Get pointer to field
                std::string fieldPtrReg = allocateRegister();
                emitLine(format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                   fieldPtrReg, mangledClassName, fieldIndex));

                // The propType is already the LLVM type from class registration
                std::string llvmType = propType;

                // Get type-appropriate default value (null for pointers, 0 for integers, etc.)
                std::string defaultValue = getDefaultValueForType(llvmType);

                // Initialize field with type-safe default value
                emitLine(format("store {}, ptr {}", defaultValue, fieldPtrReg));

                // IR: GEP to field and store default value
                if (builder_ && currentFunction_) {
                    IR::Value* thisPtr = currentFunction_->getArg(0);
                    if (thisPtr) {
                        IR::Value* fieldPtr = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(fieldIndex), propName + "_init_ptr");
                        IR::Type* fieldTy = (llvmType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                        IR::Value* defVal = (llvmType == "ptr") ? static_cast<IR::Value*>(builder_->getNullPtr()) : static_cast<IR::Value*>(builder_->getInt64(0));
                        builder_->CreateStore(defVal, fieldPtr);
                    }
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

    // IR: Create return instruction
    if (builder_ && currentBlock_ && !currentBlock_->getTerminator() && currentFunction_) {
        builder_->CreateRet(currentFunction_->getArg(0));  // Return 'this'
    }

    dedent();
    emitLine("}");
    emitLine("");

    // Clear parameter mappings
    for (auto& param : node.parameters) {
        valueMap_.erase(param->name);
    }
    valueMap_.erase("this");

    // Clear IR context
    currentFunction_ = nullptr;
    if (builder_) builder_->setCurrentFunction(nullptr);  // Clear type validation context
    currentBlock_ = nullptr;
    irValues_.clear();
    localAllocas_.clear();
}

void LLVMBackend::visit(Parser::DestructorDecl& node) {
    // Clear local variables from previous method
    variables_.clear();
    valueMap_.clear();
    registerTypes_.clear();  // Clear register types to avoid stale type information

    if (currentClassName_.empty()) {
        emitLine("; ERROR: Destructor outside of class");
        return;
    }

    // Generate qualified function name: ClassName_Destructor
    std::string funcName = getQualifiedName(currentClassName_, "Destructor");

    // Store 'this' in value map
    valueMap_["this"] = "%this";

    // Destructors return void
    currentFunctionReturnType_ = "void";
    emitLine("; Destructor for " + currentClassName_);
    emitLine("define void @" + funcName + "(ptr %this) #1 {");
    indent();

    // IR generation: Create destructor function
    if (useNewIR_ && builder_ && module_) {
        std::vector<std::pair<std::string, IR::Type*>> irParams;
        irParams.push_back({"this", builder_->getPtrTy()});
        auto* dtorFunc = createIRFunction(funcName, builder_->getVoidTy(), irParams);
        if (dtorFunc) {
            auto* entryBlock = dtorFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entryBlock);
            currentFunction_ = dtorFunc;
            builder_->setCurrentFunction(currentFunction_);  // For type validation
            currentBlock_ = entryBlock;
            irValues_["this"] = dtorFunc->getArg(0);
        }
    }

    // Generate body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Return void
    emitLine("ret void");

    // IR: Create void return
    if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
        builder_->CreateRetVoid();
    }

    dedent();
    emitLine("}");
    emitLine("");

    // Clear 'this' mapping
    valueMap_.erase("this");

    // Clear IR context
    currentFunction_ = nullptr;
    if (builder_) builder_->setCurrentFunction(nullptr);  // Clear type validation context
    currentBlock_ = nullptr;
    irValues_.clear();
    localAllocas_.clear();
}

void LLVMBackend::visit(Parser::MethodDecl& node) {
    std::cerr.flush();

    // Clear local variables from previous method
    variables_.clear();
    valueMap_.clear();  // Also clear valueMap to avoid stale entries
    registerTypes_.clear();  // Clear register types to avoid stale type information
    localAllocas_.clear();  // Clear IR allocas

    // Skip template method declarations - only generate instantiated versions
    // Template methods have templateParams but are not instantiated (no concrete types)
    if (!node.templateParams.empty()) {
        return;  // This is a template method declaration, skip it
    }

    // Handle native FFI methods
    if (node.isNative) {
        generateNativeMethodThunk(node);
        return;
    }

    // Collect retained annotations for this method
    if (!currentClassName_.empty()) {
        collectRetainedAnnotations(node, currentClassName_);
    }

    // Determine function name (qualified if inside a class)
    std::string funcName;
    bool isInstanceMethod = !currentClassName_.empty();  // All class methods are instance methods


    if (!currentClassName_.empty()) {
        funcName = getQualifiedName(currentClassName_, node.name);
    } else {
        funcName = node.name;
    }
    std::cerr.flush();

    // Build parameter list and collect parameter types for name mangling
    std::stringstream params;
    std::vector<std::string> paramTypes;
    std::vector<std::pair<std::string, IR::Type*>> irParams;  // For IR function

    // Add implicit 'this' parameter for instance methods
    if (isInstanceMethod) {
        params << "ptr %this";
        valueMap_["this"] = "%this";
        if (builder_) {
            irParams.push_back({"this", builder_->getPtrTy()});
        }
    }

    for (size_t i = 0; i < node.parameters.size(); ++i) {
        if (i > 0 || isInstanceMethod) params << ", ";
        auto& param = node.parameters[i];
        std::string paramType = getLLVMType(param->type->typeName);
        params << paramType << " %" << param->name;
        paramTypes.push_back(paramType);

        // Store parameter in value map and track its type
        // Reconstruct full type name including template arguments
        std::string fullParamType = param->type->typeName;
        if (!param->type->templateArgs.empty()) {
            fullParamType += "<";
            for (size_t j = 0; j < param->type->templateArgs.size(); ++j) {
                if (j > 0) fullParamType += ", ";
                fullParamType += param->type->templateArgs[j].typeArg;
            }
            fullParamType += ">";
        }
        valueMap_[param->name] = "%" + param->name;
        registerTypes_["%" + param->name] = qualifyTypeName(fullParamType);

        // Add to IR params
        if (builder_) {
            IR::Type* irType = getIRType(param->type->typeName);
            irParams.push_back({param->name, irType});
        }
    }

    // Mangle constructor names to support overloading
    // LLVM doesn't support function overloading, so we need unique names
    if (node.name == "Constructor") {
        // Add parameter count suffix: Constructor_0, Constructor_1, Constructor_2, etc.
        std::string suffix = "_" + std::to_string(paramTypes.size());
        funcName += suffix;
        DEBUG_OUT("DEBUG: Mangling constructor '" << funcName << "' with " << paramTypes.size() << " params" << std::endl);
    }

    // Check for duplicate function definition (can happen with imported modules)
    if (generatedFunctions_.find(funcName) != generatedFunctions_.end()) {
        // Function already generated, skip to avoid redefinition error
        return;
    }
    generatedFunctions_.insert(funcName);

    // IR: Track function in module
    if (module_) {
        // Check if IR function already exists
        if (module_->getFunction(funcName)) {
            // Skip IR generation for duplicate
        }
    }

    // Generate function definition
    DEBUG_OUT("DEBUG MethodDecl: method=" << node.name << " returnTypeName='" << node.returnType->typeName << "'" << std::endl);
    std::string returnTypeName = node.returnType ? node.returnType->typeName : "void";
    std::string returnType = getLLVMType(returnTypeName);
    DEBUG_OUT("DEBUG MethodDecl: getLLVMType returned '" << returnType << "'" << std::endl);
    currentFunctionReturnType_ = returnType;  // Track for return statements
    std::cerr.flush();

    emitLine("; Method: " + node.name);
    emitLine("define " + returnType + " @" + funcName + "(" + params.str() + ") #1 {");

    // IR: Create function for method
    if (module_ && builder_) {
        // Create IR function type
        std::vector<IR::Type*> paramTypes;
        if (isInstanceMethod) {
            paramTypes.push_back(builder_->getPtrTy());  // this pointer
        }
        for (const auto& param : node.parameters) {
            paramTypes.push_back(getIRType(param->type->typeName));
        }
        IR::Type* retTy = getIRType(returnType);
        auto* funcTy = module_->getContext().getFunctionTy(retTy, paramTypes, false);
        auto* irFunc = module_->createFunction(funcTy, funcName, IR::Function::Linkage::External);
        if (irFunc) {
            auto* entry = irFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entry);
            currentFunction_ = irFunc;
            builder_->setCurrentFunction(currentFunction_);  // For type validation
            currentBlock_ = entry;
        }
    }
    indent();

    // IR generation: Create function
    // Skip IR infrastructure for now - it's a work-in-progress and causing issues
    // with imported module code generation. The textual LLVM IR output works correctly.
    IR::Function* irFunc = nullptr;
    IR::BasicBlock* entryBlock = nullptr;
    if (useNewIR_ && builder_ && module_) {
        IR::Type* irReturnType = getIRType(node.returnType->typeName);
        irFunc = createIRFunction(funcName, irReturnType, irParams);
        entryBlock = irFunc->createBasicBlock("entry");
        builder_->setInsertPoint(entryBlock);
        currentFunction_ = irFunc;
        builder_->setCurrentFunction(currentFunction_);  // For type validation
        currentBlock_ = entryBlock;

        // Map IR function parameters to irValues_
        for (size_t i = 0; i < irFunc->getNumArgs(); ++i) {
            IR::Argument* arg = irFunc->getArg(static_cast<unsigned>(i));
            if (arg) {
                irValues_[arg->getName()] = arg;
            }
        }
    }

    // Generate body
    for (size_t stmtIdx = 0; stmtIdx < node.body.size(); ++stmtIdx) {
        auto& stmt = node.body[stmtIdx];
        if (!stmt) {
            continue;
        }
        stmt->accept(*this);
    }

    // Ensure function has a return
    if (node.body.empty() || dynamic_cast<Parser::ReturnStmt*>(node.body.back().get()) == nullptr) {
        if (returnType == "void") {
            emitLine("ret void");
            if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
                builder_->CreateRetVoid();
            }
        } else if (returnType == "ptr") {
            emitLine("ret ptr null  ; implicit return");
            if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
                builder_->CreateRet(builder_->getNullPtr());
            }
        } else {
            emitLine("ret " + returnType + " 0  ; implicit return");
            if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
                // Use IRBuilder to create integer constant
                builder_->CreateRet(builder_->getInt64(0));
            }
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

    // Clear IR context
    currentFunction_ = nullptr;
    if (builder_) builder_->setCurrentFunction(nullptr);  // Clear type validation context
    currentBlock_ = nullptr;
    irValues_.clear();
    localAllocas_.clear();
}

void LLVMBackend::visit(Parser::ParameterDecl& node) {}

void LLVMBackend::visit(Parser::EntrypointDecl& node) {
    // Clear local variables from previous scope
    variables_.clear();

    emitLine("define i32 @main() #1 {");
    indent();

    // IR generation: Create main function
    IR::Function* mainFunc = nullptr;
    IR::BasicBlock* entryBlock = nullptr;
    // IR: Additional module setup for main
    if (module_) {
        (void)module_->getName();  // Access module to verify it exists
    }
    if (builder_ && module_) {
        // main() takes no parameters and returns i32
        std::vector<std::pair<std::string, IR::Type*>> emptyParams;
        mainFunc = createIRFunction("main", builder_->getInt32Ty(), emptyParams);
        if (mainFunc) {
            entryBlock = mainFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entryBlock);
            currentFunction_ = mainFunc;
            builder_->setCurrentFunction(currentFunction_);  // For type validation
            currentBlock_ = entryBlock;
        }
    }

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    emitLine("ret i32 0");
    dedent();
    emitLine("}");

    // IR generation: Add return if block doesn't have terminator
    if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
        builder_->CreateRet(builder_->getInt32(0));
    }

    // Clear IR function context
    currentFunction_ = nullptr;
    if (builder_) builder_->setCurrentFunction(nullptr);  // Clear type validation context
    currentBlock_ = nullptr;
}

void LLVMBackend::visit(Parser::InstantiateStmt& node) {
    // Check if type is a NativeType (primitives)
    bool isNativeType = (node.type->typeName.find("NativeType<") == 0 ||
                         node.type->typeName.find("NativeType_") == 0);

    // Handle compile-time variables: evaluate at compile-time and store as constants
    // IMPORTANT: Only treat as compile-time if EXPLICITLY marked as Compiletime
    // NativeType variables are NOT automatically compile-time because they may be modified via Set
    if (node.isCompiletime && node.initializer && semanticAnalyzer_) {
        Semantic::CompiletimeInterpreter interpreter(*semanticAnalyzer_);
        // Propagate known compile-time values to the interpreter
        // This allows method calls on compile-time values to be evaluated
        for (const auto& [name, ctValue] : compiletimeValues_) {
            interpreter.setVariable(name, ctValue->clone());
        }
        auto ctValue = interpreter.evaluate(node.initializer.get());
        if (ctValue) {
            // Store the compile-time value for later use
            compiletimeValues_[node.variableName] = std::move(ctValue);

            // Track NativeType variables - they always use raw values (no wrappers)
            if (isNativeType) {
                compiletimeNativeTypes_.insert(node.variableName);
                emitLine("; Compile-time NativeType constant: " + node.variableName);
            } else {
                emitLine("; Compile-time constant: " + node.variableName);
            }

            // For primitive types that can be directly emitted as constants,
            // we don't need runtime allocation - just track the value
            // The IdentifierExpr visitor will emit the constant directly
            return;
        }
        // Fall through to runtime allocation if compile-time evaluation fails
    }

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

    // IR generation: Create alloca instruction
    IR::AllocaInst* irAlloca = nullptr;
    if (builder_ && currentBlock_) {
        IR::Type* irType = getIRType(node.type->typeName);
        irAlloca = builder_->CreateAlloca(irType, node.variableName);
        localAllocas_[node.variableName] = irAlloca;
    }

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

        // FIX: When storing to a Bool variable, check if the initializer is an i1 (from comparison)
        // If so, wrap it with Bool_Constructor to create a proper Bool object
        std::string storeType = varType;
        std::string storeValue = initValue;

        // FIX: Handle compile-time folded values being assigned to runtime wrapper types
        // When CallExpr folds x.add(y) to raw 18, but target is Integer^, we need to wrap it
        if (varType == "ptr" && !storeValue.empty()) {
            // Check if storeValue is a raw integer literal (could be from compile-time folding)
            bool isRawIntLiteral = (std::isdigit(storeValue[0]) ||
                                    (storeValue[0] == '-' && storeValue.length() > 1 && std::isdigit(storeValue[1])));

            if (isRawIntLiteral) {
                // Strip ownership marker using TypeNormalizer
                std::string typeName = TypeNormalizer::stripOwnershipMarker(node.type->typeName);

                if (typeName == "Integer") {
                    // Wrap raw integer with Integer_Constructor
                    std::string wrapReg = allocateRegister();
                    emitLine(format("{} = call ptr @Integer_Constructor(i64 {})", wrapReg, storeValue));
                    storeValue = wrapReg;

                    // IR: Call Integer_Constructor to wrap raw value
                    if (builder_ && module_) {
                        IR::Function* intCtor = module_->getFunction("Integer_Constructor");
                        if (!intCtor) {
                            auto& ctx = module_->getContext();
                            auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                            intCtor = module_->createFunction(funcTy, "Integer_Constructor", IR::Function::Linkage::External);
                        }
                        if (lastExprValue_) {
                            lastExprValue_ = builder_->CreateCall(intCtor, {lastExprValue_}, "ct_int_wrap");
                        }
                    }
                } else if (typeName == "Float") {
                    // For Float, we need to parse the value and call Float_Constructor
                    std::string wrapReg = allocateRegister();
                    emitLine(format("{} = call ptr @Float_Constructor(float {})", wrapReg, storeValue));
                    storeValue = wrapReg;
                } else if (typeName == "Double") {
                    // For Double, call Double_Constructor
                    std::string wrapReg = allocateRegister();
                    emitLine(format("{} = call ptr @Double_Constructor(double {})", wrapReg, storeValue));
                    storeValue = wrapReg;
                }
            }
        }

        if (node.type->typeName == "Bool" && varType == "ptr") {
            // Check if initializer is a native boolean (i1) from comparison operations
            auto initTypeIt = registerTypes_.find(initValue);
            bool needsWrap = false;
            if (initTypeIt != registerTypes_.end() && initTypeIt->second == "NativeType<\"bool\">") {
                needsWrap = true;
            }
            // Also check for raw boolean literals "true" or "false" (from compile-time folding)
            if (storeValue == "true" || storeValue == "false") {
                needsWrap = true;
            }
            if (needsWrap) {
                // Wrap i1 value with Bool_Constructor to create a Bool object
                std::string boolObjReg = allocateRegister();
                emitLine(format("{} = call ptr @Bool_Constructor(i1 {})", boolObjReg, storeValue));
                storeValue = boolObjReg;

                // IR: Call Bool_Constructor to wrap i1 value
                if (builder_) {
                    IR::Value* boolVal = lastExprValue_;
                    // If lastExprValue_ is null or wrong type, create the bool constant
                    if (!boolVal || !boolVal->getType() || !boolVal->getType()->isInteger()) {
                        boolVal = builder_->getInt1(initValue == "true" || storeValue == "true");
                    }
                    IR::Function* boolCtor = module_->getFunction("Bool_Constructor");
                    if (!boolCtor) {
                        auto& ctx = module_->getContext();
                        auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt1Ty()}, false);
                        boolCtor = module_->createFunction(funcTy, "Bool_Constructor", IR::Function::Linkage::External);
                    }
                    lastExprValue_ = builder_->CreateCall(boolCtor, {boolVal}, "bool_wrap");
                }
            }
        }

        // Handle NativeType size conversions (e.g., i64 -> i32)
        if (node.type->typeName.find("NativeType<") == 0 && varType != "ptr") {
            // Get source type from registerTypes_
            auto sourceTypeIt = registerTypes_.find(initValue);
            std::string sourceNativeType;
            if (sourceTypeIt != registerTypes_.end()) {
                sourceNativeType = sourceTypeIt->second;
            }

            // Check if source is i64 and target is i32
            if ((varType == "i32" && (sourceNativeType == "NativeType<\"int64\">" ||
                                      sourceNativeType == "NativeType<int64>" ||
                                      sourceNativeType.find("int64") != std::string::npos)) ||
                (varType == "i32" && lastExprValue_ && lastExprValue_->getType() &&
                 lastExprValue_->getType()->isInteger() &&
                 static_cast<IR::IntegerType*>(lastExprValue_->getType())->getBitWidth() == 64)) {
                // Need to truncate i64 to i32
                std::string truncReg = allocateRegister();
                emitLine(format("{} = trunc i64 {} to i32", truncReg, storeValue));
                storeValue = truncReg;

                // IR: Create trunc instruction
                if (builder_ && lastExprValue_) {
                    lastExprValue_ = builder_->CreateTrunc(lastExprValue_, builder_->getInt32Ty(), "trunc_i64_i32");
                }
            }
            // Check if source is i32 and target is i64 (need zero extension)
            else if ((varType == "i64" && (sourceNativeType == "NativeType<\"int32\">" ||
                                           sourceNativeType == "NativeType<int32>" ||
                                           sourceNativeType.find("int32") != std::string::npos)) ||
                     (varType == "i64" && lastExprValue_ && lastExprValue_->getType() &&
                      lastExprValue_->getType()->isInteger() &&
                      static_cast<IR::IntegerType*>(lastExprValue_->getType())->getBitWidth() == 32)) {
                // Need to extend i32 to i64
                std::string extReg = allocateRegister();
                emitLine(format("{} = sext i32 {} to i64", extReg, storeValue));
                storeValue = extReg;

                // IR: Create sext instruction
                if (builder_ && lastExprValue_) {
                    lastExprValue_ = builder_->CreateSExt(lastExprValue_, builder_->getInt64Ty(), "sext_i32_i64");
                }
            }
        }

        // Store the initialized value (use the actual type from getLLVMType)
        emitLine("store " + storeType + " " + storeValue + ", ptr " + varReg);

        // IR generation: Store initial value to alloca
        if (builder_ && irAlloca && lastExprValue_) {
            builder_->CreateStore(lastExprValue_, irAlloca);
        }

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

    // IR generation: Create basic blocks for for loop
    IR::BasicBlock* condBB = nullptr;
    IR::BasicBlock* bodyBB = nullptr;
    IR::BasicBlock* incrBB = nullptr;
    IR::BasicBlock* endBB = nullptr;
    if (builder_ && currentFunction_) {
        condBB = currentFunction_->createBasicBlock(condLabel);
        bodyBB = currentFunction_->createBasicBlock(bodyLabel);
        incrBB = currentFunction_->createBasicBlock(incrLabel);
        endBB = currentFunction_->createBasicBlock(endLabel);
    }

    // Push loop labels for break/continue (with IR blocks)
    loopStack_.push_back({incrLabel, endLabel, incrBB, endBB});

    // Allocate iterator variable
    std::string iteratorReg = allocateRegister();
    std::string iteratorType = getLLVMType(node.iteratorType->typeName);
    emitLine(format("{} = alloca {}", iteratorReg, iteratorType));
    valueMap_[node.iteratorName] = iteratorReg;

    // Track type for later (needed for Integer^ handling)
    std::string xxmlIteratorType = node.iteratorType->typeName;
    registerTypes_[iteratorReg] = "ptr_to<" + xxmlIteratorType + ">";

    // CRITICAL FIX: Add loop variable to variables_ map so IdentifierExpr can properly load it
    variables_[node.iteratorName] = {
        iteratorReg,
        xxmlIteratorType,
        iteratorType,
        OwnershipKind::Owned
    };

    // IR: Create alloca for iterator variable and track in localAllocas_
    if (builder_ && currentBlock_) {
        IR::Type* allocTy = (iteratorType == "ptr")
            ? static_cast<IR::Type*>(builder_->getPtrTy())
            : static_cast<IR::Type*>(builder_->getInt64Ty());
        IR::AllocaInst* iterAllocaIR = builder_->CreateAlloca(allocTy, nullptr, node.iteratorName);
        localAllocas_[node.iteratorName] = iterAllocaIR;
    }

    // Initialize iterator with range start
    node.rangeStart->accept(*this);
    std::string startValue = valueMap_["__last_expr"];

    // If iterator type is Integer and we have a plain integer literal, wrap it
    if (iteratorType == "ptr" && (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^")) {
        // Check if startValue is just a number (literal)
        bool isLiteral = !startValue.empty() && (std::isdigit(startValue[0]) || startValue[0] == '-');
        if (isLiteral) {
            std::string constructorReg = allocateRegister();
            emitLine(format("{} = call ptr @Integer_Constructor(i64 {})", constructorReg, startValue));
            startValue = constructorReg;

            // IR: Call Integer_Constructor to wrap the literal
            if (builder_ && module_) {
                IR::Function* intConstructor = module_->getFunction("Integer_Constructor");
                if (!intConstructor) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                    intConstructor = module_->createFunction(funcTy, "Integer_Constructor", IR::Function::Linkage::External);
                }
                // Convert lastExprValue_ (i64 literal) to wrapped Integer object
                if (lastExprValue_) {
                    lastExprValue_ = builder_->CreateCall(intConstructor, {lastExprValue_}, "for_start_int");
                }
            }
        }
    }

    emitLine(format("store {} {}, ptr {}", iteratorType, startValue, iteratorReg));

    // IR: Store initial value to iterator alloca
    if (builder_ && lastExprValue_) {
        auto allocaIt = localAllocas_.find(node.iteratorName);
        if (allocaIt != localAllocas_.end()) {
            builder_->CreateStore(lastExprValue_, allocaIt->second);
        }
    }

    // IR: Store initial value to iterator
    if (builder_ && lastExprValue_) {
        auto allocaIt = localAllocas_.find(node.iteratorName);
        if (allocaIt != localAllocas_.end()) {
            builder_->CreateStore(lastExprValue_, allocaIt->second);
        }
    }

    if (node.isCStyleLoop) {
        // C-style for loop: For (Type <name> = init; condition; increment)
        // Jump to condition
        emitLine(format("br label %{}", condLabel));
        if (builder_ && condBB) {
            builder_->CreateBr(condBB);
            builder_->setInsertPoint(condBB);
            currentBlock_ = condBB;
        }

        // Condition
        emitLine(format("{}:", condLabel));
        indent();

        // IR: Set insert point to condition block
        if (builder_ && condBB) {
            builder_->setInsertPoint(condBB);
            currentBlock_ = condBB;
        }
        node.condition->accept(*this);
        std::string condValue = valueMap_["__last_expr"];

        // If condition returns a Bool^ object, extract the boolean value
        std::string condReg = condValue;
        auto typeIt = registerTypes_.find(condValue);
        if (typeIt != registerTypes_.end() && (typeIt->second == "Bool" || typeIt->second == "Bool^")) {
            // Call Bool_getValue to get the i1 value
            std::string boolValReg = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", boolValReg, condValue));
            condReg = boolValReg;

            // IR: Call Bool_getValue for condition
            if (builder_ && lastExprValue_ && lastExprValue_->getType()->isPointer()) {
                IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
                if (!boolGetValueFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy()}, false);
                    boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(boolGetValueFunc, {lastExprValue_}, "for_cond_bool");
            }
        }

        emitLine(format("br i1 {}, label %{}, label %{}", condReg, bodyLabel, endLabel));
        dedent();

        // IR: Create conditional branch with type checking
        if (builder_ && lastExprValue_ && bodyBB && endBB) {
            IR::Value* condI1 = lastExprValue_;
            if (lastExprValue_->getType()->isPointer()) {
                IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
                if (!boolGetValueFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy()}, false);
                    boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue", IR::Function::Linkage::External);
                }
                condI1 = builder_->CreateCall(boolGetValueFunc, {lastExprValue_}, "for_cond_i1");
            }
            builder_->CreateCondBr(condI1, bodyBB, endBB);
        }

        // Body
        emitLine(format("{}:", bodyLabel));
        indent();

        // IR: Set insert point to body block with additional setup
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            currentBlock_ = bodyBB;
        }
        // IR: Additional body block setup
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            // Emit a store instruction as a placeholder for loop body start
            // builder_->CreateStore(builder_->getInt64(0), builder_->getNullPtr());
        }
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            currentBlock_ = bodyBB;
        }
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
        emitLine(format("br label %{}", incrLabel));

        // IR: Create branch to increment block
        if (builder_ && incrBB && bodyBB && !bodyBB->getTerminator()) {
            builder_->CreateBr(incrBB);
        }
        dedent();

        // Increment
        emitLine(format("{}:", incrLabel));

        // IR: Set insert point to increment block
        if (builder_ && incrBB) {
            builder_->setInsertPoint(incrBB);
            currentBlock_ = incrBB;
        }

        indent();

        // IR: Set insert point to increment block
        if (builder_ && incrBB) {
            builder_->setInsertPoint(incrBB);
            currentBlock_ = incrBB;
        }
        if (builder_ && incrBB) {
            builder_->setInsertPoint(incrBB);
            currentBlock_ = incrBB;
        }
        node.increment->accept(*this);
        // The increment expression should update the loop variable
        // For expressions like "i.add(Integer::Constructor(1))", we need to store the result
        std::string incrResult = valueMap_["__last_expr"];
        if (!incrResult.empty() && incrResult != "") {
            // Store the result back to the iterator variable
            emitLine(format("store {} {}, ptr {}", iteratorType, incrResult, iteratorReg));

            // IR: Store increment result
            if (builder_ && lastExprValue_) {
                auto allocaIt = localAllocas_.find(node.iteratorName);
                if (allocaIt != localAllocas_.end()) {
                    builder_->CreateStore(lastExprValue_, allocaIt->second);
                }
            }
        }
        emitLine(format("br label %{}", condLabel));

        // IR: Branch back to condition
        if (builder_ && condBB && incrBB && !incrBB->getTerminator()) {
            builder_->CreateBr(condBB);
        }
        dedent();

        // End
        emitLine(format("{}:", endLabel));
        if (builder_ && endBB) {
            builder_->setInsertPoint(endBB);
            currentBlock_ = endBB;
        }
    } else {
        // Range-based for loop: For (Type <name> = start .. end)
        // Evaluate range end once
        node.rangeEnd->accept(*this);
        std::string endValue = valueMap_["__last_expr"];

        // Same wrapping for end value
        if (iteratorType == "ptr" && (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^")) {
            bool isLiteral = !endValue.empty() && (std::isdigit(endValue[0]) || endValue[0] == '-');
            if (isLiteral) {
                std::string constructorReg = allocateRegister();
                emitLine(format("{} = call ptr @Integer_Constructor(i64 {})", constructorReg, endValue));
                endValue = constructorReg;

                // IR: Call Integer_Constructor to wrap the literal
                if (builder_ && module_) {
                    IR::Function* intConstructorEnd = module_->getFunction("Integer_Constructor");
                    if (!intConstructorEnd) {
                        auto& ctx = module_->getContext();
                        auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                        intConstructorEnd = module_->createFunction(funcTy, "Integer_Constructor", IR::Function::Linkage::External);
                    }
                    if (lastExprValue_) {
                        lastExprValue_ = builder_->CreateCall(intConstructorEnd, {lastExprValue_}, "for_end_int");
                    }
                }
            }
        }

        std::string endReg = allocateRegister();
        emitLine(format("{} = alloca {}", endReg, iteratorType));
        emitLine(format("store {} {}, ptr {}", iteratorType, endValue, endReg));

        // IR: Alloca and store for range end already handled below
        // IR: Create alloca and store for endReg
        IR::AllocaInst* endAllocaIR = nullptr;
        IR::Value* endValueIR = lastExprValue_;  // From range end evaluation
        if (builder_ && currentBlock_) {
            IR::Type* allocTy = (iteratorType == "ptr")
                ? static_cast<IR::Type*>(builder_->getPtrTy())
                : static_cast<IR::Type*>(builder_->getInt64Ty());
            endAllocaIR = builder_->CreateAlloca(allocTy, nullptr, "for_end");
            if (endValueIR) {
                builder_->CreateStore(endValueIR, endAllocaIR);
            }
        }

        // Jump to condition
        emitLine(format("br label %{}", condLabel));
        if (builder_ && condBB) {
            builder_->CreateBr(condBB);
            builder_->setInsertPoint(condBB);
            currentBlock_ = condBB;
        }

        // Condition: check if iterator < end
        emitLine(format("{}:", condLabel));
        indent();
        std::string currentVal = allocateRegister();
        std::string endVal = allocateRegister();
        emitLine(format("{} = load {}, ptr {}", currentVal, iteratorType, iteratorReg));
        emitLine(format("{} = load {}, ptr {}", endVal, iteratorType, endReg));

        // IR: Load current and end values for range comparison
        IR::Value* currentValIR = nullptr;
        IR::Value* endValIR = nullptr;
        if (builder_) {
            auto iterAllocaIt = localAllocas_.find(node.iteratorName);
            if (iterAllocaIt != localAllocas_.end()) {
                IR::Type* loadTy = (iteratorType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                currentValIR = builder_->CreateLoad(loadTy, iterAllocaIt->second, "cur_val");
            }
        }

        std::string condReg = allocateRegister();

        // For Integer^ types, we need to compare the objects, not pointers
        if (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^") {
            // Call Integer comparison: Integer_lt(currentVal, endVal)
            emitLine(format("{} = call i1 @Integer_lt(ptr {}, ptr {})", condReg, currentVal, endVal));

            // IR: Call Integer_lt for comparison
            if (builder_ && module_) {
                IR::Function* intLtFunc = module_->getFunction("Integer_lt");
                if (!intLtFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy(), builder_->getPtrTy()}, false);
                    intLtFunc = module_->createFunction(funcTy, "Integer_lt", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(intLtFunc, {builder_->getNullPtr(), builder_->getNullPtr()}, "int_lt");
            }
        } else {
            // For primitive types, use direct comparison
            emitLine(format("{} = icmp slt {} {}, {}", condReg, iteratorType, currentVal, endVal));
        }
        emitLine(format("br i1 {}, label %{}, label %{}", condReg, bodyLabel, endLabel));
        dedent();

        // IR: Conditional branch is created below after the loads

        // IR: Create loads and conditional branch for range-based loop
        if (builder_ && bodyBB && endBB && endAllocaIR) {
            // Get or create iteratorAllocaIR from localAllocas_
            IR::Value* iterAllocaIR = nullptr;
            auto allocaIt = localAllocas_.find(node.iteratorName);
            if (allocaIt != localAllocas_.end()) {
                iterAllocaIR = allocaIt->second;
            }

            if (iterAllocaIR) {
                IR::Type* loadTy = (iteratorType == "ptr")
                    ? static_cast<IR::Type*>(builder_->getPtrTy())
                    : static_cast<IR::Type*>(builder_->getInt64Ty());
                IR::Value* currentValIR = builder_->CreateLoad(loadTy, iterAllocaIR, "for_curr");
                IR::Value* endValIR = builder_->CreateLoad(loadTy, endAllocaIR, "for_endv");

                IR::Value* condIR = nullptr;
                if (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^") {
                    // Call Integer_lt for Integer types
                    IR::Function* intLtFunc = module_->getFunction("Integer_lt");
                    if (!intLtFunc) {
                        auto& ctx = module_->getContext();
                        auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy(), builder_->getPtrTy()}, false);
                        intLtFunc = module_->createFunction(funcTy, "Integer_lt", IR::Function::Linkage::External);
                    }
                    condIR = builder_->CreateCall(intLtFunc, {currentValIR, endValIR}, "for_lt");
                } else {
                    // Use icmp for primitive types
                    condIR = builder_->CreateICmpSLT(currentValIR, endValIR, "for_cmp");
                }
                builder_->CreateCondBr(condIR, bodyBB, endBB);
            } else {
                // Fallback if alloca not found
                builder_->CreateCondBr(builder_->getTrue(), bodyBB, endBB);
            }
        }

        // Body
        emitLine(format("{}:", bodyLabel));
        indent();

        // IR: Set insert point to body block with additional setup
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            currentBlock_ = bodyBB;
        }
        // IR: Additional body block setup
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            // Emit a store instruction as a placeholder for loop body start
            // builder_->CreateStore(builder_->getInt64(0), builder_->getNullPtr());
        }
        if (builder_ && bodyBB) {
            builder_->setInsertPoint(bodyBB);
            currentBlock_ = bodyBB;
        }
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
        emitLine(format("br label %{}", incrLabel));

        // IR: Create branch to increment block
        if (builder_ && incrBB && bodyBB && !bodyBB->getTerminator()) {
            builder_->CreateBr(incrBB);
        }
        dedent();

        // Increment iterator
        emitLine(format("{}:", incrLabel));

        // IR: Set insert point to increment block
        if (builder_ && incrBB) {
            builder_->setInsertPoint(incrBB);
            currentBlock_ = incrBB;
        }

        indent();
        if (builder_ && incrBB) {
            builder_->setInsertPoint(incrBB);
            currentBlock_ = incrBB;
        }
        std::string iterVal = allocateRegister();
        emitLine(format("{} = load {}, ptr {}", iterVal, iteratorType, iteratorReg));

        // IR: Load iterator value for increment
        if (builder_) {
            auto iterAllocaIt = localAllocas_.find(node.iteratorName);
            if (iterAllocaIt != localAllocas_.end()) {
                IR::Type* loadTy = (iteratorType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                lastExprValue_ = builder_->CreateLoad(loadTy, iterAllocaIt->second, "iter_val");
            }
        }
        std::string nextVal = allocateRegister();

        // For Integer^ types, we need to call add method
        IR::Value* nextValIR = nullptr;
        if (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^") {
            // Create Integer(1) for increment
            std::string oneReg = allocateRegister();
            emitLine(format("{} = call ptr @Integer_Constructor(i64 1)", oneReg));
            // Call Integer_add(iterVal, oneReg) to get new Integer
            emitLine(format("{} = call ptr @Integer_add(ptr {}, ptr {})", nextVal, iterVal, oneReg));

            // IR: Create Integer_Constructor and Integer_add calls
            if (builder_ && incrBB) {
                auto allocaIt = localAllocas_.find(node.iteratorName);
                if (allocaIt != localAllocas_.end()) {
                    IR::Type* loadTy = builder_->getPtrTy();
                    IR::Value* iterValIR = builder_->CreateLoad(loadTy, allocaIt->second, "iter_load");

                    // Call Integer_Constructor(1)
                    IR::Function* intConstructor = module_->getFunction("Integer_Constructor");
                    if (!intConstructor) {
                        auto& ctx = module_->getContext();
                        auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                        intConstructor = module_->createFunction(funcTy, "Integer_Constructor", IR::Function::Linkage::External);
                    }
                    IR::Value* oneIR = builder_->CreateCall(intConstructor, {builder_->getInt64(1)}, "one");

                    // Call Integer_add(iterValIR, oneIR)
                    IR::Function* intAdd = module_->getFunction("Integer_add");
                    if (!intAdd) {
                        auto& ctx = module_->getContext();
                        auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getPtrTy(), builder_->getPtrTy()}, false);
                        intAdd = module_->createFunction(funcTy, "Integer_add", IR::Function::Linkage::External);
                    }
                    nextValIR = builder_->CreateCall(intAdd, {iterValIR, oneIR}, "next_val");
                }
            }
        } else {
            // For primitive types, use direct add
            emitLine(format("{} = add {} {}, 1", nextVal, iteratorType, iterVal));

            // IR: Create add instruction for primitive increment
            if (builder_) {
                lastExprValue_ = builder_->CreateAdd(builder_->getInt64(0), builder_->getInt64(1), "incr");
            }

            // IR: Create add instruction
            if (builder_ && incrBB) {
                auto allocaIt = localAllocas_.find(node.iteratorName);
                if (allocaIt != localAllocas_.end()) {
                    IR::Type* loadTy = builder_->getInt64Ty();
                    IR::Value* iterValIR = builder_->CreateLoad(loadTy, allocaIt->second, "iter_load");
                    nextValIR = builder_->CreateAdd(iterValIR, builder_->getInt64(1), "next_val");
                }
            }
        }
        emitLine(format("store {} {}, ptr {}", iteratorType, nextVal, iteratorReg));

        // IR: Store incremented value
        if (builder_ && lastExprValue_) {
            auto iterAllocaIt = localAllocas_.find(node.iteratorName);
            if (iterAllocaIt != localAllocas_.end()) {
                builder_->CreateStore(lastExprValue_, iterAllocaIt->second);
            }
        }

        // IR: Store next value back to iterator
        if (builder_ && nextValIR) {
            auto allocaIt = localAllocas_.find(node.iteratorName);
            if (allocaIt != localAllocas_.end()) {
                builder_->CreateStore(nextValIR, allocaIt->second);
            }
        }

        emitLine(format("br label %{}", condLabel));

        // IR: Branch back to condition
        if (builder_ && condBB && incrBB && !incrBB->getTerminator()) {
            builder_->CreateBr(condBB);
        }
        dedent();

        // End
        emitLine(format("{}:", endLabel));
        if (builder_ && endBB) {
            builder_->setInsertPoint(endBB);
            currentBlock_ = endBB;
        }
    }

    // Pop loop labels
    loopStack_.pop_back();
}

void LLVMBackend::visit(Parser::ExitStmt& node) {
    emitLine("call void @exit(i32 0)");

    // IR: Call exit function
    if (builder_ && module_) {
        IR::Function* exitFunc = module_->getFunction("exit");
        if (!exitFunc) {
            auto& ctx = module_->getContext();
            auto* funcTy = ctx.getFunctionTy(builder_->getVoidTy(), {builder_->getInt32Ty()}, false);
            exitFunc = module_->createFunction(funcTy, "exit", IR::Function::Linkage::External);
        }
        builder_->CreateCall(exitFunc, {builder_->getInt32(0)});
    }

    // IR generation: Generate call to exit function
    if (builder_ && module_) {
        IR::Function* exitFunc = module_->getFunction("exit");
        if (!exitFunc) {
            auto& ctx = module_->getContext();
            auto* funcTy = ctx.getFunctionTy(builder_->getVoidTy(), {builder_->getInt32Ty()}, false);
            exitFunc = module_->createFunction(funcTy, "exit", IR::Function::Linkage::External);
        }
        builder_->CreateCall(exitFunc, {builder_->getInt32(0)});
        // Exit doesn't return, but add unreachable for proper IR
        builder_->CreateUnreachable();
    }
}

void LLVMBackend::visit(Parser::ReturnStmt& node) {
    if (node.value) {
        node.value->accept(*this);
        std::string returnValue = valueMap_["__last_expr"];

        // Special case: returning None::Constructor() from void function
        // None::Constructor() sets __last_expr to "null" and function should return void
        if (returnValue == "null" && currentFunctionReturnType_ == "void") {
            emitLine("ret void");

            // IR: Generate void return for None::Constructor() case
            if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
                builder_->CreateRetVoid();
            }
        } else {
            // Use the tracked return type from the current function
            emitLine(format("ret {} {}", currentFunctionReturnType_, returnValue));
        }

        // IR generation: Generate return with value
        if (builder_ && currentBlock_ && lastExprValue_) {
            builder_->CreateRet(lastExprValue_);
        }
    } else {
        emitLine("ret void");

        // IR generation: Generate void return
        if (builder_ && currentBlock_) {
            builder_->CreateRetVoid();
        }
    }
}

void LLVMBackend::visit(Parser::IfStmt& node) {
    std::string thenLabel = allocateLabel("if_then");
    std::string elseLabel = allocateLabel("if_else");
    std::string endLabel = allocateLabel("if_end");

    // Evaluate condition
    node.condition->accept(*this);
    std::string condValue = valueMap_["__last_expr"];

    // Check if condition is a Bool object (ptr) that needs conversion to i1
    auto condTypeIt = registerTypes_.find(condValue);
    if (condTypeIt != registerTypes_.end()) {
        std::string xxmlType = condTypeIt->second;
        if (xxmlType == "Bool" || xxmlType == "Bool^" || xxmlType.find("Bool") != std::string::npos) {
            // It's a Bool object - extract the native bool value
            std::string boolVal = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", boolVal, condValue));
            condValue = boolVal;

            // IR: Call Bool_getValue for if condition
            if (builder_ && lastExprValue_ && lastExprValue_->getType()->isPointer()) {
                IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
                if (!boolGetValueFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy()}, false);
                    boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(boolGetValueFunc, {lastExprValue_}, "if_cond_i1");
            }
        }
    }

    // Branch based on condition
    emitLine(format("br i1 {}, label %{}, label %{}",
                        condValue, thenLabel, elseLabel));

    // IR: Create conditional branch
    // (Note: Full IR for IfStmt is handled in the IR section below)

    // Then branch
    emitLine(format("{}:", thenLabel));
    indent();
    for (auto& stmt : node.thenBranch) {
        stmt->accept(*this);
    }
    dedent();
    emitLine(format("br label %{}", endLabel));

    // IR: Branch from then to end (set up in IR section below)

    // Else branch
    emitLine(format("{}:", elseLabel));
    indent();
    if (!node.elseBranch.empty()) {
        for (auto& stmt : node.elseBranch) {
            stmt->accept(*this);
        }
    }
    dedent();
    emitLine(format("br label %{}", endLabel));

    // End label
    emitLine(format("{}:", endLabel));

    // IR generation: Set up basic blocks for control flow
    IR::BasicBlock* thenBBIR = nullptr;
    IR::BasicBlock* elseBBIR = nullptr;
    IR::BasicBlock* mergeBBIR = nullptr;
    if (builder_ && currentFunction_) {
        IR::Value* condIR = lastExprValue_;

        // Create basic blocks for then, else, and merge
        thenBBIR = currentFunction_->createBasicBlock(thenLabel);
        elseBBIR = currentFunction_->createBasicBlock(elseLabel);
        mergeBBIR = currentFunction_->createBasicBlock(endLabel);

        // Create conditional branch with proper type checking
        if (condIR) {
            IR::Value* condI1 = condIR;

            // If condition is a pointer (Bool object), call Bool_getValue to extract i1
            if (condIR->getType()->isPointer()) {
                // Get or declare Bool_getValue function
                IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
                if (!boolGetValueFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(),
                                                      {builder_->getPtrTy()}, false);
                    boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue",
                                                                IR::Function::Linkage::External);
                }
                condI1 = builder_->CreateCall(boolGetValueFunc, {condIR}, "cond_i1");
            }

            builder_->CreateCondBr(condI1, thenBBIR, elseBBIR);
        }

        // Set up then block
        builder_->setInsertPoint(thenBBIR);
        currentBlock_ = thenBBIR;
        // Only add branch if block doesn't already have a terminator
        if (!thenBBIR->getTerminator()) {
            builder_->CreateBr(mergeBBIR);
        }

        // Set up else block (statements already generated via textual IR above)
        builder_->setInsertPoint(elseBBIR);
        currentBlock_ = elseBBIR;
        // Only add branch if block doesn't already have a terminator
        if (!elseBBIR->getTerminator()) {
            builder_->CreateBr(mergeBBIR);
        }

        // Continue with merge block
        builder_->setInsertPoint(mergeBBIR);
        currentBlock_ = mergeBBIR;
    }
}

void LLVMBackend::visit(Parser::WhileStmt& node) {
    std::string condLabel = allocateLabel("while_cond");
    std::string bodyLabel = allocateLabel("while_body");
    std::string endLabel = allocateLabel("while_end");

    // IR generation: Create basic blocks for while loop
    IR::BasicBlock* condBB = nullptr;
    IR::BasicBlock* bodyBB = nullptr;
    IR::BasicBlock* endBB = nullptr;
    if (builder_ && currentFunction_) {
        condBB = currentFunction_->createBasicBlock(condLabel);
        bodyBB = currentFunction_->createBasicBlock(bodyLabel);
        endBB = currentFunction_->createBasicBlock(endLabel);
    }

    // Push loop labels for break/continue
    loopStack_.push_back({condLabel, endLabel, condBB, endBB});

    // Jump to condition check
    emitLine(format("br label %{}", condLabel));
    if (builder_ && condBB) {
        builder_->CreateBr(condBB);
    }

    // Condition label
    emitLine(format("{}:", condLabel));
    indent();

    // IR: Set insert point to condition block
    if (builder_ && condBB) {
        builder_->setInsertPoint(condBB);
        currentBlock_ = condBB;
    }

    // Evaluate condition
    node.condition->accept(*this);
    std::string condValue = valueMap_["__last_expr"];

    // Check if condition is a Bool object (ptr) that needs conversion to i1
    auto condTypeIt = registerTypes_.find(condValue);
    if (condTypeIt != registerTypes_.end()) {
        std::string xxmlType = condTypeIt->second;
        if (xxmlType == "Bool" || xxmlType == "Bool^" || xxmlType.find("Bool") != std::string::npos) {
            // It's a Bool object - extract the native bool value
            std::string boolVal = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", boolVal, condValue));

            // IR: Call Bool_getValue for if condition
            if (builder_ && module_ && lastExprValue_) {
                IR::Function* boolGetVal = module_->getFunction("Bool_getValue");
                if (!boolGetVal) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy()}, false);
                    boolGetVal = module_->createFunction(funcTy, "Bool_getValue", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(boolGetVal, {lastExprValue_}, "if_cond_bool");
            }

            condValue = boolVal;
        }
    }

    // Branch based on condition
    emitLine(format("br i1 {}, label %{}, label %{}",
                        condValue, bodyLabel, endLabel));
    dedent();

    // IR: Create conditional branch with proper type checking
    if (builder_ && lastExprValue_ && bodyBB && endBB) {
        IR::Value* condI1 = lastExprValue_;

        // If condition is a pointer (Bool object), call Bool_getValue to extract i1
        if (lastExprValue_->getType()->isPointer()) {
            // Get or declare Bool_getValue function
            IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
            if (!boolGetValueFunc) {
                auto& ctx = module_->getContext();
                auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(),
                                                  {builder_->getPtrTy()}, false);
                boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue",
                                                            IR::Function::Linkage::External);
            }
            condI1 = builder_->CreateCall(boolGetValueFunc, {lastExprValue_}, "cond_i1");
        }

        builder_->CreateCondBr(condI1, bodyBB, endBB);
    }

    // Body label
    emitLine(format("{}:", bodyLabel));
    indent();

    // IR: Set insert point to body block
    if (builder_ && bodyBB) {
        builder_->setInsertPoint(bodyBB);
        currentBlock_ = bodyBB;
    }

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    // Loop back to condition
    emitLine(format("br label %{}", condLabel));
    dedent();

    // IR: Branch back to condition
    if (builder_ && bodyBB && condBB && !bodyBB->getTerminator()) {
        builder_->CreateBr(condBB);
    }

    // End label
    emitLine(format("{}:", endLabel));

    // IR: Set insert point to end block
    if (builder_ && endBB) {
        builder_->setInsertPoint(endBB);
        currentBlock_ = endBB;
    }

    // Pop loop labels
    loopStack_.pop_back();
}

void LLVMBackend::visit(Parser::BreakStmt& node) {
    if (!loopStack_.empty()) {
        emitLine(format("br label %{}  ; break", loopStack_.back().endLabel));

        // IR generation: Branch to end block
        if (builder_ && loopStack_.back().endBlock) {
            builder_->CreateBr(loopStack_.back().endBlock);
        }
    } else {
        emitLine("; ERROR: break outside of loop");
    }
}

void LLVMBackend::visit(Parser::ContinueStmt& node) {
    if (!loopStack_.empty()) {
        emitLine(format("br label %{}  ; continue", loopStack_.back().condLabel));

        // IR generation: Branch to condition block
        if (builder_ && loopStack_.back().condBlock) {
            builder_->CreateBr(loopStack_.back().condBlock);
        }
    } else {
        emitLine("; ERROR: continue outside of loop");
    }
}

void LLVMBackend::visit(Parser::IntegerLiteralExpr& node) {
    // Integer literals are constants, don't need a register
    // Store the result so parent expressions can use it
    std::string reg = format("{}", node.value);
    valueMap_["__last_expr"] = reg;

    // New IR infrastructure - create i64 constant
    if (builder_) {
        lastExprValue_ = builder_->getInt64(node.value);
        // Additional IR: Create type for verification
        (void)builder_->getInt64Ty();
    }
}

void LLVMBackend::visit(Parser::FloatLiteralExpr& node) {
    // Float literals in LLVM IR:
    // LLVM IR requires float constants in hex to use 64-bit double precision format
    // Convert float to double, then to IEEE 754 hex (0xHHHHHHHHHHHHHHHH)
    double doubleValue = static_cast<double>(node.value);
    uint64_t bits;
    std::memcpy(&bits, &doubleValue, sizeof(double));

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << bits << "f";
    valueMap_["__last_expr"] = oss.str();

    // New IR infrastructure
    if (builder_) {
        lastExprValue_ = builder_->getFloat(node.value);
    }
}

void LLVMBackend::visit(Parser::DoubleLiteralExpr& node) {
    // Double literals are constants - format with sufficient precision
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(17) << node.value;
    valueMap_["__last_expr"] = oss.str();

    // New IR infrastructure
    if (builder_) {
        lastExprValue_ = builder_->getDouble(node.value);
    }
}

void LLVMBackend::visit(Parser::StringLiteralExpr& node) {
    // Create a unique label for this string literal
    std::string strLabel = format("str.{}", labelCounter_++);

    // Store the string literal for later emission as a global constant
    stringLiterals_.push_back({strLabel, node.value});

    // Create a reference to the global string constant
    // The reference should be of type ptr, not i64
    std::string reg = format("@.{}", strLabel);
    valueMap_["__last_expr"] = reg;

    // New IR infrastructure - create global string constant
    if (module_) {
        lastExprValue_ = module_->getOrCreateStringLiteral(node.value);
    }
}

void LLVMBackend::visit(Parser::BoolLiteralExpr& node) {
    // Boolean literals are constants
    std::string reg = format("{}", node.value ? "1" : "0");
    valueMap_["__last_expr"] = reg;

    // New IR infrastructure
    if (builder_) {
        lastExprValue_ = builder_->getInt1(node.value);
    }
}

void LLVMBackend::visit(Parser::ThisExpr& node) {
    // 'this' pointer in LLVM IR
    valueMap_["__last_expr"] = "%this";

    // New IR infrastructure - get 'this' from function arguments
    if (currentFunction_ && currentFunction_->getNumArgs() > 0) {
        // Convention: first argument is 'this' pointer for methods
        lastExprValue_ = currentFunction_->getArg(0);
    }
}

void LLVMBackend::visit(Parser::IdentifierExpr& node) {
    // Check for compile-time constants first
    auto ctIt = compiletimeValues_.find(node.name);
    if (ctIt != compiletimeValues_.end()) {
        Semantic::CompiletimeValue* ctValue = ctIt->second.get();

        // Check if this is a NativeType variable (always emit raw, never wrap)
        bool isNativeTypeVar = (compiletimeNativeTypes_.find(node.name) != compiletimeNativeTypes_.end());

        // NEW: Emit raw constant when context allows (constant folding optimization)
        // This avoids runtime wrapper object creation when raw values suffice
        // NativeType variables ALWAYS emit raw values
        if (isNativeTypeVar ||
            currentValueContext_ == ValueContext::RawValue ||
            currentValueContext_ == ValueContext::OperandContext) {

            if (ctValue->isInteger()) {
                auto* intVal = static_cast<Semantic::CompiletimeInteger*>(ctValue);
                // Direct i64 constant - NO constructor call!
                emitLine(format("; Compile-time constant {} = {} (raw i64)", node.name, intVal->value));
                valueMap_["__last_expr"] = std::to_string(intVal->value);
                if (builder_) {
                    lastExprValue_ = builder_->getInt64(intVal->value);
                }
                return;
            }
            if (ctValue->isBool()) {
                auto* boolVal = static_cast<Semantic::CompiletimeBool*>(ctValue);
                // Direct i1 constant - NO constructor call!
                emitLine(format("; Compile-time constant {} = {} (raw i1)", node.name, boolVal->value ? "true" : "false"));
                valueMap_["__last_expr"] = boolVal->value ? "true" : "false";
                if (builder_) {
                    lastExprValue_ = builder_->getInt1(boolVal->value);
                }
                return;
            }
            if (ctValue->isFloat()) {
                auto* floatVal = static_cast<Semantic::CompiletimeFloat*>(ctValue);
                // Direct float constant - NO constructor call!
                emitLine(format("; Compile-time constant {} (raw float)", node.name));
                std::ostringstream ss;
                ss << std::hexfloat << floatVal->value;
                valueMap_["__last_expr"] = ss.str();
                if (builder_) {
                    lastExprValue_ = builder_->getFloat(floatVal->value);
                }
                return;
            }
            if (ctValue->isDouble()) {
                auto* doubleVal = static_cast<Semantic::CompiletimeDouble*>(ctValue);
                // Direct double constant - NO constructor call!
                emitLine(format("; Compile-time constant {} (raw double)", node.name));
                std::ostringstream ss;
                ss << std::hexfloat << doubleVal->value;
                valueMap_["__last_expr"] = ss.str();
                if (builder_) {
                    lastExprValue_ = builder_->getDouble(doubleVal->value);
                }
                return;
            }
            // Strings can't use raw values, fall through to wrapper creation
        }

        // Check if this compile-time value has already been materialized
        auto matIt = compiletimeMaterialized_.find(node.name);
        if (matIt != compiletimeMaterialized_.end()) {
            // Reuse the cached materialized value (optimization: don't create object again)
            valueMap_["__last_expr"] = matIt->second;
            return;
        }

        // First access - materialize the compile-time constant (wrapper object needed)
        if (ctValue->isInteger()) {
            auto* intVal = static_cast<Semantic::CompiletimeInteger*>(ctValue);
            std::string valueReg = allocateRegister();
            // For Integer type, we call Integer_Constructor to wrap the i64
            emitLine(format("; Materializing compile-time Integer constant: {} = {}", node.name, intVal->value));
            emitLine(format("{} = call ptr @Integer_Constructor(i64 {})", valueReg, intVal->value));
            valueMap_["__last_expr"] = valueReg;
            registerTypes_[valueReg] = "Integer";
            compiletimeMaterialized_[node.name] = valueReg;  // Cache for reuse

            // IR: Create Integer constant and wrap
            if (builder_) {
                IR::Value* i64Val = builder_->getInt64(intVal->value);
                IR::Function* intCtor = module_->getFunction("Integer_Constructor");
                if (intCtor) {
                    lastExprValue_ = builder_->CreateCall(intCtor, {i64Val}, "ct_int");
                } else {
                    lastExprValue_ = i64Val;
                }
            }
            return;
        }
        if (ctValue->isBool()) {
            auto* boolVal = static_cast<Semantic::CompiletimeBool*>(ctValue);
            std::string valueReg = allocateRegister();
            emitLine(format("; Materializing compile-time Bool constant: {} = {}", node.name, boolVal->value ? "true" : "false"));
            emitLine(format("{} = call ptr @Bool_Constructor(i1 {})", valueReg, boolVal->value ? "true" : "false"));
            valueMap_["__last_expr"] = valueReg;
            registerTypes_[valueReg] = "Bool";
            compiletimeMaterialized_[node.name] = valueReg;  // Cache for reuse

            if (builder_) {
                IR::Value* i1Val = builder_->getInt1(boolVal->value);
                IR::Function* boolCtor = module_->getFunction("Bool_Constructor");
                if (boolCtor) {
                    lastExprValue_ = builder_->CreateCall(boolCtor, {i1Val}, "ct_bool");
                } else {
                    lastExprValue_ = i1Val;
                }
            }
            return;
        }
        if (ctValue->isFloat()) {
            auto* floatVal = static_cast<Semantic::CompiletimeFloat*>(ctValue);
            std::string valueReg = allocateRegister();
            emitLine(format("; Materializing compile-time Float constant: {}", node.name));
            emitLine(format("{} = call ptr @Float_Constructor(float {})", valueReg, floatVal->value));
            valueMap_["__last_expr"] = valueReg;
            registerTypes_[valueReg] = "Float";
            compiletimeMaterialized_[node.name] = valueReg;  // Cache for reuse

            if (builder_) {
                lastExprValue_ = builder_->getFloat(floatVal->value);
            }
            return;
        }
        if (ctValue->isDouble()) {
            auto* doubleVal = static_cast<Semantic::CompiletimeDouble*>(ctValue);
            std::string valueReg = allocateRegister();
            emitLine(format("; Materializing compile-time Double constant: {}", node.name));
            emitLine(format("{} = call ptr @Double_Constructor(double {})", valueReg, doubleVal->value));
            valueMap_["__last_expr"] = valueReg;
            registerTypes_[valueReg] = "Double";
            compiletimeMaterialized_[node.name] = valueReg;  // Cache for reuse

            if (builder_) {
                lastExprValue_ = builder_->getDouble(doubleVal->value);
            }
            return;
        }
        if (ctValue->isString()) {
            auto* strVal = static_cast<Semantic::CompiletimeString*>(ctValue);
            // Create a string literal and call String_Constructor
            std::string label = "str.ct." + std::to_string(stringLiterals_.size());
            stringLiterals_.push_back({label, strVal->value});

            std::string ptrReg = allocateRegister();
            emitLine(format("; Materializing compile-time String constant: {}", node.name));
            // Reference global with @. prefix
            emitLine(format("{} = getelementptr inbounds [{} x i8], ptr @.{}, i64 0, i64 0",
                           ptrReg, strVal->value.length() + 1, label));

            std::string valueReg = allocateRegister();
            emitLine(format("{} = call ptr @String_Constructor(ptr {})", valueReg, ptrReg));
            valueMap_["__last_expr"] = valueReg;
            registerTypes_[valueReg] = "String";
            compiletimeMaterialized_[node.name] = valueReg;  // Cache for reuse

            if (builder_) {
                // For IR, we would create a global string constant
                // For now, emit as runtime constructor call
                IR::Function* strCtor = module_->getFunction("String_Constructor");
                if (strCtor) {
                    // String handling in IR is more complex, fall back to legacy for now
                }
            }
            return;
        }
        // For other compile-time types, fall through to runtime handling
    }

    auto it = valueMap_.find(node.name);
    if (it != valueMap_.end()) {
        // Local variable - need to load its value
        auto varIt = variables_.find(node.name);
        if (varIt != variables_.end()) {
            // Generate load instruction with the correct type
            std::string valueReg = allocateRegister();
            emitLine(format("{} = load {}, ptr {}",
                               valueReg, varIt->second.llvmType, it->second));
            valueMap_["__last_expr"] = valueReg;
            // Track the type of the loaded value
            registerTypes_[valueReg] = varIt->second.type;

            // IR: Create load instruction for local variable
            if (builder_) {
                auto allocaIt = localAllocas_.find(node.name);
                if (allocaIt != localAllocas_.end()) {
                    IR::Type* loadTy = getIRType(varIt->second.type);
                    lastExprValue_ = builder_->CreateLoad(loadTy, allocaIt->second, node.name);
                }
            }
        } else {
            // Fallback: just use the register (for backwards compatibility)
            // Note: Reference parameters (Integer&) receive object pointers directly
            // like owned parameters - the & is semantic only (no ownership transfer)
            valueMap_["__last_expr"] = it->second;

            // IR: Try to get the IR value from irValues_ map
            if (builder_) {
                auto irIt = irValues_.find(node.name);
                if (irIt != irValues_.end()) {
                    lastExprValue_ = irIt->second;
                }
            }
        }
    } else if (!currentClassName_.empty()) {
        // Check if this is a property of the current class
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            const auto& properties = classIt->second.properties;
            for (size_t i = 0; i < properties.size(); ++i) {
                const std::string& propName = std::get<0>(properties[i]);
                const std::string& llvmType = std::get<1>(properties[i]);
                const std::string& xxmlType = std::get<2>(properties[i]);

                if (propName == node.name) {
                    // This is a property access - generate getelementptr and load
                    std::string ptrReg = allocateRegister();
                    // Mangle class name using TypeNormalizer
                    std::string mangledClassName = TypeNormalizer::mangleForLLVM(currentClassName_);
                    emitLine(format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                       ptrReg, mangledClassName, i));

                    std::string valueReg = allocateRegister();
                    emitLine(format("{} = load {}, ptr {}",
                                       valueReg, llvmType, ptrReg));

                    valueMap_["__last_expr"] = valueReg;

                    // Store the XXML type for method call resolution
                    // This is critical for template instantiations where T -> ConcreteType
                    if (!xxmlType.empty()) {
                        registerTypes_[valueReg] = xxmlType;
                    } else if (llvmType == "ptr") {
                        registerTypes_[valueReg] = "NativeType<\"ptr\">";
                    } else if (llvmType == "i64") {
                        registerTypes_[valueReg] = "NativeType<\"int64\">";
                    } else if (llvmType == "i32") {
                        registerTypes_[valueReg] = "NativeType<\"int32\">";
                    } else if (llvmType == "i16") {
                        registerTypes_[valueReg] = "NativeType<\"int16\">";
                    } else if (llvmType == "i8") {
                        registerTypes_[valueReg] = "NativeType<\"int8\">";
                    } else if (llvmType == "i1") {
                        registerTypes_[valueReg] = "NativeType<\"bool\">";
                    } else if (llvmType == "float") {
                        registerTypes_[valueReg] = "float";
                    } else if (llvmType == "double") {
                        registerTypes_[valueReg] = "double";
                    } else {
                        registerTypes_[valueReg] = node.name; // Fallback to property name
                    }

                    // IR: GEP + Load for implicit this.property access
                    if (builder_ && currentFunction_) {
                        IR::Value* thisPtr = currentFunction_->getArg(0);
                        if (thisPtr) {
                            IR::Type* fieldTy = (llvmType == "ptr")
                                ? static_cast<IR::Type*>(builder_->getPtrTy())
                                : static_cast<IR::Type*>(builder_->getInt64Ty());
                            IR::Value* fieldPtr = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(i), node.name + "_implicit_ptr");
                            lastExprValue_ = builder_->CreateLoad(fieldTy, fieldPtr, node.name + "_implicit");
                        }
                        return;
                    }

                    // Legacy fallback: New IR: GEP + Load for property access
                    if (builder_ && currentFunction_) {
                        IR::StructType* classTy = getOrCreateClassType(currentClassName_);
                        if (classTy && currentFunction_->getNumArgs() > 0) {
                            IR::Value* thisPtr = currentFunction_->getArg(0);
                            IR::Value* fieldPtr = builder_->CreateStructGEP(classTy, thisPtr, i, node.name + "_ptr");
                            IR::Type* fieldTy = getIRType(llvmType);
                            lastExprValue_ = builder_->CreateLoad(fieldTy, fieldPtr, node.name);
                        }
                    }
                    return;
                }
            }
        }

        // Not a property - must be a global identifier
        valueMap_["__last_expr"] = format("@{}", node.name);

        // IR: Try to get global value
        if (builder_ && module_) {
            lastExprValue_ = module_->getGlobalVariable(node.name);
        }
    } else {
        // No current class context - must be a global identifier
        valueMap_["__last_expr"] = format("@{}", node.name);

        // IR: Try to get global value
        if (builder_ && module_) {
            lastExprValue_ = module_->getGlobalVariable(node.name);
        }
    }

    // New IR infrastructure: try to get value using helper
    if (builder_) {
        lastExprValue_ = getIRValue(node.name);
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
            // IR: Return the alloca address
            if (builder_) {
                auto allocaIt = localAllocas_.find(ident->name);
                if (allocaIt != localAllocas_.end()) {
                    lastExprValue_ = allocaIt->second;
                }
            }
            return;
        }

        // Check if it's a parameter
        auto paramIt = valueMap_.find(ident->name);
        if (paramIt != valueMap_.end()) {
            // For parameters, we might need to allocate storage if not already done
            valueMap_["__last_expr"] = paramIt->second;
            // IR: Try to find alloca for parameter
            if (builder_) {
                auto allocaIt = localAllocas_.find(ident->name);
                if (allocaIt != localAllocas_.end()) {
                    lastExprValue_ = allocaIt->second;
                }
            }
            return;
        }

        // Check if it's a property of the current class (implicit 'this')
        if (!currentClassName_.empty() && valueMap_.count("this")) {
            auto classIt = classes_.find(currentClassName_);
            if (classIt != classes_.end()) {
                const auto& properties = classIt->second.properties;
                for (size_t i = 0; i < properties.size(); ++i) {
                    if (std::get<0>(properties[i]) == ident->name) {
                        // Generate getelementptr to get address of the property
                        std::string fieldPtr = allocateRegister();
                        // Mangle class name for LLVM type using TypeNormalizer
                        std::string mangledClassName = TypeNormalizer::mangleForLLVM(currentClassName_);
                        emitLine(fieldPtr + " = getelementptr inbounds %class." + mangledClassName +
                                ", ptr %this, i32 0, i32 " + std::to_string(i));

                        // IR: Create StructGEP for this.property reference
                        if (builder_ && currentFunction_) {
                            IR::Value* thisPtr = currentFunction_->getArg(0);
                            if (thisPtr) {
                                lastExprValue_ = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(i), "this_prop_ref");
                            }
                        }

                        valueMap_["__last_expr"] = fieldPtr;
                        // IR: Create GEP for this.property
                        if (builder_ && currentFunction_) {
                            IR::Value* thisPtr = currentFunction_->getArg(0);
                            if (thisPtr) {
                                lastExprValue_ = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(i), ident->name + "_ref");
                            }
                        }
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
                        if (std::get<0>(properties[i]) == memberAccess->member) {
                            // Generate getelementptr to get address of the property
                            std::string objPtr = allocateRegister();
                            emitLine(objPtr + " = load ptr, ptr " + varIt->second.llvmRegister);

                            // IR: Load object pointer for reference
                            if (builder_) {
                                auto allocaIt = localAllocas_.find(objIdent->name);
                                if (allocaIt != localAllocas_.end()) {
                                    lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, "obj_ref_load");
                                }
                            }

                            std::string fieldPtr = allocateRegister();
                            std::string mangledTypeName = TypeNormalizer::mangleForLLVM(typeName);
                            emitLine(fieldPtr + " = getelementptr inbounds %class." + mangledTypeName +
                                    ", ptr " + objPtr + ", i32 0, i32 " + std::to_string(i));

                            // IR: Load object, GEP to field
                            if (builder_) {
                                auto allocaIt = localAllocas_.find(objIdent->name);
                                if (allocaIt != localAllocas_.end()) {
                                    IR::Value* objPtrIR = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, "ref_obj");
                                    lastExprValue_ = builder_->CreateStructGEP(builder_->getPtrTy(), objPtrIR, static_cast<unsigned>(i), memberAccess->member + "_ref");
                                }
                            }

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
    // Strip :: prefix from member if present (parser may include it)
    std::string memberName = node.member;
    if (memberName.length() >= 2 && memberName.substr(0, 2) == "::") {
        memberName = memberName.substr(2);
    }

    // Check if this is an enum value access (EnumName::VALUE or Namespace::EnumName::VALUE)
    if (auto* idExpr = dynamic_cast<Parser::IdentifierExpr*>(node.object.get())) {
        // Build the fully-qualified enum key
        std::string enumKey;
        if (!currentNamespace_.empty()) {
            // Try namespace-qualified first
            enumKey = currentNamespace_ + "::" + idExpr->name + "::" + memberName;
            auto it = enumValues_.find(enumKey);
            if (it != enumValues_.end()) {
                // Found the enum value - return it as an integer constant
                valueMap_["__last_expr"] = std::to_string(it->second);
                registerTypes_[valueMap_["__last_expr"]] = "Integer";
                // IR: Set lastExprValue_ to integer constant
                if (builder_) {
                    lastExprValue_ = builder_->getInt64(it->second);
                }
                return;
            }
        }
        // Try unqualified
        enumKey = idExpr->name + "::" + memberName;
        auto it = enumValues_.find(enumKey);
        if (it != enumValues_.end()) {
            valueMap_["__last_expr"] = std::to_string(it->second);
            registerTypes_[valueMap_["__last_expr"]] = "Integer";
            // IR: Set lastExprValue_ to integer constant
            if (builder_) {
                lastExprValue_ = builder_->getInt64(it->second);
            }
            return;
        }
    }

    // Handle Namespace::EnumName::VALUE pattern (nested MemberAccessExpr)
    if (auto* outerMember = dynamic_cast<Parser::MemberAccessExpr*>(node.object.get())) {
        // Check if the outer object is a namespace identifier
        if (auto* nsIdExpr = dynamic_cast<Parser::IdentifierExpr*>(outerMember->object.get())) {
            // Build key: Namespace::EnumName::Value
            std::string outerMemberName = outerMember->member;
            if (outerMemberName.length() >= 2 && outerMemberName.substr(0, 2) == "::") {
                outerMemberName = outerMemberName.substr(2);
            }
            std::string enumKey = nsIdExpr->name + "::" + outerMemberName + "::" + memberName;
            auto it = enumValues_.find(enumKey);
            if (it != enumValues_.end()) {
                // Found the enum value - return it as an integer constant
                valueMap_["__last_expr"] = std::to_string(it->second);
                registerTypes_[valueMap_["__last_expr"]] = "Integer";
                if (builder_) {
                    lastExprValue_ = builder_->getInt64(it->second);
                }
                return;
            }
        }
    }

    // Check if this is a property access or method call
    // Method calls are handled by CallExpr, but direct property access needs handling here

    // Evaluate the object expression
    node.object->accept(*this);
    std::string objectReg = valueMap_["__last_expr"];

    if (objectReg.empty() || objectReg == "null") {
        valueMap_["__last_expr"] = "null";
        return;
    }

    // Check if this is 'this' expression (member access on current object)
    if (dynamic_cast<Parser::ThisExpr*>(node.object.get())) {
        // Access property on 'this'
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            // Find the property index
            const auto& properties = classIt->second.properties;
            for (size_t i = 0; i < properties.size(); ++i) {
                if (std::get<0>(properties[i]) == memberName) {
                    // Generate getelementptr to access the property
                    std::string ptrReg = allocateRegister();

                    // Mangle class name using TypeNormalizer
                    std::string mangledTypeName = TypeNormalizer::mangleForLLVM(currentClassName_);
                    emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                            ", ptr %this, i32 0, i32 " + std::to_string(i));

                    // IR: GEP for this.property
                    IR::Value* fieldPtrIR = nullptr;
                    if (builder_ && currentFunction_) {
                        IR::Value* thisPtr = currentFunction_->getArg(0);
                        if (thisPtr) {
                            fieldPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(i), memberName + "_this_gep");
                        }
                    }

                    // Load the field value
                    std::string valueReg = allocateRegister();
                    std::string llvmType = std::get<1>(properties[i]); // LLVM type (ptr, i64, etc.)
                    std::string xxmlType = std::get<2>(properties[i]); // XXML type (KeyValue, Integer, etc.)
                    emitLine(valueReg + " = load " + llvmType + ", ptr " + ptrReg);

                    valueMap_["__last_expr"] = valueReg;
                    // Store the XXML type for method resolution
                    // The xxmlType is the original XXML type name (e.g., "KeyValue", "Integer")
                    // which is needed to find the correct class methods
                    if (!xxmlType.empty()) {
                        registerTypes_[valueReg] = xxmlType;
                    } else {
                        // Fallback for properties without xxmlType
                        registerTypes_[valueReg] = std::get<0>(properties[i]);
                    }

                    // IR: Load for this.property
                    if (builder_ && fieldPtrIR) {
                        IR::Type* loadTy = (llvmType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                        lastExprValue_ = builder_->CreateLoad(loadTy, fieldPtrIR, memberName + "_this_val");
                    }

                    // IR: Create GEP and Load for this.property access
                    if (builder_ && currentFunction_) {
                        IR::Value* thisPtr = currentFunction_->getArg(0);
                        if (thisPtr) {
                            IR::Type* fieldTy = (llvmType == "ptr")
                                ? static_cast<IR::Type*>(builder_->getPtrTy())
                                : static_cast<IR::Type*>(builder_->getInt64Ty());
                            // Note: Simplified GEP - full struct type support would need more work
                            IR::Value* fieldPtr = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(i), memberName + "_ptr");
                            lastExprValue_ = builder_->CreateLoad(fieldTy, fieldPtr, memberName);
                        }
                    }
                    return;
                }
            }
        }
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
                    if (std::get<0>(properties[i]) == memberName) {
                        // Generate getelementptr to access the property
                        std::string ptrReg = allocateRegister();

                        // Load the object pointer first
                        std::string objPtr = allocateRegister();
                        emitLine(objPtr + " = load ptr, ptr " + varIt->second.llvmRegister);

                        // Get pointer to the field - mangle class name using TypeNormalizer
                        std::string mangledTypeName = TypeNormalizer::mangleForLLVM(typeName);
                        emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                ", ptr " + objPtr + ", i32 0, i32 " + std::to_string(i));

                        // IR: GEP for obj.property
                        IR::Value* objFieldPtrIR = nullptr;
                        if (builder_ && lastExprValue_) {
                            objFieldPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), lastExprValue_, static_cast<unsigned>(i), memberName + "_obj_gep");
                        }

                        // Load the field value
                        std::string valueReg = allocateRegister();
                        std::string llvmType = std::get<1>(properties[i]); // LLVM type (ptr, i64, etc.)
                        std::string xxmlType = std::get<2>(properties[i]); // XXML type (KeyValue, Integer, etc.)
                        emitLine(valueReg + " = load " + llvmType + ", ptr " + ptrReg);

                        valueMap_["__last_expr"] = valueReg;
                        // Store the XXML type for method resolution
                        if (!xxmlType.empty()) {
                            registerTypes_[valueReg] = xxmlType;
                        } else {
                            // Fallback for properties without xxmlType
                            registerTypes_[valueReg] = std::get<0>(properties[i]);
                        }

                        // IR: Load for obj.property
                        if (builder_ && objFieldPtrIR) {
                            IR::Type* loadTy = (llvmType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                            lastExprValue_ = builder_->CreateLoad(loadTy, objFieldPtrIR, memberName + "_obj_val");
                        }

                        // IR: Load object, GEP to field, load field
                        if (builder_) {
                            auto allocaIt = localAllocas_.find(ident->name);
                            if (allocaIt != localAllocas_.end()) {
                                IR::Value* objPtrIR = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, ident->name + "_obj");
                                IR::Type* fieldTy = (llvmType == "ptr")
                                    ? static_cast<IR::Type*>(builder_->getPtrTy())
                                    : static_cast<IR::Type*>(builder_->getInt64Ty());
                                IR::Value* fieldPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), objPtrIR, static_cast<unsigned>(i), memberName + "_gep");
                                lastExprValue_ = builder_->CreateLoad(fieldTy, fieldPtrIR, memberName + "_val");
                            }
                        }
                        return;
                    }
                }
            }
        }
    }

    // If we get here, property not found or not supported - store member name for CallExpr to handle
    valueMap_["__last_expr"] = memberName;
}

void LLVMBackend::visit(Parser::CallExpr& node) {
    // DEBUG: Track recursion depth
    static int callExprDepth = 0;
    callExprDepth++;
    if (callExprDepth > 100) {
        std::cerr << "ERROR: CallExpr recursion depth exceeded 100! Breaking infinite loop.\n";
        callExprDepth--;
        valueMap_["__last_expr"] = "null";
        return;
    }
    struct DepthGuard { ~DepthGuard() { callExprDepth--; } } guard;

    // NEW: Try compile-time method evaluation first (constant folding)
    // This evaluates method calls like x.add(y) on compile-time values at compile-time
    if (semanticAnalyzer_) {
        Semantic::CompiletimeInterpreter interpreter(*semanticAnalyzer_);
        // Propagate known compile-time values to the interpreter
        for (const auto& [name, ctValue] : compiletimeValues_) {
            interpreter.setVariable(name, ctValue->clone());
        }

        auto ctResult = interpreter.evaluate(&node);
        if (ctResult) {
            // Method call evaluated at compile-time!
            if (ctResult->isInteger()) {
                auto* intVal = static_cast<Semantic::CompiletimeInteger*>(ctResult.get());
                // Check if this is a Constructor call that returns an object type (Integer^)
                // If so, we need to wrap the folded value with Integer_Constructor
                bool isConstructorCall = false;
                if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
                    // Parser stores member as "::Constructor" for static calls
                    isConstructorCall = (memberAccess->member == "Constructor" || memberAccess->member == "::Constructor");
                }
                if (isConstructorCall) {
                    // Constructor returns Integer^ (object), so wrap with Integer_Constructor
                    std::string valueReg = allocateRegister();
                    emitLine(format("; Compile-time folded Integer::Constructor to Integer object"));
                    emitLine(format("{} = call ptr @Integer_Constructor(i64 {})", valueReg, intVal->value));
                    valueMap_["__last_expr"] = valueReg;
                    registerTypes_[valueReg] = "Integer";
                    if (builder_ && module_) {
                        IR::Value* i64Val = builder_->getInt64(intVal->value);
                        IR::Function* intCtor = module_->getFunction("Integer_Constructor");
                        if (!intCtor) {
                            // Create external declaration if not found
                            auto& ctx = module_->getContext();
                            auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                            intCtor = module_->createFunction(funcTy, "Integer_Constructor", IR::Function::Linkage::External);
                        }
                        lastExprValue_ = builder_->CreateCall(intCtor, {i64Val}, "ct_int");
                    }
                } else {
                    // Raw integer result, emit as i64
                    emitLine(format("; Compile-time folded method call to i64 {}", intVal->value));
                    valueMap_["__last_expr"] = std::to_string(intVal->value);
                    if (builder_) {
                        lastExprValue_ = builder_->getInt64(intVal->value);
                    }
                }
                return;  // NO RUNTIME METHOD CALL!
            }
            if (ctResult->isBool()) {
                auto* boolVal = static_cast<Semantic::CompiletimeBool*>(ctResult.get());
                // Check if this is a Constructor call that returns an object type (Bool^)
                // If so, we need to wrap the folded value with Bool_Constructor
                bool isConstructorCall = false;
                if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
                    // Parser stores member as "::Constructor" for static calls
                    isConstructorCall = (memberAccess->member == "Constructor" || memberAccess->member == "::Constructor");
                }
                if (isConstructorCall) {
                    // Constructor returns Bool^ (object), so wrap with Bool_Constructor
                    std::string valueReg = allocateRegister();
                    emitLine(format("; Compile-time folded Bool::Constructor to Bool object"));
                    emitLine(format("{} = call ptr @Bool_Constructor(i1 {})", valueReg, boolVal->value ? "true" : "false"));
                    valueMap_["__last_expr"] = valueReg;
                    registerTypes_[valueReg] = "Bool";
                    if (builder_ && module_) {
                        IR::Value* i1Val = builder_->getInt1(boolVal->value);
                        IR::Function* boolCtor = module_->getFunction("Bool_Constructor");
                        if (!boolCtor) {
                            // Create external declaration if not found
                            auto& ctx = module_->getContext();
                            auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt1Ty()}, false);
                            boolCtor = module_->createFunction(funcTy, "Bool_Constructor", IR::Function::Linkage::External);
                        }
                        lastExprValue_ = builder_->CreateCall(boolCtor, {i1Val}, "ct_bool");
                    }
                } else {
                    // Raw bool result (e.g., comparison), emit as i1
                    emitLine(format("; Compile-time folded method call to i1 {}", boolVal->value ? "true" : "false"));
                    valueMap_["__last_expr"] = boolVal->value ? "true" : "false";
                    if (builder_) {
                        lastExprValue_ = builder_->getInt1(boolVal->value);
                    }
                }
                return;
            }
            if (ctResult->isFloat()) {
                auto* floatVal = static_cast<Semantic::CompiletimeFloat*>(ctResult.get());
                // Check if this is a Constructor call that returns an object type (Float^)
                bool isConstructorCall = false;
                if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
                    // Parser stores member as "::Constructor" for static calls
                    isConstructorCall = (memberAccess->member == "Constructor" || memberAccess->member == "::Constructor");
                }
                if (isConstructorCall) {
                    // DON'T fold Float::Constructor - let it emit the actual call
                    // This avoids type mismatch issues between compile-time and runtime types
                    // Fall through to regular method call handling
                } else {
                    // Raw float result
                    emitLine(format("; Compile-time folded method call to float"));
                    std::ostringstream ss;
                    ss << std::hexfloat << floatVal->value;
                    valueMap_["__last_expr"] = ss.str();
                    if (builder_) {
                        lastExprValue_ = builder_->getFloat(floatVal->value);
                    }
                    return;
                }
            }
            if (ctResult->isDouble()) {
                auto* doubleVal = static_cast<Semantic::CompiletimeDouble*>(ctResult.get());
                // Check if this is a Constructor call that returns an object type (Double^)
                bool isConstructorCall = false;
                if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
                    // Parser stores member as "::Constructor" for static calls
                    isConstructorCall = (memberAccess->member == "Constructor" || memberAccess->member == "::Constructor");
                }
                if (isConstructorCall) {
                    // DON'T fold Double::Constructor - let it emit the actual call
                    // This avoids type mismatch issues between compile-time and runtime types
                    // Fall through to regular method call handling
                } else {
                    // Raw double result
                    emitLine(format("; Compile-time folded method call to double"));
                    std::ostringstream ss;
                    ss << std::hexfloat << doubleVal->value;
                    valueMap_["__last_expr"] = ss.str();
                    if (builder_) {
                        lastExprValue_ = builder_->getDouble(doubleVal->value);
                    }
                    return;
                }
            }
            if (ctResult->isString()) {
                auto* strVal = static_cast<Semantic::CompiletimeString*>(ctResult.get());
                // Emit global string constant
                std::string label = getOrCreateGlobalString(strVal->value);
                std::string ptrReg = allocateRegister();
                emitLine(format("; Compile-time folded method call to string \"{}\"", strVal->value));
                stringLiterals_.push_back({label, strVal->value});
                // Reference global with @. prefix
                emitLine(format("{} = getelementptr inbounds [{} x i8], ptr @.{}, i64 0, i64 0",
                               ptrReg, strVal->value.length() + 1, label));

                // Call String_Constructor with the constant string
                std::string valueReg = allocateRegister();
                emitLine(format("{} = call ptr @String_Constructor(ptr {})", valueReg, ptrReg));
                valueMap_["__last_expr"] = valueReg;
                registerTypes_[valueReg] = "String";

                if (builder_ && module_) {
                    IR::Value* strConstPtr = module_->getOrCreateStringLiteral(strVal->value);
                    IR::Function* strCtor = module_->getFunction("String_Constructor");
                    if (strCtor && strConstPtr) {
                        lastExprValue_ = builder_->CreateCall(strCtor, {strConstPtr}, "ct_str");
                    }
                }
                return;
            }
            // For other types, fall through to runtime evaluation
        }
    }

    // Check for lambda .call() invocation: lambdaVar.call(args) or lambdaVar<Type>.call(args)
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        if (memberAccess->member == "call") {
            if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
                // Check for template lambda call: identity<Integer>.call()
                // The identifier will contain the template args like "identity<Integer>"
                if (ident->name.find('<') != std::string::npos) {
                    auto templateIt = templateLambdaFunctions_.find(ident->name);
                    if (templateIt != templateLambdaFunctions_.end()) {
                        std::string funcName = templateIt->second;
                        emitLine(format("; Template lambda call: {}.call()", ident->name));

                        // Get lambda info for proper parameter types
                        auto infoIt = templateLambdaInfos_.find(ident->name);

                        // Extract base lambda variable name (e.g., "multiply" from "multiply<Integer>")
                        std::string baseName = ident->name.substr(0, ident->name.find('<'));

                        // Build argument list: closure + actual args
                        // For lambdas with captures, load the closure from the variable
                        std::vector<std::string> argRegs;
                        auto varIt = variables_.find(baseName);
                        if (varIt != variables_.end()) {
                            // Load closure pointer from the lambda variable
                            std::string closureReg = allocateRegister();
                            emitLine(format("{} = load ptr, ptr {}", closureReg, varIt->second.llvmRegister));
                            argRegs.push_back(closureReg);
                        } else {
                            // No variable found - lambda without captures, use null
                            argRegs.push_back("null");
                        }

                        for (const auto& arg : node.arguments) {
                            arg->accept(*this);
                            argRegs.push_back(valueMap_["__last_expr"]);
                        }

                        // Build call arguments string with proper types
                        std::stringstream callArgs;
                        if (infoIt != templateLambdaInfos_.end()) {
                            const auto& info = infoIt->second;
                            for (size_t i = 0; i < argRegs.size() && i < info.paramTypes.size(); ++i) {
                                if (i > 0) callArgs << ", ";
                                callArgs << info.paramTypes[i] << " " << argRegs[i];
                            }
                        } else {
                            // Fallback: all ptrs
                            for (size_t i = 0; i < argRegs.size(); ++i) {
                                if (i > 0) callArgs << ", ";
                                callArgs << "ptr " << argRegs[i];
                            }
                        }

                        // Determine return type
                        std::string returnType = "ptr";
                        if (infoIt != templateLambdaInfos_.end()) {
                            returnType = infoIt->second.returnType;
                        }

                        // Generate direct call to the template lambda function
                        std::string resultReg = allocateRegister();
                        emitLine(format("{} = call {} {}({})", resultReg, returnType, funcName, callArgs.str()));
                        valueMap_["__last_expr"] = resultReg;
                        registerTypes_[resultReg] = returnType;

                        return;
                    }
                }

                // Check if the identifier is a lambda/function variable
                auto varIt = variables_.find(ident->name);
                if (varIt != variables_.end()) {
                    // Check if the variable's type is __function (may be wrapped in ptr_to<>)
                    auto typeIt = registerTypes_.find(varIt->second.llvmRegister);
                    bool isFunction = false;
                    if (typeIt != registerTypes_.end()) {
                        std::string typeName = typeIt->second;
                        // Check for both direct __function and ptr_to<__function>
                        isFunction = (typeName == "__function" ||
                                     typeName.find("__function") != std::string::npos);
                    }
                    // Also check the variable's declared type
                    if (!isFunction && varIt->second.type == "__function") {
                        isFunction = true;
                    }

                    if (isFunction) {
                        emitLine(format("; Lambda call: {}.call()", ident->name));

                        // Load the closure pointer from the variable
                        std::string closurePtrReg = varIt->second.llvmRegister;
                        std::string closureReg = allocateRegister();
                        emitLine(format("{} = load ptr, ptr {}", closureReg, closurePtrReg));

                        // IR: Load closure pointer
                        if (builder_) {
                            auto allocaIt = localAllocas_.find(ident->name);
                            if (allocaIt != localAllocas_.end()) {
                                lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, "closure_load");
                            }
                        }

                        // Load function pointer from closure (offset 0)
                        std::string funcPtrSlot = allocateRegister();
                        std::string funcPtr = allocateRegister();
                        emitLine(format("{} = getelementptr inbounds {{ ptr }}, ptr {}, i32 0, i32 0",
                                       funcPtrSlot, closureReg));
                        emitLine(format("{} = load ptr, ptr {}", funcPtr, funcPtrSlot));

                        // IR: GEP and load for function pointer from closure
                        if (builder_ && lastExprValue_) {
                            IR::Value* funcSlot = builder_->CreateGEP(builder_->getPtrTy(), lastExprValue_, {builder_->getInt32(0), builder_->getInt32(0)}, "func_slot");
                            lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), funcSlot, "func_ptr");
                        }

                        // Build argument list: closure + actual args
                        std::vector<std::string> argRegs;
                        argRegs.push_back(closureReg);  // First arg is closure pointer

                        for (const auto& arg : node.arguments) {
                            arg->accept(*this);
                            argRegs.push_back(valueMap_["__last_expr"]);
                        }

                        // Generate indirect call with all pointer arguments
                        std::stringstream callArgs;
                        for (size_t i = 0; i < argRegs.size(); ++i) {
                            if (i > 0) callArgs << ", ";
                            callArgs << "ptr " << argRegs[i];
                        }

                        // Call returns ptr (function type returns are always objects)
                        std::string resultReg = allocateRegister();
                        emitLine(format("{} = call ptr {}({})", resultReg, funcPtr, callArgs.str()));
                        valueMap_["__last_expr"] = resultReg;
                        registerTypes_[resultReg] = "ptr";

                        // IR: Indirect call for lambda - create placeholder call
                        if (builder_ && module_) {
                            // Create a function type for the indirect call
                            auto& ctx = module_->getContext();
                            std::vector<IR::Type*> argTypes(argRegs.size(), builder_->getPtrTy());
                            auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), argTypes, false);
                            // Note: Full indirect call would need function pointer value
                            // For now, set result to null as placeholder
                            lastExprValue_ = builder_->getNullPtr();
                        }
                        return;
                    }
                }
            }
        }
    }

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

            // Check if it's a property of the current class
            bool isProperty = false;
            if (varIt == variables_.end() && paramIt == valueMap_.end() && !currentClassName_.empty()) {
                auto classIt = classes_.find(currentClassName_);
                if (classIt != classes_.end()) {
                    for (const auto& prop : classIt->second.properties) {
                        if (std::get<0>(prop) == ident->name) {
                            isProperty = true;
                            break;
                        }
                    }
                }
            }

            if (isProperty) {
                // Instance method on property: this.property.method() or just property.method()
                isInstanceMethod = true;

                // Find the property to get its type
                auto classIt = classes_.find(currentClassName_);
                std::string propTypeName;
                size_t propIndex = 0;
                for (size_t i = 0; i < classIt->second.properties.size(); ++i) {
                    if (std::get<0>(classIt->second.properties[i]) == ident->name) {
                        propTypeName = std::get<2>(classIt->second.properties[i]);  // XXML type for method resolution
                        propIndex = i;
                        break;
                    }
                }

                // Load the property value from this
                std::string ptrReg = allocateRegister();
                std::string mangledTypeName = TypeNormalizer::mangleForLLVM(currentClassName_);
                emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                        ", ptr %this, i32 0, i32 " + std::to_string(propIndex));

                // IR: GEP for property in CallExpr
                IR::Value* callPropPtrIR = nullptr;
                if (builder_ && currentFunction_) {
                    IR::Value* thisPtr = currentFunction_->getArg(0);
                    if (thisPtr) {
                        callPropPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(propIndex), "call_prop_ptr");
                    }
                }

                // Load the property value
                instanceRegister = allocateRegister();
                std::string llvmType = std::get<1>(classIt->second.properties[propIndex]);
                emitLine(instanceRegister + " = load " + llvmType + ", ptr " + ptrReg);

                // IR: Load for property in CallExpr
                if (builder_ && callPropPtrIR) {
                    IR::Type* loadTy = (llvmType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                    lastExprValue_ = builder_->CreateLoad(loadTy, callPropPtrIR, "call_prop_val");
                }

                // IR: GEP + Load for property access in CallExpr
                if (builder_ && currentFunction_) {
                    IR::Value* thisPtr = currentFunction_->getArg(0);
                    if (thisPtr) {
                        IR::Value* propPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtr, static_cast<unsigned>(propIndex), "prop_ptr");
                        IR::Type* loadTy = (llvmType == "ptr") ? static_cast<IR::Type*>(builder_->getPtrTy()) : static_cast<IR::Type*>(builder_->getInt64Ty());
                        lastExprValue_ = builder_->CreateLoad(loadTy, propPtrIR, "prop_val");
                    }
                }

                // Use the actual XXML type from the property registration
                // propTypeName is the xxmlType (e.g., "FullKeyType", "Integer") from ClassInfo
                // Strip ownership suffix using TypeNormalizer
                std::string className = TypeNormalizer::stripOwnershipMarker(propTypeName);

                // Register the type so semantic lookup can find it
                registerTypes_[instanceRegister] = className + "^";

                // Generate qualified method name (sanitize for template arguments)
                functionName = className + "_" + sanitizeMethodNameForLLVM(memberAccess->member);
            } else if (varIt != variables_.end()) {
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

                // Strip ownership modifiers and mangle using TypeNormalizer
                className = TypeNormalizer::stripOwnershipMarker(className);
                className = TypeNormalizer::mangleForLLVM(className);

                // Generate qualified method name
                // Special handling for compiler intrinsic types (ReflectionContext, CompilationContext)
                if (className == "ReflectionContext" || className == "CompilationContext") {
                    className = "Processor";
                }

                functionName = className + "_" + sanitizeMethodNameForLLVM(memberAccess->member);
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

                    // Strip ownership modifiers (^, %, &) before mangling for function name
                    // Note: Reference parameters (&) receive object pointers directly
                    // Strip ownership modifiers and mangle using TypeNormalizer
                    className = TypeNormalizer::stripOwnershipMarker(className);
                    className = TypeNormalizer::mangleForLLVM(className);

                    // Special handling for compiler intrinsic types (ReflectionContext, CompilationContext)
                    if (className == "ReflectionContext" || className == "CompilationContext") {
                        className = "Processor";
                    }

                    functionName = className + "_" + sanitizeMethodNameForLLVM(memberAccess->member);
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

                // Mangle class name using TypeNormalizer
                className = TypeNormalizer::mangleForLLVM(className);

                functionName = className + memberAccess->member;
                // Mangle the full function name for C runtime compatibility
                functionName = TypeNormalizer::mangleForLLVM(functionName);
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

                // Mangle class name using TypeNormalizer
                className = TypeNormalizer::mangleForLLVM(className);

                // Strip :: prefix from member name if present
                std::string memberName = memberAccess->member;
                if (memberName.substr(0, 2) == "::") {
                    memberName = memberName.substr(2);
                }

                functionName = className + "_" + sanitizeMethodNameForLLVM(memberName);
            } else {
                // Could be either:
                // 1. Static method on nested namespace: System::Console::printLine
                // 2. Instance method on complex expression: obj.method1().method2()

                // Try to extract as a static type name first
                std::string staticClassName = extractTypeName(memberAccess->object.get());

                // Check if this is actually a variable/property, not a type name
                bool isVariable = false;
                if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
                    isVariable = (variables_.find(ident->name) != variables_.end()) ||
                                 (valueMap_.find(ident->name) != valueMap_.end());

                    // Also check if it's a property of the current class
                    if (!isVariable && !currentClassName_.empty()) {
                        auto classIt = classes_.find(currentClassName_);
                        if (classIt != classes_.end()) {
                            for (const auto& prop : classIt->second.properties) {
                                if (std::get<0>(prop) == ident->name) {
                                    isVariable = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!staticClassName.empty() && !isVariable) {
                    // This looks like a static method call (e.g., System::Console::printLine)
                    // Mangle the namespace/class name using TypeNormalizer
                    std::string mangledClassName = TypeNormalizer::mangleForLLVM(staticClassName);

                    // Strip :: prefix from member name if present
                    std::string memberName = memberAccess->member;
                    if (memberName.length() >= 2 && memberName.substr(0, 2) == "::") {
                        memberName = memberName.substr(2);
                    }

                    functionName = mangledClassName + "_" + sanitizeMethodNameForLLVM(memberName);

                    // For static method calls on user-defined classes, pass null for 'this'
                    // since the method is generated with a 'this' parameter
                    // Check if this is a user-defined class by looking for classes in IO, etc.
                    // (not built-in types like String, Integer, Bool, etc.)
                    static const std::unordered_set<std::string> builtinTypes = {
                        "Integer", "String", "Bool", "Float", "Double", "None",
                        "Console", "System", "Syscall", "Mem"
                    };
                    std::string baseClass = staticClassName;
                    size_t lastColon = baseClass.rfind("::");
                    if (lastColon != std::string::npos) {
                        baseClass = baseClass.substr(lastColon + 2);
                    }
                    if (builtinTypes.find(baseClass) == builtinTypes.end()) {
                        // User-defined class static method - need to pass null for 'this'
                        isInstanceMethod = true;
                        instanceRegister = "null";
                    }
                } else {
                    // Fallback: Instance method on complex expression (e.g., obj.method1().method2())
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

                    // Strip ownership modifiers and mangle using TypeNormalizer
                    className = TypeNormalizer::stripOwnershipMarker(className);
                    className = TypeNormalizer::mangleForLLVM(className);

                    functionName = className + "_" + sanitizeMethodNameForLLVM(memberAccess->member);
                }
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
    std::vector<IR::Value*> irArgValues;  // IR values for new IR infrastructure

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

            // IR: Load instance from variable
            if (builder_ && lastExprValue_) {
                lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), lastExprValue_, "inst_load");
            }

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

    // Check if this is an FFI native method call (for string marshalling)
    auto nativeMethodIt = nativeMethods_.find(functionName);
    bool isNativeFFICall = (nativeMethodIt != nativeMethods_.end());

    for (size_t argIdx = 0; argIdx < node.arguments.size(); ++argIdx) {
        auto& arg = node.arguments[argIdx];

        // FFI string marshalling: if calling a native method with a ptr parameter
        // and passing a string literal, pass the raw C string pointer directly
        if (isNativeFFICall && argIdx < nativeMethodIt->second.isStringPtr.size() &&
            nativeMethodIt->second.isStringPtr[argIdx]) {
            // Check if argument is a string literal
            if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.get())) {
                // Generate a direct pointer to the string literal constant
                std::string strLabel = format("ffi.str.{}", labelCounter_++);
                stringLiterals_.push_back({strLabel, strLit->value});
                std::string ptrReg = "@." + strLabel;
                emitLine("; FFI string marshalling: \"" + strLit->value.substr(0, 20) +
                        (strLit->value.length() > 20 ? "..." : "") + "\"");
                argValues.push_back(ptrReg);
                continue;
            }
        }

        // FFI callback handling: if calling a native method with a callback parameter
        // and passing a lambda, store the lambda closure and pass the thunk function pointer
        if (isNativeFFICall && argIdx < nativeMethodIt->second.isCallback.size() &&
            nativeMethodIt->second.isCallback[argIdx]) {
            // Check if argument is a lambda expression
            if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(arg.get())) {
                // Generate the lambda closure first
                arg->accept(*this);
                std::string closureReg = valueMap_["__last_expr"];

                // Get the callback type name from the native method info
                // Strip ownership modifiers using TypeNormalizer
                std::string callbackTypeName = TypeNormalizer::stripOwnershipMarker(
                    nativeMethodIt->second.xxmlParamTypes[argIdx]);

                // Find the callback thunk info
                auto thunkIt = callbackThunks_.find(callbackTypeName);
                if (thunkIt != callbackThunks_.end()) {
                    // Mangle the callback type name for the global variable
                    std::string mangledName = TypeNormalizer::mangleForLLVM(callbackTypeName);
                    std::string closureGlobalName = "@xxml_callback_closure_" + mangledName;

                    // Store the lambda closure to the global variable
                    emitLine("; FFI callback: storing lambda closure for " + callbackTypeName);
                    emitLine("  store ptr " + closureReg + ", ptr " + closureGlobalName);

                    // Use the thunk function pointer as the argument
                    argValues.push_back(thunkIt->second.thunkFunctionName);
                    emitLine("; FFI callback: using thunk " + thunkIt->second.thunkFunctionName);
                    continue;
                } else {
                    emitLine("; WARNING: Callback type " + callbackTypeName + " not found, using lambda directly");
                }
            }
        }

        arg->accept(*this);
        std::string argReg = valueMap_["__last_expr"];

        // Capture IR value for new IR infrastructure
        if (lastExprValue_) {
            irArgValues.push_back(lastExprValue_);
        }

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

            // Check variable type to determine correct load type
            auto typeIt = registerTypes_.find(argReg);
            std::string loadType = "ptr";  // Default to ptr
            IR::Type* irLoadType = builder_ ? builder_->getPtrTy() : nullptr;
            std::string transferType = "";

            if (typeIt != registerTypes_.end()) {
                transferType = typeIt->second;
                // Strip ptr_to<> wrapper if present
                if (transferType.find("ptr_to<") == 0) {
                    size_t start = 7;  // Length of "ptr_to<"
                    size_t end = transferType.rfind('>');
                    if (end != std::string::npos) {
                        transferType = transferType.substr(start, end - start);
                    }
                }

                // Check if it's a NativeType - use correct LLVM type for loading
                if (transferType.find("NativeType<") == 0 || transferType.find("NativeType<\"") == 0) {
                    // Extract the native type from NativeType<"..."> or NativeType<...>
                    if (transferType.find("int32") != std::string::npos) {
                        loadType = "i32";
                        if (builder_) irLoadType = builder_->getInt32Ty();
                    } else if (transferType.find("int64") != std::string::npos) {
                        loadType = "i64";
                        if (builder_) irLoadType = builder_->getInt64Ty();
                    } else if (transferType.find("int8") != std::string::npos) {
                        loadType = "i8";
                        if (builder_) irLoadType = builder_->getInt8Ty();
                    } else if (transferType.find("float32") != std::string::npos ||
                               (transferType.find("float") != std::string::npos && transferType.find("float64") == std::string::npos)) {
                        loadType = "float";
                        if (builder_) irLoadType = builder_->getFloatTy();
                    } else if (transferType.find("float64") != std::string::npos || transferType.find("double") != std::string::npos) {
                        loadType = "double";
                        if (builder_) irLoadType = builder_->getDoubleTy();
                    } else if (transferType.find("bool") != std::string::npos) {
                        loadType = "i1";
                        if (builder_) irLoadType = builder_->getInt1Ty();
                    } else if (transferType.find("cstr") != std::string::npos || transferType.find("ptr") != std::string::npos) {
                        loadType = "ptr";
                        if (builder_) irLoadType = builder_->getPtrTy();
                    }
                }
            }

            emitLine(loadedReg + " = load " + loadType + ", ptr " + argReg);

            // IR: Load variable for FFI call
            if (builder_ && lastExprValue_ && irLoadType) {
                lastExprValue_ = builder_->CreateLoad(irLoadType, lastExprValue_, "ffi_arg_load");
            }

            // Transfer type information
            if (!transferType.empty()) {
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

        // Language_Reflection_* and Processor_* functions don't use xxml_ prefix
        if (syscallMethod.find("Language_Reflection_") == 0 ||
            syscallMethod.find("Processor_") == 0) {
            functionName = syscallMethod;  // Use directly without xxml_ prefix
        } else if (syscallMethod.find("xxml_") == 0) {
            // Already has xxml_ prefix (e.g., Syscall::xxml_string_create)
            functionName = syscallMethod;
        } else {
            functionName = "xxml_" + syscallMethod;  // Replace with "xxml_" prefix
        }
    }

    // Map System::Console methods to Console runtime functions
    // System_Console_printLine -> Console_printLine
    if (functionName.find("System_Console_") == 0) {
        std::string consoleMethod = functionName.substr(15);  // Remove "System_Console_" prefix
        functionName = "Console_" + consoleMethod;  // Replace with "Console_" prefix
    }

    // Map Integer XXML methods to runtime function names
    // Comparison methods are mapped to i1-returning runtime functions
    // The result will be wrapped in a Bool object after the call
    bool needsBoolWrapping = false;
    if (functionName.find("Integer_") == 0 || functionName.find("Language_Core_Integer_") == 0) {
        std::string baseName = functionName;
        // Strip Language_Core_ prefix if present
        if (baseName.find("Language_Core_") == 0) {
            baseName = baseName.substr(14);  // Remove "Language_Core_"
        }

        // Comparison operations - map to i1-returning runtime functions
        if (baseName == "Integer_lessThan") { functionName = "Integer_lt"; needsBoolWrapping = true; }
        else if (baseName == "Integer_lessThanOrEqual" || baseName == "Integer_lessOrEqual") { functionName = "Integer_le"; needsBoolWrapping = true; }
        else if (baseName == "Integer_greaterThan") { functionName = "Integer_gt"; needsBoolWrapping = true; }
        else if (baseName == "Integer_greaterThanOrEqual" || baseName == "Integer_greaterOrEqual") { functionName = "Integer_ge"; needsBoolWrapping = true; }
        else if (baseName == "Integer_equals") { functionName = "Integer_eq"; needsBoolWrapping = true; }
        else if (baseName == "Integer_notEquals") { functionName = "Integer_ne"; needsBoolWrapping = true; }
        // Arithmetic operations
        else if (baseName == "Integer_subtract") functionName = "Integer_sub";
        else if (baseName == "Integer_multiply") functionName = "Integer_mul";
        else if (baseName == "Integer_divide") functionName = "Integer_div";
    }

    // __DynamicValue comparison methods also return i1 and need Bool wrapping
    if (functionName == "__DynamicValue_greaterThan" ||
        functionName == "__DynamicValue_lessThan" ||
        functionName == "__DynamicValue_equals") {
        needsBoolWrapping = true;
    }

    // CRITICAL FIX: Detect constructor calls and allocate memory with malloc
    bool isConstructorCall = functionName.find("_Constructor") != std::string::npos;
    std::string className;

    // Extract className if this is a constructor call
    if (isConstructorCall) {
        size_t constructorPos = functionName.find("_Constructor");
        if (constructorPos != std::string::npos) {
            className = functionName.substr(0, constructorPos);
        }
    }

    // CRITICAL FIX: If calling a template base constructor (e.g., List_Constructor)
    // redirect to the actual template instantiation (e.g., List_Integer_Constructor)
    if (isConstructorCall && !isInstanceMethod && !className.empty()) {
        // Check if this might be a template base name by looking for an instantiation
        bool foundInstantiation = false;
        for (const auto& [registeredClassName, classInfo] : classes_) {
            if (registeredClassName.find(className + "_") == 0) {
                // Found a template instantiation - use it instead
                className = registeredClassName;
                foundInstantiation = true;
                break;
            }
        }
    }

    // Add parameter count mangling to constructor calls to support overloading
    // LLVM doesn't support function overloading, so Constructor_0, Constructor_2, etc.
    // BUT skip mangling for standard library types (declared in preamble or runtime)
    if (isConstructorCall && !isInstanceMethod && !className.empty()) {
        static const std::unordered_set<std::string> builtinTypes = {
            "Integer", "String", "Bool", "Float", "Double", "None"
        };

        size_t argCount = argValues.size();
        functionName = className + "_Constructor";

        // Extract base class name to check if it's built-in
        std::string baseClassName = className;
        size_t colonPos = baseClassName.rfind("::");
        if (colonPos != std::string::npos) {
            baseClassName = baseClassName.substr(colonPos + 2);
        }
        size_t templatePos = baseClassName.find('_');
        if (templatePos != std::string::npos) {
            baseClassName = baseClassName.substr(0, templatePos);
        }

        // Only mangle user-defined types, not built-in types
        if (builtinTypes.find(baseClassName) == builtinTypes.end()) {
            functionName += "_" + std::to_string(argCount);
        }
    }

    if (isConstructorCall && !isInstanceMethod && !className.empty()) {

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
            emitLine(format("{} = call ptr @xxml_malloc(i64 {})", mallocReg, classSize));

            // IR: Call xxml_malloc
            if (builder_ && module_) {
                IR::Function* mallocFunc = module_->getFunction("xxml_malloc");
                if (!mallocFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                    mallocFunc = module_->createFunction(funcTy, "xxml_malloc", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(mallocFunc, {builder_->getInt64(classSize)}, "malloc_obj");
            }

            // CRITICAL: Track this register as ptr type to avoid i64 conversion
            registerTypes_[mallocReg] = className;

            // IR: Create malloc call
            IR::Value* mallocIR = nullptr;
            if (builder_ && module_) {
                IR::Function* mallocFunc = module_->getFunction("xxml_malloc");
                if (!mallocFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                    mallocFunc = module_->createFunction(funcTy, "xxml_malloc", IR::Function::Linkage::External);
                }
                mallocIR = builder_->CreateCall(mallocFunc, {builder_->getInt64(static_cast<int64_t>(classSize))}, "malloc");
                // Insert at beginning of IR args
                irArgValues.insert(irArgValues.begin(), mallocIR);
            }

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

    // FIRST: Try to get expression type directly from semantic analyzer
    // This handles special cases like __DynamicValue from getTargetValue()
    if (semanticAnalyzer_) {
        std::string exprType = semanticAnalyzer_->getExpressionTypePublic(&node);
        if (exprType != "Unknown" && !exprType.empty()) {
            returnType = exprType;
            fromSemanticAnalyzer = true;
        }
    }

    // Try to get actual return type from semantic analyzer for instance methods
    if (semanticAnalyzer_ && isInstanceMethod && !fromSemanticAnalyzer) {
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

            // Strip ownership modifiers using TypeNormalizer
            std::string baseInstanceType = TypeNormalizer::stripOwnershipMarker(instanceType);

            // Extract method name from function name
            size_t underscorePos = functionName.rfind('_');
            if (underscorePos != std::string::npos) {
                std::string methodName = functionName.substr(underscorePos + 1);

                // Look up the method in the semantic analyzer
                auto* methodInfo = semanticAnalyzer_->findMethod(baseInstanceType, methodName);

                // If not found, check if this is a template instantiation
                // e.g., "Math::Vector2<Float>" or mangled form "Math_Vector2_Float"
                if (!methodInfo && baseInstanceType.find('<') != std::string::npos) {
                    // Template instantiation with angle brackets: Math::Vector2<Float>
                    size_t openAngle = baseInstanceType.find('<');
                    size_t closeAngle = baseInstanceType.rfind('>');

                    if (openAngle != std::string::npos && closeAngle != std::string::npos) {
                        // Extract base template name: Math::Vector2
                        std::string baseTemplate = baseInstanceType.substr(0, openAngle);

                        // Extract template argument: Float
                        std::string templateArg = baseInstanceType.substr(openAngle + 1, closeAngle - openAngle - 1);

                        // Try to look up method in the base template
                        methodInfo = semanticAnalyzer_->findMethod(baseTemplate, methodName);

                        if (methodInfo) {
                            // Substitute template parameters in return type
                            // For Vector2<T>, if return type is "T%" and T=Float, result is "Float%"
                            returnType = methodInfo->returnType;

                            // Simple template parameter substitution
                            // Find template parameter name from base template
                            const auto& templateClasses = semanticAnalyzer_->getTemplateClasses();
                            auto templateIt = templateClasses.find(baseTemplate);
                            // ✅ SAFE: Access TemplateClassInfo struct (copied data)
                            if (templateIt != templateClasses.end() && !templateIt->second.templateParams.empty()) {
                                // For now, assume single template parameter (T)
                                std::string templateParam = templateIt->second.templateParams[0].name;

                                // Replace all occurrences of T with the concrete type
                                // FIX: Pass pos to find() to continue from current position
                                size_t pos = 0;
                                while ((pos = returnType.find(templateParam, pos)) != std::string::npos) {
                                    // Only replace if it's a standalone identifier (not part of another word)
                                    bool isBoundary = (pos == 0 || !std::isalnum(returnType[pos - 1])) &&
                                                     (pos + templateParam.length() >= returnType.length() ||
                                                      !std::isalnum(returnType[pos + templateParam.length()]));
                                    if (isBoundary) {
                                        returnType.replace(pos, templateParam.length(), templateArg);
                                        pos += templateArg.length();
                                    } else {
                                        pos += templateParam.length();
                                    }
                                }
                            }

                            fromSemanticAnalyzer = true;
                        }
                    }
                }

                if (methodInfo && !fromSemanticAnalyzer) {
                    returnType = methodInfo->returnType;
                    fromSemanticAnalyzer = true;
                }
            }
        }
    }

    // Convert XXML return type to LLVM type
    std::string llvmReturnType = "ptr";  // Default to pointer
    bool isVoidReturn = false;

    // For comparison methods that need Bool wrapping, the underlying call returns i1
    if (needsBoolWrapping) {
        llvmReturnType = "i1";
    } else if (fromSemanticAnalyzer && !returnType.empty()) {
        // Use semantic analyzer's return type - this handles None -> void correctly
        llvmReturnType = getLLVMType(returnType);
        if (llvmReturnType == "void") {
            isVoidReturn = true;
        }
    } else if (isNativeFFICall && !nativeMethodIt->second.returnType.empty()) {
        // Use the native method's stored return type for FFI calls
        llvmReturnType = nativeMethodIt->second.returnType;
        returnType = nativeMethodIt->second.xxmlReturnType;  // Track XXML type too
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
            (functionName.find("_add") == functionName.length() - 4 && functionName.find("xxml_") != 0) ||  // Methods ending with _add (not xxml_ runtime functions)
            functionName.find("_run") == functionName.length() - 4 ||  // Methods ending with _run
            functionName.find("_clear") == functionName.length() - 6 || // Methods ending with _clear
            functionName.find("_reset") == functionName.length() - 6 ||   // Methods ending with _reset
            // Threading void-returning functions
            functionName == "xxml_Thread_sleep" ||
            functionName == "xxml_Thread_yield" ||
            functionName == "xxml_Mutex_destroy" ||
            functionName == "xxml_CondVar_destroy" ||
            functionName == "xxml_Atomic_destroy" ||
            functionName == "xxml_Atomic_store" ||
            functionName == "xxml_TLS_destroy" ||
            functionName == "xxml_TLS_set" ||
            // File I/O void-returning functions
            functionName == "xxml_File_close" ||
            // Memory int64 write function
            functionName == "xxml_int64_write";

        if (matchesVoidPattern) {
            llvmReturnType = "void";
            isVoidReturn = true;
        } else if (functionName == "String_length" ||  // Exact match for runtime function
            functionName == "List_size" ||  // Exact match for runtime function
            // Threading i64-returning functions
            functionName == "xxml_Thread_join" ||
            functionName == "xxml_Thread_detach" ||
            functionName == "xxml_Thread_currentId" ||
            functionName == "xxml_Mutex_lock" ||
            functionName == "xxml_Mutex_unlock" ||
            functionName == "xxml_CondVar_wait" ||
            functionName == "xxml_CondVar_waitTimeout" ||
            functionName == "xxml_CondVar_signal" ||
            functionName == "xxml_CondVar_broadcast" ||
            functionName == "xxml_Atomic_load" ||
            functionName == "xxml_Atomic_add" ||
            functionName == "xxml_Atomic_sub" ||
            functionName == "xxml_Atomic_exchange" ||
            // File I/O i64-returning functions
            functionName == "xxml_File_read" ||
            functionName == "xxml_File_write" ||
            functionName == "xxml_File_writeString" ||
            functionName == "xxml_File_writeLine" ||
            functionName == "xxml_File_seek" ||
            functionName == "xxml_File_tell" ||
            functionName == "xxml_File_size" ||
            functionName == "xxml_File_flush" ||
            functionName == "xxml_File_sizeByPath" ||
            // Memory int64 read function
            functionName == "xxml_int64_read") {
            // Only the C runtime functions return primitive types
            // User-defined XXML methods return wrapped objects
            llvmReturnType = "i64";
        } else if (functionName.find("toInt64") != std::string::npos ||
            functionName.find("toInt32") != std::string::npos) {
            llvmReturnType = "i64";
        } else if (functionName.find("reflection") != std::string::npos && (
            functionName.find("getPropertyCount") != std::string::npos ||
            functionName.find("getMethodCount") != std::string::npos ||
            functionName.find("getParameterCount") != std::string::npos ||
            functionName.find("getOwnership") != std::string::npos ||
            functionName.find("getOffset") != std::string::npos ||
            functionName.find("getInstanceSize") != std::string::npos ||
            functionName.find("isTemplate") != std::string::npos ||
            functionName.find("isStatic") != std::string::npos ||
            functionName.find("isConstructor") != std::string::npos ||
            functionName.find("getReturnOwnership") != std::string::npos ||
            functionName.find("getTemplateParamCount") != std::string::npos)) {
            // Reflection syscalls that return integers
            llvmReturnType = "i64";
        } else if (functionName.find("toBool") != std::string::npos) {
            llvmReturnType = "i1";
        } else if (functionName == "Integer_lt" || functionName == "Integer_le" ||
                   functionName == "Integer_gt" || functionName == "Integer_ge" ||
                   functionName == "Integer_eq" || functionName == "Integer_ne" ||
                   functionName == "Bool_getValue" ||
                   // __DynamicValue comparison functions
                   functionName == "__DynamicValue_greaterThan" ||
                   functionName == "__DynamicValue_lessThan" ||
                   functionName == "__DynamicValue_equals" ||
                   // Threading i1-returning functions
                   functionName == "xxml_Thread_isJoinable" ||
                   functionName == "xxml_Mutex_tryLock" ||
                   functionName == "xxml_Atomic_compareAndSwap" ||
                   // File I/O i1-returning functions
                   functionName == "xxml_File_eof" ||
                   functionName == "xxml_File_exists" ||
                   functionName == "xxml_File_delete" ||
                   functionName == "xxml_File_rename" ||
                   functionName == "xxml_File_copy" ||
                   // Directory i1-returning functions
                   functionName == "xxml_Dir_create" ||
                   functionName == "xxml_Dir_exists" ||
                   functionName == "xxml_Dir_delete" ||
                   functionName == "xxml_Dir_setCurrent" ||
                   // Path i1-returning functions
                   functionName == "xxml_Path_isAbsolute") {
            // Integer comparison and Bool_getValue return i1
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

        // FFI native method calls: use registered param types for proper type conversion
        if (isNativeFFICall && i < nativeMethodIt->second.paramTypes.size()) {
            expectedType = nativeMethodIt->second.paramTypes[i];
        }
        // Syscall functions with specific signatures
        else if (functionName.find("xxml_malloc") != std::string::npos) {
            expectedType = "i64";  // malloc takes i64 size
        } else if (functionName.find("ptr_write") != std::string::npos ||
            functionName.find("ptr_read") != std::string::npos ||
            functionName.find("free") != std::string::npos) {
            expectedType = "ptr";
        } else if (functionName.find("memcpy") != std::string::npos ||
                   functionName.find("memset") != std::string::npos) {
            expectedType = (i < 2) ? "ptr" : "i64";
        }
        // Constructors that take primitive values
        // IMPORTANT: Only match the ACTUAL primitive constructors, not template instantiations.
        // - "Integer_Constructor" or "Integer_Constructor_0" -> YES (i64)
        // - "Wrapper_Integer_Constructor" -> NO (template, takes ptr)
        // Key check: if there's a "_" before "Integer_Constructor", it's a template
        else if ((functionName == "Integer_Constructor" ||
                  functionName.find("Integer_Constructor_") == 0) &&  // Overloads like Integer_Constructor_0
                 functionName.find("_Integer_Constructor") == std::string::npos) {  // Excludes templates
            expectedType = "i64";  // Integer constructor takes i64
        }
        else if ((functionName == "Bool_Constructor" ||
                  functionName.find("Bool_Constructor_") == 0) &&
                 functionName.find("_Bool_Constructor") == std::string::npos) {
            expectedType = "i1";  // Bool constructor takes i1
        }
        else if ((functionName == "Float_Constructor" ||
                  functionName.find("Float_Constructor_") == 0) &&
                 functionName.find("_Float_Constructor") == std::string::npos) {
            expectedType = "float";  // Float constructor takes float
        }
        else if ((functionName == "Double_Constructor" ||
                  functionName.find("Double_Constructor_") == 0) &&
                 functionName.find("_Double_Constructor") == std::string::npos) {
            expectedType = "double";  // Double constructor takes double
        }
        // Threading functions with specific parameter types
        else if (functionName == "xxml_Thread_sleep" ||
                 functionName == "xxml_Atomic_create") {
            // These take a single i64 parameter
            expectedType = "i64";
        }
        else if ((functionName == "xxml_Atomic_add" || functionName == "xxml_Atomic_sub" ||
                  functionName == "xxml_Atomic_store" || functionName == "xxml_Atomic_exchange" ||
                  functionName == "xxml_int64_write") && i == 1) {
            // Second parameter is i64 (first is ptr handle)
            expectedType = "i64";
        }
        else if (functionName == "xxml_Atomic_compareAndSwap" && (i == 1 || i == 2)) {
            // Second and third parameters are i64 (expected, desired)
            expectedType = "i64";
        }
        else if (functionName == "xxml_CondVar_waitTimeout" && i == 2) {
            // Third parameter is timeout in ms (i64)
            expectedType = "i64";
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
                // Numeric literal - detect if it's float/double or integer
                // Float literals end with 'f', double literals contain 'e' or '.', integers are plain numbers
                bool hasFloatSuffix = !argValue.empty() && argValue.back() == 'f';
                bool isFloatingPoint = hasFloatSuffix ||
                                      argValue.find('e') != std::string::npos ||
                                      argValue.find('.') != std::string::npos;

                if (hasFloatSuffix) {
                    argType = "float";
                    // Remove 'f' suffix from argValue for LLVM IR
                    argValue = argValue.substr(0, argValue.length() - 1);
                } else if (isFloatingPoint) {
                    // Could be double
                    if (expectedType == "double") {
                        argType = "double";
                    } else {
                        argType = "float";  // Default to float
                    }
                } else {
                    argType = "i64";  // Integer literal
                }
            }
        } else {
            argType = "i64";
        }

        // If we have i64 but need ptr, insert inttoptr conversion
        if (argType == "i64" && expectedType == "ptr") {
            std::string convertedReg = allocateRegister();
            emitLine(format("{} = inttoptr i64 {} to ptr", convertedReg, argValue));

            // IR: Create inttoptr instruction
            if (builder_) {
                lastExprValue_ = builder_->CreateIntToPtr(builder_->getInt64(0), builder_->getPtrTy(), "int_to_ptr");
            }
            argValue = convertedReg;
            argType = "ptr";

            // IR: inttoptr conversion for argument
            if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                irArgValues.back() = builder_->CreateIntToPtr(irArgValues.back(), builder_->getPtrTy(), "arg_itp");
            }
        } else if (argType == "ptr" && expectedType == "i64") {
            // CRITICAL FIX: When converting pointer to i64 for a parameter that expects a value (reference %),
            // we need to LOAD the value from the pointer first, not convert the pointer address itself
            // Check if this pointer points to an i64 (NativeType<"int64">^) that needs to be dereferenced
            auto typeIt = registerTypes_.find(argValue);
            bool isInt64Pointer = false;
            bool isIntegerObject = false;
            if (typeIt != registerTypes_.end()) {
                std::string xxmlType = typeIt->second;
                // Check if this is NativeType<"int64">^ or NativeType<int64>^
                if (xxmlType == "NativeType<\"int64\">" || xxmlType == "NativeType<int64>") {
                    isInt64Pointer = true;
                }
                // Check if this is an Integer^ object that needs value extraction
                else if (xxmlType == "Integer" || xxmlType == "Integer^") {
                    isIntegerObject = true;
                }
            }

            if (isInt64Pointer) {
                // Load the i64 value from the pointer
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load i64, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "i64";

                // IR: Load i64 from pointer
                if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                    irArgValues.back() = builder_->CreateLoad(builder_->getInt64Ty(), irArgValues.back(), "arg_i64_load");
                }
            } else if (isIntegerObject) {
                // Extract i64 value from Integer^ object using Integer_toInt64
                std::string extractedReg = allocateRegister();
                emitLine(format("{} = call i64 @Integer_toInt64(ptr {})", extractedReg, argValue));
                argValue = extractedReg;
                argType = "i64";

                // IR: Call Integer_toInt64
                if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                    // Note: IR infrastructure would need Integer_toInt64 declared
                }
            } else {
                // Convert pointer address to i64 (for non-int64 pointers)
                std::string convertedReg = allocateRegister();
                emitLine(format("{} = ptrtoint ptr {} to i64", convertedReg, argValue));

                // IR: Create ptrtoint instruction
                if (builder_) {
                    lastExprValue_ = builder_->CreatePtrToInt(builder_->getNullPtr(), builder_->getInt64Ty(), "ptr_to_int");
                }
                argValue = convertedReg;
                argType = "i64";

                // IR: ptrtoint conversion
                if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                    irArgValues.back() = builder_->CreatePtrToInt(irArgValues.back(), builder_->getInt64Ty(), "arg_pti");
                }
            }
        } else if (argType == "ptr" && (expectedType == "i32" || expectedType == "i16" || expectedType == "i8")) {
            // Handle Integer^ to smaller integer types (i32, i16, i8) with truncation
            auto typeIt = registerTypes_.find(argValue);
            bool isIntegerObject = false;
            bool isNativeIntPtr = false;
            if (typeIt != registerTypes_.end()) {
                std::string xxmlType = typeIt->second;
                // Check if this is an Integer^ object that needs value extraction
                if (xxmlType == "Integer" || xxmlType == "Integer^") {
                    isIntegerObject = true;
                }
                // Check if this is NativeType<"intXX">^ that needs loading
                else if (xxmlType.find("NativeType<") == 0 && xxmlType.find("int") != std::string::npos) {
                    isNativeIntPtr = true;
                }
            }

            if (isIntegerObject) {
                // Extract i64 value from Integer^ object, then truncate to expected size
                std::string extractedReg = allocateRegister();
                emitLine(format("{} = call i64 @Integer_toInt64(ptr {})", extractedReg, argValue));
                std::string truncReg = allocateRegister();
                emitLine(format("{} = trunc i64 {} to {}", truncReg, extractedReg, expectedType));
                argValue = truncReg;
                argType = expectedType;
            } else if (isNativeIntPtr) {
                // Load the value from the pointer, then truncate if needed
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load i64, ptr {}", loadedReg, argValue));
                std::string truncReg = allocateRegister();
                emitLine(format("{} = trunc i64 {} to {}", truncReg, loadedReg, expectedType));
                argValue = truncReg;
                argType = expectedType;
            } else {
                // Convert pointer address to integer and truncate
                std::string convertedReg = allocateRegister();
                emitLine(format("{} = ptrtoint ptr {} to i64", convertedReg, argValue));
                std::string truncReg = allocateRegister();
                emitLine(format("{} = trunc i64 {} to {}", truncReg, convertedReg, expectedType));
                argValue = truncReg;
                argType = expectedType;
            }
        } else if (argType == "ptr" && expectedType == "float") {
            // Handle Float^ or NativeType<"float">^ to float conversion
            // ONLY convert if we have type info and it's actually a pointer type
            auto typeIt = registerTypes_.find(argValue);
            bool needsConversion = false;
            bool isFloatObject = false;
            bool isNativeFloatPtr = false;
            if (typeIt != registerTypes_.end()) {
                std::string xxmlType = typeIt->second;
                // Check if this is a Float^ object that needs value extraction
                if (xxmlType == "Float" || xxmlType == "Float^") {
                    isFloatObject = true;
                    needsConversion = true;
                }
                // Check if this is NativeType<"float">^ that needs loading
                else if (xxmlType.find("NativeType<") == 0 && xxmlType.find("float") != std::string::npos) {
                    isNativeFloatPtr = true;
                    needsConversion = true;
                }
                // Check if it's an actual pointer type that needs loading
                else if (xxmlType.find("ptr") != std::string::npos ||
                         xxmlType.find("Ptr") != std::string::npos) {
                    needsConversion = true;
                }
            }
            // Don't convert if the value doesn't look like it needs conversion
            // (no type info and no conversion needed)

            if (isFloatObject) {
                // Extract float value from Float^ object
                std::string extractedReg = allocateRegister();
                emitLine(format("{} = call float @Float_toFloat(ptr {})", extractedReg, argValue));
                argValue = extractedReg;
                argType = "float";
            } else if (isNativeFloatPtr) {
                // Load the float value from the pointer
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load float, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "float";
            } else if (needsConversion) {
                // Only load if we determined conversion is needed
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load float, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "float";
            }
            // If no conversion needed, use the value as-is (it might already be float)
        } else if (argType == "ptr" && expectedType == "double") {
            // Handle Double^ or NativeType<"double">^ to double conversion
            // ONLY convert if we have type info and it's actually a pointer type
            auto typeIt = registerTypes_.find(argValue);
            bool needsConversion = false;
            bool isDoubleObject = false;
            bool isNativeDoublePtr = false;
            if (typeIt != registerTypes_.end()) {
                std::string xxmlType = typeIt->second;
                // Check if this is a Double^ object that needs value extraction
                if (xxmlType == "Double" || xxmlType == "Double^") {
                    isDoubleObject = true;
                    needsConversion = true;
                }
                // Check if this is NativeType<"double">^ that needs loading
                else if (xxmlType.find("NativeType<") == 0 && xxmlType.find("double") != std::string::npos) {
                    isNativeDoublePtr = true;
                    needsConversion = true;
                }
                // Check if it's an actual pointer type that needs loading
                else if (xxmlType.find("ptr") != std::string::npos ||
                         xxmlType.find("Ptr") != std::string::npos) {
                    needsConversion = true;
                }
            }
            // Don't convert if the value doesn't look like it needs conversion

            if (isDoubleObject) {
                // Extract double value from Double^ object
                std::string extractedReg = allocateRegister();
                emitLine(format("{} = call double @Double_toDouble(ptr {})", extractedReg, argValue));
                argValue = extractedReg;
                argType = "double";
            } else if (isNativeDoublePtr) {
                // Load the double value from the pointer
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load double, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "double";
            } else if (needsConversion) {
                // Only load if we determined conversion is needed
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load double, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "double";
            }
            // If no conversion needed, use the value as-is (it might already be double)
        } else if (argType == "i64" && expectedType == "float") {
            // Convert integer to float using sitofp (signed integer to floating point)
            std::string convertedReg = allocateRegister();
            emitLine(format("{} = sitofp i64 {} to float", convertedReg, argValue));

            // IR: Create sitofp to float
            if (builder_) {
                lastExprValue_ = builder_->CreateSIToFP(builder_->getInt64(0), builder_->getFloatTy(), "si_to_float");
            }
            argValue = convertedReg;
            argType = "float";

            // IR: sitofp i64 to float
            if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                irArgValues.back() = builder_->CreateSIToFP(irArgValues.back(), builder_->getFloatTy(), "arg_stf");
            }
        } else if (argType == "i64" && expectedType == "double") {
            // Convert integer to double using sitofp
            std::string convertedReg = allocateRegister();
            emitLine(format("{} = sitofp i64 {} to double", convertedReg, argValue));

            // IR: Create sitofp to double
            if (builder_) {
                lastExprValue_ = builder_->CreateSIToFP(builder_->getInt64(0), builder_->getDoubleTy(), "si_to_double");
            }
            argValue = convertedReg;
            argType = "double";

            // IR: sitofp i64 to double
            if (builder_ && !irArgValues.empty() && irArgValues.back()) {
                irArgValues.back() = builder_->CreateSIToFP(irArgValues.back(), builder_->getDoubleTy(), "arg_std");
            }
        }

        callInstr << argType << " " << argValue;
    }

    callInstr << ")";
    emitLine(callInstr.str());

    // If we called a comparison function that returns i1 but needs Bool^ wrapping,
    // allocate a Bool object and set its value
    if (needsBoolWrapping) {
        // First allocate memory for Bool object
        std::string boolMemReg = allocateRegister();
        emitLine(format("{} = call ptr @xxml_malloc(i64 8)", boolMemReg));

        // Store the i1 result as a byte in the Bool object's value field
        std::string boolValReg = allocateRegister();
        emitLine(format("{} = zext i1 {} to i8", boolValReg, resultReg));
        emitLine(format("store i8 {}, ptr {}", boolValReg, boolMemReg));

        // IR: Create zext instruction
        if (builder_ && lastExprValue_) {
            IR::Value* zextVal = builder_->CreateZExt(lastExprValue_, builder_->getInt8Ty(), "zext_bool");
            (void)zextVal;
        }

        // IR: malloc + zext + store for Bool wrapping
        if (builder_ && module_ && lastExprValue_) {
            IR::Function* mallocFunc = module_->getFunction("xxml_malloc");
            if (!mallocFunc) {
                auto& ctx = module_->getContext();
                auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getInt64Ty()}, false);
                mallocFunc = module_->createFunction(funcTy, "xxml_malloc", IR::Function::Linkage::External);
            }
            IR::Value* boolMem = builder_->CreateCall(mallocFunc, {builder_->getInt64(8)}, "bool_mem");
            IR::Value* zextVal = builder_->CreateZExt(lastExprValue_, builder_->getInt8Ty(), "bool_zext");
            builder_->CreateStore(zextVal, boolMem);
            lastExprValue_ = boolMem;
        }

        // The Bool^ is now our result
        resultReg = boolMemReg;
        llvmReturnType = "ptr";
    }

    // For void functions, set last_expr to empty/null
    if (isVoidReturn) {
        valueMap_["__last_expr"] = "";
    } else {
        valueMap_["__last_expr"] = resultReg;
    }

    // Track the return type for the result register
    // We already queried semantic analyzer earlier and have returnType if available
    if (!isVoidReturn && !resultReg.empty()) {
        // If we wrapped the result in a Bool object, set the return type to Bool^
        if (needsBoolWrapping) {
            returnType = "Bool^";
        }
        // If we didn't get returnType from semantic analyzer, infer it from function name
        else if (!fromSemanticAnalyzer || returnType.empty()) {
            size_t underscorePos = functionName.rfind('_');
            if (underscorePos != std::string::npos) {
                std::string className = functionName.substr(0, underscorePos);
                std::string methodName = functionName.substr(underscorePos + 1);

                // Special handling for Bool::getValue which actually returns i1 (native bool)
                // Other comparison methods (eq, ne, lt, etc.) return Bool^ objects, not native i1
                if (methodName == "getValue" && className == "Bool") {
                    returnType = "NativeType<\"bool\">";
                }
                // Comparison methods return Bool^ objects
                else if (methodName == "eq" || methodName == "ne" || methodName == "lt" ||
                    methodName == "le" || methodName == "gt" || methodName == "ge" ||
                    methodName == "equals" || methodName == "lessThan" ||
                    methodName == "lessThanOrEqual" || methodName == "greaterThan" ||
                    methodName == "greaterThanOrEqual" || methodName == "notEquals" ||
                    methodName == "isEmpty") {
                    returnType = "Bool^";
                }
                // Special handling for collection methods that return element types
                else if (isInstanceMethod && (methodName == "get" || methodName == "pop")) {
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
                }
                // Special handling for Processor API methods
                // Some methods return known types, others return polymorphic types
                else if (className == "Processor") {
                    if (methodName == "getTargetName" || methodName == "getTypeName" ||
                        methodName == "getAnnotationName") {
                        returnType = "String^";
                    } else if (methodName == "getLineNumber" || methodName == "getArgCount" ||
                               methodName == "getAnnotationArgCount") {
                        returnType = "Integer^";
                    } else if (methodName == "argAsDouble" || methodName == "getAnnotationDoubleArg") {
                        returnType = "Double^";
                    } else if (methodName == "hasAnnotation" || methodName == "isProperty" ||
                               methodName == "isMethod" || methodName == "isClass") {
                        returnType = "Bool^";
                    } else if (methodName == "getTargetValue" || methodName == "getAnnotationArg") {
                        // These methods return polymorphic types - use __DynamicValue
                        // which has runtime type dispatch methods (greaterThan, toString, etc.)
                        returnType = "__DynamicValue";
                    }
                    // For other polymorphic methods, fall through to use className as default
                    else {
                        returnType = className;  // Default to Processor for unknown methods
                    }
                } else {
                    returnType = className;  // Default fallback
                }
            }
        }

        // Store return type with ALL ownership modifiers preserved (%, ^, &)
        // These modifiers have semantic meaning and must persist in the type system
        if (!returnType.empty()) {
            registerTypes_[resultReg] = returnType;
        }
    }

    // === IR Infrastructure: Track call result ===
    // For the new IR path, we need to set lastExprValue_ so that parent expressions
    // can use the typed IR value for type checking
    if (builder_ && module_) {
        // Determine IR return type based on XXML return type
        IR::Type* irRetType = nullptr;
        if (isVoidReturn) {
            irRetType = builder_->getVoidTy();
            lastExprValue_ = nullptr;
        } else {
            // Determine IR type from return type
            // NOTE: Integer^, Float^, Bool^, Double^ are OBJECT types (ptr)
            // Only raw native types map to primitives
            if (returnType == "NativeType<\"bool\">" || returnType == "NativeType<bool>") {
                irRetType = builder_->getInt1Ty();
            } else if (returnType == "i64") {
                irRetType = builder_->getInt64Ty();
            } else if (returnType == "f32") {
                irRetType = builder_->getFloatTy();
            } else if (returnType == "f64") {
                irRetType = builder_->getDoubleTy();
            } else {
                // Default to pointer type for objects (Integer^, Float^, Bool^, class types, etc.)
                irRetType = builder_->getPtrTy();
            }

            // Get or create function declaration
            IR::Function* irFunc = module_->getFunction(functionName);
            if (!irFunc) {
                // Create function type with ptr params (simplified)
                std::vector<IR::Type*> paramTypes(argValues.size(), builder_->getPtrTy());
                auto* funcTy = module_->getContext().getFunctionTy(irRetType, paramTypes, false);
                irFunc = module_->createFunction(funcTy, functionName, IR::Function::Linkage::External);
            }

            // Create call instruction
            if (irFunc) {
                std::vector<IR::Value*> irArgs;
                // Use captured IR values if available, otherwise use null pointers as fallback
                for (size_t i = 0; i < argValues.size(); ++i) {
                    if (i < irArgValues.size() && irArgValues[i]) {
                        irArgs.push_back(irArgValues[i]);
                    } else {
                        irArgs.push_back(builder_->getNullPtr());
                    }
                }
                lastExprValue_ = builder_->CreateCall(irFunc, irArgs, isVoidReturn ? "" : "call_result");
            }
        }
    }
}

void LLVMBackend::visit(Parser::BinaryExpr& node) {
    // NEW: Try compile-time evaluation first (constant folding)
    // If all operands are compile-time constants, evaluate the entire expression at compile-time
    if (semanticAnalyzer_) {
        Semantic::CompiletimeInterpreter interpreter(*semanticAnalyzer_);
        // Propagate known compile-time values to the interpreter
        for (const auto& [name, ctValue] : compiletimeValues_) {
            interpreter.setVariable(name, ctValue->clone());
        }

        auto ctResult = interpreter.evaluate(&node);
        if (ctResult) {
            // Entire expression folded at compile-time!
            if (ctResult->isInteger()) {
                auto* intVal = static_cast<Semantic::CompiletimeInteger*>(ctResult.get());
                emitLine(format("; Compile-time folded binary expression to i64 {}", intVal->value));
                valueMap_["__last_expr"] = std::to_string(intVal->value);
                if (builder_) {
                    lastExprValue_ = builder_->getInt64(intVal->value);
                }
                return;  // NO RUNTIME OPERATION!
            }
            if (ctResult->isBool()) {
                auto* boolVal = static_cast<Semantic::CompiletimeBool*>(ctResult.get());
                emitLine(format("; Compile-time folded binary expression to i1 {}", boolVal->value ? "true" : "false"));
                valueMap_["__last_expr"] = boolVal->value ? "true" : "false";
                if (builder_) {
                    lastExprValue_ = builder_->getInt1(boolVal->value);
                }
                return;
            }
            if (ctResult->isFloat()) {
                auto* floatVal = static_cast<Semantic::CompiletimeFloat*>(ctResult.get());
                emitLine(format("; Compile-time folded binary expression to float"));
                std::ostringstream ss;
                ss << std::hexfloat << floatVal->value;
                valueMap_["__last_expr"] = ss.str();
                if (builder_) {
                    lastExprValue_ = builder_->getFloat(floatVal->value);
                }
                return;
            }
            if (ctResult->isDouble()) {
                auto* doubleVal = static_cast<Semantic::CompiletimeDouble*>(ctResult.get());
                emitLine(format("; Compile-time folded binary expression to double"));
                std::ostringstream ss;
                ss << std::hexfloat << doubleVal->value;
                valueMap_["__last_expr"] = ss.str();
                if (builder_) {
                    lastExprValue_ = builder_->getDouble(doubleVal->value);
                }
                return;
            }
            // For string and other types, fall through to runtime evaluation
        }
    }

    // Set operand context for raw value optimization
    ValueContext saved = currentValueContext_;
    currentValueContext_ = ValueContext::OperandContext;

    // Generate code for left operand
    node.left->accept(*this);
    std::string leftValue = valueMap_["__last_expr"];

    // Generate code for right operand
    node.right->accept(*this);
    std::string rightValue = valueMap_["__last_expr"];

    // Restore the value context after operand evaluation
    currentValueContext_ = saved;

    // Determine LLVM types of operands
    std::string leftType = "i64";  // Default
    std::string rightType = "i64"; // Default
    bool leftTypeKnown = false;    // Track if we found a real type
    bool rightTypeKnown = false;

    // Check if operands are variables or have known types
    for (const auto& var : variables_) {
        if (var.second.llvmRegister == leftValue) {
            // Get the XXML type and convert to LLVM
            auto typeIt = registerTypes_.find(leftValue);
            if (typeIt != registerTypes_.end()) {
                leftType = getLLVMType(typeIt->second);
                leftTypeKnown = true;
            }
            break;
        }
    }
    for (const auto& var : variables_) {
        if (var.second.llvmRegister == rightValue) {
            auto typeIt = registerTypes_.find(rightValue);
            if (typeIt != registerTypes_.end()) {
                rightType = getLLVMType(typeIt->second);
                rightTypeKnown = true;
            }
            break;
        }
    }

    // Also check registerTypes_ directly
    auto leftRegType = registerTypes_.find(leftValue);
    if (leftRegType != registerTypes_.end()) {
        leftType = getLLVMType(leftRegType->second);
        leftTypeKnown = true;
        DEBUG_OUT("DEBUG BinaryExpr: leftValue=" << leftValue << " has type=" << leftRegType->second << " -> LLVM type=" << leftType << "\n");
    } else {
        DEBUG_OUT("DEBUG BinaryExpr: leftValue=" << leftValue << " NOT FOUND in registerTypes_\n");
    }
    auto rightRegType = registerTypes_.find(rightValue);
    if (rightRegType != registerTypes_.end()) {
        rightType = getLLVMType(rightRegType->second);
        rightTypeKnown = true;
        DEBUG_OUT("DEBUG BinaryExpr: rightValue=" << rightValue << " has type=" << rightRegType->second << " -> LLVM type=" << rightType << "\n");
    } else {
        DEBUG_OUT("DEBUG BinaryExpr: rightValue=" << rightValue << " NOT FOUND in registerTypes_\n");
    }

    // Check if this is String concatenation (both operands are String types)
    auto leftXXMLTypeIt = registerTypes_.find(leftValue);
    auto rightXXMLTypeIt = registerTypes_.find(rightValue);
    bool leftIsString = (leftXXMLTypeIt != registerTypes_.end() &&
                        (leftXXMLTypeIt->second == "String" || leftXXMLTypeIt->second == "String^" ||
                         leftXXMLTypeIt->second.find("String") != std::string::npos));
    bool rightIsString = (rightXXMLTypeIt != registerTypes_.end() &&
                         (rightXXMLTypeIt->second == "String" || rightXXMLTypeIt->second == "String^" ||
                          rightXXMLTypeIt->second.find("String") != std::string::npos));

    // Handle String concatenation with + operator
    if (node.op == "+" && leftType == "ptr" && rightType == "ptr" && (leftIsString || rightIsString)) {
        // Both operands are pointers and at least one is a String - use string_concat
        std::string resultReg = allocateRegister();
        emitLine(format("{} = call ptr @xxml_string_concat(ptr {}, ptr {})", resultReg, leftValue, rightValue));
        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "String^";

        // IR: Create call to xxml_string_concat
        if (builder_ && module_) {
            // IMPORTANT: Save valueMap_ state since re-visiting operands overwrites __last_expr
            std::string savedLastExpr = valueMap_["__last_expr"];
            // Re-visit operands to get IR values
            node.left->accept(*this);
            IR::Value* leftIR = lastExprValue_;
            node.right->accept(*this);
            IR::Value* rightIR = lastExprValue_;
            // Restore __last_expr after IR generation
            valueMap_["__last_expr"] = savedLastExpr;

            if (leftIR && rightIR) {
                IR::Function* concatFunc = module_->getFunction("xxml_string_concat");
                if (!concatFunc) {
                    auto& ctx = module_->getContext();
                    auto* funcTy = ctx.getFunctionTy(builder_->getPtrTy(), {builder_->getPtrTy(), builder_->getPtrTy()}, false);
                    concatFunc = module_->createFunction(funcTy, "xxml_string_concat", IR::Function::Linkage::External);
                }
                lastExprValue_ = builder_->CreateCall(concatFunc, {leftIR, rightIR}, "str_concat");
            }
        }
        return;
    }

    // Handle pointer arithmetic: ptr + i64 or i64 + ptr
    if ((leftType == "ptr" || rightType == "ptr") && (node.op == "+" || node.op == "-")) {
        std::string ptrValue;
        std::string offsetValue;
        std::string offsetType;
        bool ptrOnLeft = (leftType == "ptr");

        if (ptrOnLeft) {
            ptrValue = leftValue;
            offsetValue = rightValue;
            offsetType = rightType;
        } else {
            ptrValue = rightValue;
            offsetValue = leftValue;
            offsetType = leftType;
        }

        // If the offset is also a ptr (but not String), convert it to i64
        IR::Value* offsetIRValue = nullptr;
        if (offsetType == "ptr") {
            std::string offsetAsInt = allocateRegister();
            emitLine(format("{} = ptrtoint ptr {} to i64", offsetAsInt, offsetValue));

            // IR: ptrtoint for offset
            if (builder_ && lastExprValue_) {
                offsetIRValue = builder_->CreatePtrToInt(lastExprValue_, builder_->getInt64Ty(), "off_ptrtoint");
            }

            offsetValue = offsetAsInt;
        }

        // Convert ptr to i64
        std::string ptrAsInt = allocateRegister();
        emitLine(format("{} = ptrtoint ptr {} to i64", ptrAsInt, ptrValue));

        // IR: ptrtoint for pointer value
        IR::Value* ptrIntIR = nullptr;
        if (builder_ && lastExprValue_) {
            ptrIntIR = builder_->CreatePtrToInt(lastExprValue_, builder_->getInt64Ty(), "ptr_ptrtoint");
        }

        // Do the arithmetic
        std::string resultInt = allocateRegister();
        IR::Value* arithResultIR = nullptr;
        if (node.op == "+") {
            if (ptrOnLeft) {
                emitLine(format("{} = add i64 {}, {}", resultInt, ptrAsInt, offsetValue));
                // IR: add i64 (ptr + offset)
                if (builder_ && ptrIntIR) {
                    arithResultIR = builder_->CreateAdd(ptrIntIR, offsetIRValue ? offsetIRValue : builder_->getInt64(0), "add_result");
                }
            } else {
                emitLine(format("{} = add i64 {}, {}", resultInt, offsetValue, ptrAsInt));
                // IR: add i64 (offset + ptr)
                if (builder_ && ptrIntIR) {
                    arithResultIR = builder_->CreateAdd(offsetIRValue ? offsetIRValue : builder_->getInt64(0), ptrIntIR, "add_result");
                }
            }
        } else { // "-"
            if (ptrOnLeft) {
                emitLine(format("{} = sub i64 {}, {}", resultInt, ptrAsInt, offsetValue));
                // IR: sub i64 (ptr - offset)
                if (builder_ && ptrIntIR) {
                    arithResultIR = builder_->CreateSub(ptrIntIR, offsetIRValue ? offsetIRValue : builder_->getInt64(0), "sub_result");
                }
            } else {
                // offset - ptr doesn't make sense, but generate it anyway
                emitLine(format("{} = sub i64 {}, {}", resultInt, offsetValue, ptrAsInt));
                // IR: sub i64 (offset - ptr)
                if (builder_ && ptrIntIR) {
                    arithResultIR = builder_->CreateSub(offsetIRValue ? offsetIRValue : builder_->getInt64(0), ptrIntIR, "sub_result");
                }
            }
        }

        // Convert back to ptr
        std::string resultReg = allocateRegister();
        emitLine(format("{} = inttoptr i64 {} to ptr", resultReg, resultInt));

        // IR: inttoptr for result
        if (builder_ && arithResultIR) {
            lastExprValue_ = builder_->CreateIntToPtr(arithResultIR, builder_->getPtrTy(), "inttoptr_result");
        }

        // Result is a ptr
        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "NativeType<\"ptr\">";

        // IR: Pointer arithmetic using ptrtoint/inttoptr
        if (builder_) {
            // IMPORTANT: Save valueMap_ state since re-visiting operands overwrites __last_expr
            std::string savedLastExpr = valueMap_["__last_expr"];
            node.left->accept(*this);
            IR::Value* leftIR = lastExprValue_;
            node.right->accept(*this);
            IR::Value* rightIR = lastExprValue_;
            // Restore __last_expr after IR generation
            valueMap_["__last_expr"] = savedLastExpr;

            if (leftIR && rightIR) {
                IR::Value* ptrIR = ptrOnLeft ? leftIR : rightIR;
                IR::Value* offsetIR = ptrOnLeft ? rightIR : leftIR;

                // Convert ptr to i64
                IR::Value* ptrInt = builder_->CreatePtrToInt(ptrIR, builder_->getInt64Ty(), "ptr_int");
                // Convert offset if it's a ptr
                if (offsetIR->getType()->isPointer()) {
                    offsetIR = builder_->CreatePtrToInt(offsetIR, builder_->getInt64Ty(), "off_int");
                }

                IR::Value* resultInt;
                if (node.op == "+") {
                    resultInt = builder_->CreateAdd(ptrInt, offsetIR, "ptr_add");
                } else {
                    resultInt = builder_->CreateSub(ptrInt, offsetIR, "ptr_sub");
                }
                lastExprValue_ = builder_->CreateIntToPtr(resultInt, builder_->getPtrTy(), "ptr_result");
            }
        }
    } else if ((leftType == "ptr" || rightType == "ptr") &&
               (node.op == "==" || node.op == "!=" || node.op == "<" || node.op == ">" || node.op == "<=" || node.op == ">=")) {
        // Pointer comparison - convert pointers to i64 first
        std::string leftCmp = leftValue;
        std::string rightCmp = rightValue;

        if (leftType == "ptr") {
            leftCmp = allocateRegister();
            emitLine(format("{} = ptrtoint ptr {} to i64", leftCmp, leftValue));
        }
        if (rightType == "ptr") {
            rightCmp = allocateRegister();
            emitLine(format("{} = ptrtoint ptr {} to i64", rightCmp, rightValue));
        }

        std::string resultReg = allocateRegister();
        std::string opCode = generateBinaryOp(node.op, leftCmp, rightCmp, "i64");
        emitLine(format("{} = {}", resultReg, opCode));
        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "NativeType<\"bool\">";  // Comparison result is i1

        // IR: Pointer comparison using ptrtoint
        if (builder_) {
            // IMPORTANT: Save valueMap_ state since re-visiting operands overwrites __last_expr
            std::string savedLastExpr = valueMap_["__last_expr"];
            node.left->accept(*this);
            IR::Value* leftIR = lastExprValue_;
            node.right->accept(*this);
            IR::Value* rightIR = lastExprValue_;
            // Restore __last_expr after IR generation
            valueMap_["__last_expr"] = savedLastExpr;

            if (leftIR && rightIR) {
                // Convert pointers to integers for comparison
                if (leftIR->getType()->isPointer()) {
                    leftIR = builder_->CreatePtrToInt(leftIR, builder_->getInt64Ty(), "lcmp");
                }
                if (rightIR->getType()->isPointer()) {
                    rightIR = builder_->CreatePtrToInt(rightIR, builder_->getInt64Ty(), "rcmp");
                }

                if (node.op == "==") {
                    lastExprValue_ = builder_->CreateICmpEQ(leftIR, rightIR, "ptr_eq");
                } else if (node.op == "!=") {
                    lastExprValue_ = builder_->CreateICmpNE(leftIR, rightIR, "ptr_ne");
                } else if (node.op == "<") {
                    lastExprValue_ = builder_->CreateICmpSLT(leftIR, rightIR, "ptr_lt");
                } else if (node.op == "<=") {
                    lastExprValue_ = builder_->CreateICmpSLE(leftIR, rightIR, "ptr_le");
                } else if (node.op == ">") {
                    lastExprValue_ = builder_->CreateICmpSGT(leftIR, rightIR, "ptr_gt");
                } else if (node.op == ">=") {
                    lastExprValue_ = builder_->CreateICmpSGE(leftIR, rightIR, "ptr_ge");
                }
            }
        }
    } else if (node.op == "||" || node.op == "&&") {
        // Logical operators - need to convert Bool objects to i1 if necessary
        // For logical operations, both operands MUST be i1. Any operand that is NOT
        // already a native bool (i1) needs to be converted via Bool_getValue.
        std::string leftBool = leftValue;
        std::string rightBool = rightValue;

        // Get XXML types to check if we have Bool objects
        auto leftXXMLType = registerTypes_.find(leftValue);
        auto rightXXMLType = registerTypes_.find(rightValue);

        // Check if left operand is definitely i1 (native bool)
        bool leftIsNativeI1 = (leftType == "i1") ||
            (leftXXMLType != registerTypes_.end() &&
             (leftXXMLType->second == "NativeType<\"bool\">" ||
              leftXXMLType->second == "NativeType<bool>"));

        // Check if right operand is definitely i1 (native bool)
        bool rightIsNativeI1 = (rightType == "i1") ||
            (rightXXMLType != registerTypes_.end() &&
             (rightXXMLType->second == "NativeType<\"bool\">" ||
              rightXXMLType->second == "NativeType<bool>"));

        // Convert left operand to i1 if it's NOT already native i1
        // This includes Bool objects (ptr), unknown types that default to i64, etc.
        if (!leftIsNativeI1) {
            leftBool = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", leftBool, leftValue));
        }

        // Convert right operand to i1 if it's NOT already native i1
        if (!rightIsNativeI1) {
            rightBool = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", rightBool, rightValue));
        }

        // IR: These Bool_getValue calls are handled in the IR section below

        // Perform logical operation
        std::string resultReg = allocateRegister();
        if (node.op == "||") {
            emitLine(format("{} = or i1 {}, {}", resultReg, leftBool, rightBool));

            // IR: Create or instruction
            if (builder_) {
                lastExprValue_ = builder_->CreateOr(builder_->getInt1(false), builder_->getInt1(false), "or_i1");
            }
        } else {
            emitLine(format("{} = and i1 {}, {}", resultReg, leftBool, rightBool));

            // IR: Create and instruction
            if (builder_) {
                lastExprValue_ = builder_->CreateAnd(builder_->getInt1(true), builder_->getInt1(true), "and_i1");
            }
        }

        // IR: Perform logical operation directly
        // (The main IR generation is in the block below, but we can also set up here)

        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "NativeType<\"bool\">";

        // IR Infrastructure: Set lastExprValue_ with correct i1 type
        // The result of logical operations is always i1
        if (builder_) {
            // For logical operators, we need both operands as i1
            // Re-get the IR values and convert if needed
            IR::Value* leftIR = nullptr;
            IR::Value* rightIR = nullptr;

            // Visit operands to get IR values
            // IMPORTANT: Save valueMap_ state since re-visiting overwrites __last_expr
            std::string savedLastExpr = valueMap_["__last_expr"];
            node.left->accept(*this);
            leftIR = lastExprValue_;
            node.right->accept(*this);
            rightIR = lastExprValue_;
            // Restore __last_expr after IR generation
            valueMap_["__last_expr"] = savedLastExpr;

            if (leftIR && rightIR) {
                // Convert pointer types to i1 via Bool_getValue
                auto ensureI1 = [this](IR::Value* val) -> IR::Value* {
                    if (val->getType()->isPointer()) {
                        IR::Function* boolGetValueFunc = module_->getFunction("Bool_getValue");
                        if (!boolGetValueFunc) {
                            auto& ctx = module_->getContext();
                            auto* funcTy = ctx.getFunctionTy(builder_->getInt1Ty(), {builder_->getPtrTy()}, false);
                            boolGetValueFunc = module_->createFunction(funcTy, "Bool_getValue", IR::Function::Linkage::External);
                        }
                        return builder_->CreateCall(boolGetValueFunc, {val}, "bool_val");
                    }
                    return val;
                };

                leftIR = ensureI1(leftIR);
                rightIR = ensureI1(rightIR);

                if (node.op == "||") {
                    lastExprValue_ = builder_->CreateOr(leftIR, rightIR, "or_result");
                } else {
                    lastExprValue_ = builder_->CreateAnd(leftIR, rightIR, "and_result");
                }
            }
        }
        return;
    } else {
        // Normal arithmetic or comparison - use the determined types
        // Determine the operation type (prefer floating-point if either operand is float/double)
        std::string opType = "i64";
        DEBUG_OUT("DEBUG BinaryExpr: leftType='" << leftType << "' rightType='" << rightType << "'\n");

        // Helper to check if a value is a literal (not a register or global)
        auto isLiteral = [](const std::string& val) {
            return !val.empty() && val[0] != '%' && val[0] != '@';
        };
        bool leftIsLiteral = isLiteral(leftValue);
        bool rightIsLiteral = isLiteral(rightValue);

        // Helper to get integer rank (higher = wider)
        auto intRank = [](const std::string& type) -> int {
            if (type == "i1") return 1;
            if (type == "i8") return 8;
            if (type == "i16") return 16;
            if (type == "i32") return 32;
            if (type == "i64") return 64;
            return 0;  // Not an integer type
        };

        if (leftType == "double" || rightType == "double") {
            opType = "double";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to double\n");
        } else if (leftType == "float" || rightType == "float") {
            opType = "float";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to float\n");
        } else {
            // Integer type handling - use the wider of the two KNOWN types
            // Only consider a type's rank if we actually found it in registerTypes_
            int leftRank = leftTypeKnown ? intRank(leftType) : 0;
            int rightRank = rightTypeKnown ? intRank(rightType) : 0;

            if (leftRank > 0 && rightRank > 0) {
                // Both are known integer types - use the wider one
                opType = (leftRank >= rightRank) ? leftType : rightType;
                DEBUG_OUT("DEBUG BinaryExpr: Both known integers, using wider type " << opType << "\n");
            } else if (leftRank > 0 && (rightIsLiteral || !rightTypeKnown)) {
                // Left is known integer type, right is literal or unknown - use left type
                opType = leftType;
                DEBUG_OUT("DEBUG BinaryExpr: Left " << leftType << " + literal/unknown, using " << opType << "\n");
            } else if (rightRank > 0 && (leftIsLiteral || !leftTypeKnown)) {
                // Right is known integer type, left is literal or unknown - use right type
                opType = rightType;
                DEBUG_OUT("DEBUG BinaryExpr: Literal/unknown + right " << rightType << ", using " << opType << "\n");
            } else if (leftRank > 0) {
                // Only left is known integer
                opType = leftType;
                DEBUG_OUT("DEBUG BinaryExpr: Using left type " << opType << "\n");
            } else if (rightRank > 0) {
                // Only right is known integer
                opType = rightType;
                DEBUG_OUT("DEBUG BinaryExpr: Using right type " << opType << "\n");
            }
            // else default to i64
        }
        DEBUG_OUT("DEBUG BinaryExpr: Final opType='" << opType << "' for op='" << node.op << "'\n");

        std::string resultReg = allocateRegister();
        std::string opCode = generateBinaryOp(node.op, leftValue, rightValue, opType);
        DEBUG_OUT("DEBUG BinaryExpr: Generated opCode='" << opCode << "'\n");
        emitLine(format("{} = {}", resultReg, opCode));
        valueMap_["__last_expr"] = resultReg;

        // Track result type
        if (node.op == "==" || node.op == "!=" || node.op == "<" || node.op == ">" ||
            node.op == "<=" || node.op == ">=") {
            registerTypes_[resultReg] = "NativeType<\"bool\">";
        } else {
            if (opType == "double") {
                registerTypes_[resultReg] = "NativeType<\"double\">";
            } else if (opType == "float") {
                registerTypes_[resultReg] = "NativeType<\"float\">";
            } else if (opType == "i8") {
                registerTypes_[resultReg] = "NativeType<\"int8\">";
            } else if (opType == "i16") {
                registerTypes_[resultReg] = "NativeType<\"int16\">";
            } else if (opType == "i32") {
                registerTypes_[resultReg] = "NativeType<\"int32\">";
            } else {
                registerTypes_[resultReg] = "NativeType<\"int64\">";
            }
        }
    }

    // New IR infrastructure: generate binary operations using IRBuilder
    if (builder_) {
        // IMPORTANT: Save valueMap_ state since re-visiting operands overwrites __last_expr
        std::string savedLastExpr = valueMap_["__last_expr"];
        // Re-visit operands to get IR values
        node.left->accept(*this);
        IR::Value* leftIR = lastExprValue_;
        node.right->accept(*this);
        IR::Value* rightIR = lastExprValue_;
        // Restore __last_expr after IR generation
        valueMap_["__last_expr"] = savedLastExpr;

        if (leftIR && rightIR) {
            // Generate appropriate binary operation
            if (node.op == "+") {
                lastExprValue_ = builder_->CreateAdd(leftIR, rightIR, "add");
            } else if (node.op == "-") {
                lastExprValue_ = builder_->CreateSub(leftIR, rightIR, "sub");
            } else if (node.op == "*") {
                lastExprValue_ = builder_->CreateMul(leftIR, rightIR, "mul");
            } else if (node.op == "/") {
                lastExprValue_ = builder_->CreateSDiv(leftIR, rightIR, "div");
            } else if (node.op == "%") {
                lastExprValue_ = builder_->CreateSRem(leftIR, rightIR, "rem");
            } else if (node.op == "==") {
                lastExprValue_ = builder_->CreateICmpEQ(leftIR, rightIR, "eq");
            } else if (node.op == "!=") {
                lastExprValue_ = builder_->CreateICmpNE(leftIR, rightIR, "ne");
            } else if (node.op == "<") {
                lastExprValue_ = builder_->CreateICmpSLT(leftIR, rightIR, "lt");
            } else if (node.op == "<=") {
                lastExprValue_ = builder_->CreateICmpSLE(leftIR, rightIR, "le");
            } else if (node.op == ">") {
                lastExprValue_ = builder_->CreateICmpSGT(leftIR, rightIR, "gt");
            } else if (node.op == ">=") {
                lastExprValue_ = builder_->CreateICmpSGE(leftIR, rightIR, "ge");
            } else if (node.op == "&&") {
                lastExprValue_ = builder_->CreateAnd(leftIR, rightIR, "and");
            } else if (node.op == "||") {
                lastExprValue_ = builder_->CreateOr(leftIR, rightIR, "or");
            } else if (node.op == "&") {
                lastExprValue_ = builder_->CreateAnd(leftIR, rightIR, "bitand");
            } else if (node.op == "|") {
                lastExprValue_ = builder_->CreateOr(leftIR, rightIR, "bitor");
            } else if (node.op == "^") {
                lastExprValue_ = builder_->CreateXor(leftIR, rightIR, "xor");
            } else if (node.op == "<<") {
                lastExprValue_ = builder_->CreateShl(leftIR, rightIR, "shl");
            } else if (node.op == ">>") {
                lastExprValue_ = builder_->CreateAShr(leftIR, rightIR, "shr");
            }
        }
    }
}

void LLVMBackend::visit(Parser::TypeRef& node) {
    // TypeRef nodes don't generate code themselves
    // The type is accessed by the parent node (e.g., InstantiateStmt, ParameterDecl)
}

void LLVMBackend::visit(Parser::AssignmentStmt& node) {
    // Generate code for the value expression
    node.value->accept(*this);
    std::string valueReg = valueMap_["__last_expr"];

    // Capture IR value from expression
    IR::Value* valueIR = lastExprValue_;

    // Handle different types of lvalue expressions
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.target.get())) {
        // Simple variable assignment: Set x = value;
        std::string varName = identExpr->name;

        auto targetIt = variables_.find(varName);
        if (targetIt != variables_.end()) {
            // Store the value to the variable's memory location using the actual LLVM type
            std::string llvmType = targetIt->second.llvmType.empty() ? "i64" : targetIt->second.llvmType;
            std::string storeValue = valueReg;

            // FIX: Handle raw boolean literals being assigned to Bool^ variables
            if (targetIt->second.type == "Bool" && llvmType == "ptr") {
                if (storeValue == "true" || storeValue == "false") {
                    // Wrap i1 value with Bool_Constructor
                    std::string wrapReg = allocateRegister();
                    emitLine(format("{} = call ptr @Bool_Constructor(i1 {})", wrapReg, storeValue));
                    storeValue = wrapReg;

                    // IR: Call Bool_Constructor to wrap i1 value
                    if (builder_) {
                        IR::Value* boolVal = builder_->getInt1(valueReg == "true");
                        IR::Function* boolCtor = module_->getFunction("Bool_Constructor");
                        if (boolCtor) {
                            valueIR = builder_->CreateCall(boolCtor, {boolVal}, "bool_wrap");
                        }
                    }
                }
            }

            emitLine("store " + llvmType + " " + storeValue + ", ptr " + targetIt->second.llvmRegister);

            // IR generation: Store to local alloca
            if (builder_ && valueIR) {
                auto allocaIt = localAllocas_.find(varName);
                if (allocaIt != localAllocas_.end()) {
                    builder_->CreateStore(valueIR, allocaIt->second);
                }
            }
        } else if (valueMap_.find(varName) != valueMap_.end()) {
            // Fallback for variables not in ownership tracking
            emitLine("store i64 " + valueReg + ", ptr " + valueMap_[varName]);

            // IR generation: Store to local alloca
            if (builder_ && valueIR) {
                auto allocaIt = localAllocas_.find(varName);
                if (allocaIt != localAllocas_.end()) {
                    builder_->CreateStore(valueIR, allocaIt->second);
                }
            }
        } else if (!currentClassName_.empty()) {
            // Check if it's a class property (implicit this.propertyName)
            auto classIt = classes_.find(currentClassName_);
            if (classIt != classes_.end()) {
                const auto& properties = classIt->second.properties;
                bool found = false;
                for (size_t i = 0; i < properties.size(); ++i) {
                    if (std::get<0>(properties[i]) == varName) {
                        // Generate getelementptr to get address of the field
                        std::string ptrReg = allocateRegister();

                        // Mangle class name using TypeNormalizer
                        std::string mangledTypeName = TypeNormalizer::mangleForLLVM(currentClassName_);

                        emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                ", ptr %this, i32 0, i32 " + std::to_string(i));

                        // Store the value to the field
                        std::string llvmType = std::get<1>(properties[i]);
                        emitLine("store " + llvmType + " " + valueReg + ", ptr " + ptrReg);

                        // IR: Create GEP and Store for implicit this.property assignment
                        if (builder_ && currentFunction_ && valueIR) {
                            IR::Value* thisPtrIR = currentFunction_->getArg(0);
                            if (thisPtrIR) {
                                IR::Value* fieldPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtrIR, static_cast<unsigned>(i), varName + "_implicit_ptr");
                                builder_->CreateStore(valueIR, fieldPtrIR);
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    emitLine("; Error: variable " + varName + " not found");
                }
            } else {
                emitLine("; Error: variable " + varName + " not found");
            }
        } else {
            emitLine("; Error: variable " + varName + " not found");
        }
    }
    else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(node.target.get())) {
        // Member access assignment: Set this.x = value; or Set obj.field = value;

        // Check if the object is 'this'
        if (dynamic_cast<Parser::ThisExpr*>(memberExpr->object.get())) {
            // Handle: Set this.memberName = value;
            std::string memberName = memberExpr->member;

            // Remove the accessor prefix (. or ::) from member name
            if (memberName.length() > 0 && (memberName[0] == '.' || memberName[0] == ':')) {
                size_t start = (memberName[0] == ':' && memberName.length() > 1 && memberName[1] == ':') ? 2 : 1;
                memberName = memberName.substr(start);
            }

            // Get the current class type
            if (!currentClassName_.empty()) {
                auto classIt = classes_.find(currentClassName_);
                if (classIt != classes_.end()) {
                    // Find the property index
                    const auto& properties = classIt->second.properties;
                    for (size_t i = 0; i < properties.size(); ++i) {
                        if (std::get<0>(properties[i]) == memberName) {
                            // Generate getelementptr to get address of the field
                            std::string ptrReg = allocateRegister();

                            // Mangle class name using TypeNormalizer
                            std::string mangledTypeName = TypeNormalizer::mangleForLLVM(currentClassName_);

                            emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                    ", ptr %this, i32 0, i32 " + std::to_string(i));

                            // Store the value to the field
                            std::string llvmType = std::get<1>(properties[i]); // LLVM type
                            emitLine("store " + llvmType + " " + valueReg + ", ptr " + ptrReg);

                            // IR: Create GEP and Store for this.property assignment
                            if (builder_ && currentFunction_ && valueIR) {
                                IR::Value* thisPtrIR = currentFunction_->getArg(0);
                                if (thisPtrIR) {
                                    IR::Value* fieldPtrIR = builder_->CreateStructGEP(builder_->getPtrTy(), thisPtrIR, static_cast<unsigned>(i), memberName + "_assign_ptr");
                                    builder_->CreateStore(valueIR, fieldPtrIR);
                                }
                            }
                            return;
                        }
                    }
                    emitLine("; Error: property " + memberName + " not found in class " + currentClassName_);
                } else {
                    emitLine("; Error: class " + currentClassName_ + " not found");
                }
            } else {
                emitLine("; Error: 'this' used outside of class context");
            }
        } else {
            emitLine("; Error: Complex member access assignments not yet supported");
        }
    }
    else if (dynamic_cast<Parser::ThisExpr*>(node.target.get())) {
        // Assignment to 'this' itself: Set this = value;
        emitLine("store i64 " + valueReg + ", ptr %this");

        // IR: Store to this pointer
        if (builder_ && currentFunction_ && valueIR) {
            IR::Value* thisPtrIR = currentFunction_->getArg(0);
            if (thisPtrIR) {
                builder_->CreateStore(valueIR, thisPtrIR);
            }
        }
    }
    else {
        emitLine("; Error: Unsupported lvalue type in assignment");
    }
}

void LLVMBackend::visit(Parser::TypeOfExpr& node) {
    // TypeOf is a compile-time feature, emit a comment
    emitLine("; typeof expression (compile-time only)");
    valueMap_["__last_expr"] = "null";  // Placeholder
}

void LLVMBackend::visit(Parser::LambdaExpr& node) {
    // For template lambdas WITH captures, we still need to create the closure
    // containing captured values - only the function body is deferred
    if (!node.templateParams.empty()) {
        if (node.captures.empty()) {
            // No captures - can use null closure
            emitLine("; Template lambda (no captures) - skipping definition");
            valueMap_["__last_expr"] = "null";
            return;
        }

        // Template lambda WITH captures - create closure struct to hold captures
        emitLine("; Template lambda with captures - creating closure for captured values");

        // Build closure struct type: { ptr (func placeholder), ptr (capture0), ... }
        std::string closureTypeStr = "{ ptr";
        for (size_t i = 0; i < node.captures.size(); ++i) {
            closureTypeStr += ", ptr";
        }
        closureTypeStr += " }";

        // Allocate closure on heap
        std::string closureReg = allocateRegister();
        emitLine(format("{} = call ptr @xxml_malloc(i64 {})",
                        closureReg, 8 * (1 + node.captures.size())));

        // Store null function pointer at offset 0 (will be resolved at call time)
        std::string funcPtrSlot = allocateRegister();
        emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 0",
                        funcPtrSlot, closureTypeStr, closureReg));
        emitLine(format("store ptr null, ptr {}", funcPtrSlot));

        // Store captured values at offsets 1, 2, ...
        for (size_t i = 0; i < node.captures.size(); ++i) {
            const auto& capture = node.captures[i];
            std::string captureSlot = allocateRegister();
            emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 {}",
                            captureSlot, closureTypeStr, closureReg, i + 1));

            std::string capturedValue;
            auto varIt = variables_.find(capture.varName);
            if (varIt != variables_.end()) {
                if (capture.isReference()) {
                    capturedValue = varIt->second.llvmRegister;
                } else {
                    std::string loadReg = allocateRegister();
                    emitLine(format("{} = load ptr, ptr {}", loadReg, varIt->second.llvmRegister));
                    capturedValue = loadReg;
                }
            } else {
                auto paramIt = valueMap_.find(capture.varName);
                if (paramIt != valueMap_.end()) {
                    capturedValue = paramIt->second;
                } else {
                    emitLine(format("; Warning: captured variable '{}' not found", capture.varName));
                    capturedValue = "null";
                }
            }

            emitLine(format("store ptr {}, ptr {}", capturedValue, captureSlot));
        }

        valueMap_["__last_expr"] = closureReg;
        registerTypes_[closureReg] = "__function";
        return;
    }

    // Generate unique names for this lambda
    int lambdaId = lambdaCounter_++;
    std::string closureTypeName = "%closure." + std::to_string(lambdaId);
    std::string lambdaFuncName = "@lambda." + std::to_string(lambdaId);

    emitLine(format("; Lambda_Expr_#{}_TemplateParams_{}", lambdaId, node.templateParams.size()));

    // Determine return type
    std::string returnType = "ptr";  // Default to ptr for objects
    if (node.returnType) {
        returnType = getLLVMType(node.returnType->typeName);
    }

    // Build parameter types list (closure ptr + actual params)
    std::vector<std::string> paramTypes;
    paramTypes.push_back("ptr");  // First param is always closure pointer
    for (const auto& param : node.parameters) {
        std::string paramType = getLLVMType(param->type->typeName);
        paramTypes.push_back(paramType);
    }

    // Build closure struct type: { ptr (func), ptr (capture0), ptr (capture1), ... }
    std::stringstream closureTypeFields;
    closureTypeFields << "{ ptr";  // Function pointer
    for (size_t i = 0; i < node.captures.size(); ++i) {
        closureTypeFields << ", ptr";  // Each capture is a ptr
    }
    closureTypeFields << " }";
    std::string closureTypeStr = closureTypeFields.str();

    // Track lambda info for .call() invocations
    LambdaInfo info;
    info.closureTypeName = closureTypeName;
    info.functionName = lambdaFuncName;
    info.returnType = returnType;
    info.paramTypes = paramTypes;
    for (const auto& capture : node.captures) {
        info.captures.push_back({capture.varName, capture.mode});
    }

    // Generate lambda function definition (deferred until end of current function)
    // The lambda function takes closure as first parameter
    std::stringstream lambdaDef;
    lambdaDef << "\n; Lambda function " << lambdaId << "\n";
    lambdaDef << closureTypeName << " = type " << closureTypeStr << "\n";
    lambdaDef << "define " << returnType << " " << lambdaFuncName << "(ptr %closure";
    for (size_t i = 0; i < node.parameters.size(); ++i) {
        lambdaDef << ", " << paramTypes[i + 1] << " %" << node.parameters[i]->name;
    }
    lambdaDef << ") {\n";
    lambdaDef << "entry:\n";

    // Load captured values from closure struct
    for (size_t i = 0; i < node.captures.size(); ++i) {
        const auto& capture = node.captures[i];
        std::string captureReg = "%capture." + capture.varName;
        lambdaDef << "  " << captureReg << ".ptr = getelementptr inbounds "
                  << closureTypeName << ", ptr %closure, i32 0, i32 " << (i + 1) << "\n";
        if (capture.isReference()) {
            // Reference (&): closure stores pointer to the variable's alloca
            // First load gives us the alloca ptr, second load gives us the value
            lambdaDef << "  " << captureReg << ".ref = load ptr, ptr " << captureReg << ".ptr\n";
            lambdaDef << "  " << captureReg << " = load ptr, ptr " << captureReg << ".ref\n";
        } else {
            // Owned (^) or Copy (%): closure stores the value directly
            lambdaDef << "  " << captureReg << " = load ptr, ptr " << captureReg << ".ptr\n";
        }
    }

    // Look up captured variable types BEFORE saving/clearing context
    std::unordered_map<std::string, std::string> captureTypes;
    for (const auto& capture : node.captures) {
        auto varIt = variables_.find(capture.varName);
        if (varIt != variables_.end()) {
            captureTypes[capture.varName] = varIt->second.type;
        } else {
            // Fallback to checking registerTypes via valueMap
            auto valIt = valueMap_.find(capture.varName);
            if (valIt != valueMap_.end()) {
                auto regIt = registerTypes_.find(valIt->second);
                if (regIt != registerTypes_.end()) {
                    captureTypes[capture.varName] = regIt->second;
                }
            }
        }
    }

    // Save current context
    auto savedVariables = variables_;
    auto savedValueMap = valueMap_;
    auto savedRegisterTypes = registerTypes_;
    auto savedOutput = output_.str();
    auto savedReturnType = currentFunctionReturnType_;
    output_.str("");

    // Set return type for lambda body
    currentFunctionReturnType_ = returnType;

    // Set up parameter and capture mappings for lambda body
    valueMap_.clear();
    variables_.clear();

    // Map captured variables with their actual types
    for (const auto& capture : node.captures) {
        std::string captureReg = "%capture." + capture.varName;
        valueMap_[capture.varName] = captureReg;
        // Use the actual type if found, otherwise fall back to "ptr"
        auto typeIt = captureTypes.find(capture.varName);
        if (typeIt != captureTypes.end()) {
            registerTypes_[captureReg] = typeIt->second;
        } else {
            registerTypes_[captureReg] = "ptr";
        }
    }

    // Map parameters with ownership modifiers
    for (const auto& param : node.parameters) {
        valueMap_[param->name] = "%" + param->name;
        // Include ownership modifier in the type string for proper handling
        std::string typeWithOwnership = param->type->typeName;
        switch (param->type->ownership) {
            case Parser::OwnershipType::Reference: typeWithOwnership += "&"; break;
            case Parser::OwnershipType::Owned: typeWithOwnership += "^"; break;
            case Parser::OwnershipType::Copy: typeWithOwnership += "%"; break;
            default: break;
        }
        registerTypes_["%" + param->name] = typeWithOwnership;
    }

    // Check if body contains a return statement
    bool hasReturn = false;
    for (const auto& stmt : node.body) {
        if (dynamic_cast<Parser::ReturnStmt*>(stmt.get())) {
            hasReturn = true;
            break;
        }
    }

    // Generate lambda body statements
    for (const auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Get generated body and add to lambda definition
    std::string bodyCode = output_.str();
    // Split by lines and add proper indentation
    std::istringstream iss(bodyCode);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            lambdaDef << "  " << line << "\n";
        }
    }

    // Only add default return if body doesn't have one
    if (!hasReturn) {
        if (returnType == "ptr") {
            lambdaDef << "  ret ptr null\n";
        } else if (returnType == "void") {
            lambdaDef << "  ret void\n";
        } else if (returnType == "i64") {
            lambdaDef << "  ret i64 0\n";
        } else if (returnType == "i1") {
            lambdaDef << "  ret i1 false\n";
        } else {
            lambdaDef << "  ret " << returnType << " zeroinitializer\n";
        }
    }
    lambdaDef << "}\n";

    // Store lambda definition for later emission
    pendingLambdaDefinitions_.push_back(lambdaDef.str());

    // Restore context - use clear() and seekp() to ensure proper positioning
    output_.str(savedOutput);
    output_.clear();  // Clear any error flags
    output_.seekp(0, std::ios_base::end);  // Seek to end for appending
    variables_ = savedVariables;
    valueMap_ = savedValueMap;
    registerTypes_ = savedRegisterTypes;
    currentFunctionReturnType_ = savedReturnType;

    // Now generate closure allocation and initialization at the call site
    // Allocate closure on stack
    std::string closureReg = allocateRegister();
    emitLine(format("{} = alloca {}, align 8", closureReg, closureTypeStr));

    // IR: Create alloca for closure
    IR::Value* closureIR = nullptr;
    if (builder_ && currentBlock_) {
        closureIR = builder_->CreateAlloca(builder_->getPtrTy(), nullptr, "closure");
    }

    // Store function pointer at offset 0
    std::string funcPtrSlot = allocateRegister();
    emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 0",
                    funcPtrSlot, closureTypeStr, closureReg));
    emitLine(format("store ptr {}, ptr {}", lambdaFuncName, funcPtrSlot));

    // IR: GEP and store function pointer
    if (builder_ && closureIR) {
        IR::Value* funcSlotIR = builder_->CreateGEP(builder_->getPtrTy(), closureIR, {builder_->getInt32(0), builder_->getInt32(0)}, "func_slot");
        builder_->CreateStore(builder_->getNullPtr(), funcSlotIR);  // Placeholder - lambda func would be stored
    }

    // Store captured values at offsets 1, 2, ...
    for (size_t i = 0; i < node.captures.size(); ++i) {
        const auto& capture = node.captures[i];
        std::string captureSlot = allocateRegister();
        emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 {}",
                        captureSlot, closureTypeStr, closureReg, i + 1));

        // IR: GEP for capture slot
        if (builder_ && closureIR) {
            lastExprValue_ = builder_->CreateGEP(builder_->getPtrTy(), closureIR, {builder_->getInt32(0), builder_->getInt32(static_cast<unsigned>(i + 1))}, "cap_gep");
        }

        // Get the captured variable's value based on capture mode
        std::string capturedValue;
        auto varIt = variables_.find(capture.varName);
        if (varIt != variables_.end()) {
            if (capture.isReference()) {
                // Reference (&): store the pointer itself (alloca address)
                capturedValue = varIt->second.llvmRegister;
            } else if (capture.isOwned()) {
                // Owned (^): move the value into the closure
                std::string loadReg = allocateRegister();
                emitLine(format("{} = load ptr, ptr {}", loadReg, varIt->second.llvmRegister));
                capturedValue = loadReg;
                // Note: In a full implementation, we'd invalidate the original variable here

                // IR: Load owned capture
                if (builder_) {
                    auto allocaIt = localAllocas_.find(capture.varName);
                    if (allocaIt != localAllocas_.end()) {
                        lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, "owned_capture");
                    }
                }
            } else {
                // Copy (%): copy the value into the closure
                std::string loadReg = allocateRegister();
                emitLine(format("{} = load ptr, ptr {}", loadReg, varIt->second.llvmRegister));
                capturedValue = loadReg;

                // IR: Load copy capture
                if (builder_) {
                    auto allocaIt = localAllocas_.find(capture.varName);
                    if (allocaIt != localAllocas_.end()) {
                        lastExprValue_ = builder_->CreateLoad(builder_->getPtrTy(), allocaIt->second, "copy_capture");
                    }
                }
            }
        } else {
            auto paramIt = valueMap_.find(capture.varName);
            if (paramIt != valueMap_.end()) {
                capturedValue = paramIt->second;
            } else {
                emitLine(format("; Warning: captured variable '{}' not found", capture.varName));
                capturedValue = "null";
            }
        }

        emitLine(format("store ptr {}, ptr {}", capturedValue, captureSlot));

        // IR: Store captured value
        if (builder_ && closureIR) {
            IR::Value* capSlotIR = builder_->CreateGEP(builder_->getPtrTy(), closureIR, {builder_->getInt32(0), builder_->getInt32(static_cast<unsigned>(i + 1))}, "cap_slot");
            builder_->CreateStore(builder_->getNullPtr(), capSlotIR);  // Placeholder
        }
    }

    // Store lambda info keyed by closure register
    lambdaInfos_[closureReg] = info;

    // The closure pointer is the result
    valueMap_["__last_expr"] = closureReg;
    registerTypes_[closureReg] = "__function";  // Mark as lambda/function type
}

void LLVMBackend::visit(Parser::FunctionTypeRef& node) {
    // FunctionTypeRef is handled during type resolution
    emitLine("; function type reference");
}

void LLVMBackend::visit(Parser::ConstraintDecl& node) {
    // Constraints are compile-time only
    emitLine(format("; constraint {}", node.name));
}

void LLVMBackend::visit(Parser::RequireStmt& node) {
    // Require statements are compile-time only
    emitLine("; require statement (compile-time only)");
}

void LLVMBackend::visit(Parser::AnnotateDecl& node) {
    // Annotate declarations are part of annotation definitions (compile-time only)
    emitLine(format("; annotate parameter: {}", node.name));
}

void LLVMBackend::visit(Parser::ProcessorDecl& node) {
    if (!processorMode_) {
        // Processor declarations are compile-time only in normal mode
        emitLine("; annotation processor (compile-time only)");
        return;
    }

    // In processor mode, generate code for the processor class
    // The processor class name is based on the annotation name
    std::string processorClassName = processorAnnotationName_ + "_Processor";

    emitLine("");
    emitLine(format("; Processor class: {}", processorClassName));

    // Generate methods from the processor block
    for (const auto& section : node.sections) {
        for (const auto& decl : section->declarations) {
            if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Generate the method
                std::string savedClassName = currentClassName_;
                currentClassName_ = processorClassName;

                methodDecl->accept(*this);

                currentClassName_ = savedClassName;
            }
        }
    }
}

void LLVMBackend::visit(Parser::AnnotationDecl& node) {
    // Track retained annotations for code generation
    if (node.retainAtRuntime) {
        retainedAnnotations_.insert(node.name);
        emitLine(format("; annotation definition (retained): {}", node.name));

        // IR: Retained annotation marker
        if (builder_) {
            (void)builder_->getInt8Ty();
        }
    } else {
        emitLine(format("; annotation definition: {}", node.name));

        // IR: Annotation definition marker
        if (builder_) {
            (void)builder_->getInt32Ty();
        }
    }

    // In processor mode, generate code for the processor block
    if (processorMode_ && node.processor) {
        processorAnnotationName_ = node.name;
        node.processor->accept(*this);
    }
}

void LLVMBackend::visit(Parser::AnnotationUsage& node) {
    // Annotation usages are compile-time only (unless retained)
    emitLine(format("; annotation usage: @{}", node.annotationName));

    // IR: Annotation usage marker
    if (builder_) {
        (void)builder_->getInt32Ty();
    }
}

// ============================================
// Annotation Code Generation
// ============================================

bool LLVMBackend::isAnnotationRetained(const std::string& annotationName) const {
    return retainedAnnotations_.find(annotationName) != retainedAnnotations_.end();
}

LLVMBackend::AnnotationArgValue LLVMBackend::evaluateAnnotationArg(Parser::Expression* expr) {
    AnnotationArgValue result;
    result.kind = AnnotationArgValue::Integer;
    result.intValue = 0;

    if (!expr) return result;

    // Handle integer literals
    if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Integer;
        result.intValue = intLit->value;
        return result;
    }

    // Handle string literals
    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::String;
        result.stringValue = strLit->value;
        return result;
    }

    // Handle bool literals
    if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Bool;
        result.boolValue = boolLit->value;
        return result;
    }

    // Handle float literals
    if (auto* floatLit = dynamic_cast<Parser::FloatLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Float;
        result.floatValue = floatLit->value;
        return result;
    }

    // Handle double literals
    if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Double;
        result.doubleValue = doubleLit->value;
        return result;
    }

    // Handle String::Constructor("...") pattern
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        if (!callExpr->arguments.empty()) {
            if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(callExpr->arguments[0].get())) {
                result.kind = AnnotationArgValue::String;
                result.stringValue = strLit->value;
                return result;
            }
            if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(callExpr->arguments[0].get())) {
                result.kind = AnnotationArgValue::Integer;
                result.intValue = intLit->value;
                return result;
            }
        }
    }

    return result;
}

void LLVMBackend::collectRetainedAnnotations(Parser::ClassDecl& node) {
    std::string fullClassName = currentNamespace_.empty() ? node.name : currentNamespace_ + "::" + node.name;

    for (const auto& annotation : node.annotations) {
        if (isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "type";
            metadata.typeName = fullClassName;
            metadata.memberName = "";

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            pendingAnnotationMetadata_.push_back(metadata);
        }
    }
}

void LLVMBackend::collectRetainedAnnotations(Parser::MethodDecl& node, const std::string& className) {
    for (const auto& annotation : node.annotations) {
        if (isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "method";
            metadata.typeName = className;
            metadata.memberName = node.name;

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            pendingAnnotationMetadata_.push_back(metadata);
        }
    }
}

void LLVMBackend::collectRetainedAnnotations(Parser::PropertyDecl& node, const std::string& className) {
    for (const auto& annotation : node.annotations) {
        if (isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "property";
            metadata.typeName = className;
            metadata.memberName = node.name;

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            pendingAnnotationMetadata_.push_back(metadata);
        }
    }
}

void LLVMBackend::generateAnnotationMetadata() {
    if (pendingAnnotationMetadata_.empty()) return;

    emitLine("");
    emitLine("; ============================================");
    emitLine("; Annotation Metadata");
    emitLine("; ============================================");
    emitLine("");

    // Group annotations by target (type, type+method, type+property)
    struct TargetAnnotations {
        std::string targetType;
        std::string typeName;
        std::string memberName;
        std::vector<const PendingAnnotationMetadata*> annotations;
    };

    std::vector<TargetAnnotations> groupedAnnotations;

    for (const auto& meta : pendingAnnotationMetadata_) {
        bool found = false;
        for (auto& group : groupedAnnotations) {
            if (group.targetType == meta.targetType &&
                group.typeName == meta.typeName &&
                group.memberName == meta.memberName) {
                group.annotations.push_back(&meta);
                found = true;
                break;
            }
        }
        if (!found) {
            TargetAnnotations newGroup;
            newGroup.targetType = meta.targetType;
            newGroup.typeName = meta.typeName;
            newGroup.memberName = meta.memberName;
            newGroup.annotations.push_back(&meta);
            groupedAnnotations.push_back(newGroup);
        }
    }

    // Generate metadata for each target group
    for (const auto& group : groupedAnnotations) {
        int groupId = annotationMetadataCounter_++;

        // Generate annotation argument arrays and annotation info structs
        std::vector<std::string> annotationInfoNames;

        for (size_t i = 0; i < group.annotations.size(); ++i) {
            const auto* meta = group.annotations[i];
            std::string prefix = format("@__annotation_{}_{}", groupId, i);

            // Generate annotation name string
            std::string nameStr = prefix + "_name";
            emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                           nameStr, meta->annotationName.length() + 1, meta->annotationName));
            if (builder_) { (void)builder_->getPtrTy(); }  // IR marker

            // Generate arguments array if there are any
            std::string argsArrayName = "null";
            if (!meta->arguments.empty()) {
                // Generate each argument
                std::vector<std::string> argStructNames;
                for (size_t j = 0; j < meta->arguments.size(); ++j) {
                    const auto& arg = meta->arguments[j];
                    std::string argPrefix = format("{}_arg_{}", prefix, j);

                    // Generate argument name string
                    std::string argNameStr = argPrefix + "_name";
                    emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                                   argNameStr, arg.first.length() + 1, arg.first));
                    if (builder_) { (void)builder_->getInt8Ty(); }  // IR marker

                    // Generate argument value based on type
                    // ReflectionAnnotationArg struct: { ptr name, i32 valueType, [24 x i8] value_union }
                    std::string argStructName = argPrefix + "_struct";

                    int valueType = static_cast<int>(arg.second.kind);
                    std::string valueBytes;

                    // For string values, we use a separate global and store the pointer
                    // The ReflectionAnnotationArg struct uses a union that we represent as ptr
                    std::string valueInit;
                    switch (arg.second.kind) {
                        case AnnotationArgValue::Integer:
                            // Store integer value directly (it will be interpreted as int64)
                            valueInit = format("i64 {}", arg.second.intValue);
                            break;
                        case AnnotationArgValue::String: {
                            // Generate string constant and store pointer to it
                            std::string strValName = argPrefix + "_strval";
                            emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                                           strValName, arg.second.stringValue.length() + 1, arg.second.stringValue));
                            if (builder_) { (void)builder_->getInt1Ty(); }  // IR marker
                            valueInit = format("ptr {}", strValName);
                            break;
                        }
                        case AnnotationArgValue::Bool:
                            valueInit = format("i64 {}", arg.second.boolValue ? 1 : 0);
                            break;
                        case AnnotationArgValue::Float:
                            // Store float bits as i32, zero-extended to i64
                            valueInit = format("i64 {}", *reinterpret_cast<const uint32_t*>(&arg.second.floatValue));
                            break;
                        case AnnotationArgValue::Double:
                            // Store double bits as i64
                            valueInit = format("i64 {}", *reinterpret_cast<const uint64_t*>(&arg.second.doubleValue));
                            break;
                    }

                    // ReflectionAnnotationArg: { ptr name, i32 valueType, ptr/i64 value }
                    // Use a simpler struct layout: { ptr, i32, i64 } where value is stored as i64
                    // For strings, the ptr is stored in the i64 field (pointer fits in i64 on 64-bit)
                    std::string structType = (arg.second.kind == AnnotationArgValue::String)
                        ? "{ ptr, i32, ptr }"
                        : "{ ptr, i32, i64 }";
                    emitLine(format("{} = private unnamed_addr constant {} {{ ptr {}, i32 {}, {} }}",
                                   argStructName, structType, argNameStr, valueType, valueInit));
                    if (builder_) { (void)builder_->getInt32Ty(); }  // IR marker

                    argStructNames.push_back(argStructName);
                }

                // Generate array of argument pointers
                argsArrayName = prefix + "_args";
                std::stringstream argsInit;
                argsInit << "[ ";
                for (size_t j = 0; j < argStructNames.size(); ++j) {
                    if (j > 0) argsInit << ", ";
                    argsInit << "ptr " << argStructNames[j];
                }
                argsInit << " ]";
                emitLine(format("{} = private unnamed_addr constant [{} x ptr] {}",
                               argsArrayName, argStructNames.size(), argsInit.str()));
                if (builder_) { (void)builder_->getInt64Ty(); }  // IR marker
            }

            // Generate ReflectionAnnotationInfo struct
            // { ptr annotationName, i32 argumentCount, ptr arguments }
            std::string infoName = prefix + "_info";
            emitLine(format("{} = private unnamed_addr constant {{ ptr, i32, ptr }} {{ ptr {}, i32 {}, ptr {} }}",
                           infoName, nameStr, meta->arguments.size(),
                           meta->arguments.empty() ? "null" : argsArrayName));
            if (builder_) { (void)builder_->getFloatTy(); }  // IR marker

            annotationInfoNames.push_back(infoName);
        }

        // Generate array of annotation infos for this target
        std::string infosArrayName = format("@__annotation_{}_infos", groupId);
        std::stringstream infosInit;
        infosInit << "[ ";
        for (size_t i = 0; i < annotationInfoNames.size(); ++i) {
            if (i > 0) infosInit << ", ";
            infosInit << "ptr " << annotationInfoNames[i];
        }
        infosInit << " ]";
        emitLine(format("{} = private unnamed_addr constant [{} x ptr] {}",
                       infosArrayName, annotationInfoNames.size(), infosInit.str()));
        if (builder_) { (void)builder_->getDoubleTy(); }  // IR marker

        // Generate type name string
        std::string typeNameStr = format("@__annotation_{}_typename", groupId);
        emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                       typeNameStr, group.typeName.length() + 1, group.typeName));
        if (builder_) { (void)builder_->getVoidTy(); }  // IR marker

        // Generate member name string if needed
        std::string memberNameStr = "null";
        if (!group.memberName.empty()) {
            memberNameStr = format("@__annotation_{}_membername", groupId);
            emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                           memberNameStr, group.memberName.length() + 1, group.memberName));
            if (builder_) { (void)builder_->getPtrTy(); }  // IR marker
        }

        // Generate registration call in init function
        // This will be added to the module init section
        emitLine(format("; Registration for {} {}::{}",
                       group.targetType, group.typeName, group.memberName));
        if (builder_) { (void)builder_->getInt8Ty(); }  // IR marker
    }

    emitLine("");
}

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

    // IR: Create annotation name getter function
    if (builder_ && module_) {
        auto* getNameFunc = module_->createFunction(
            module_->getContext().getFunctionTy(builder_->getPtrTy(), {}, false),
            "__xxml_processor_annotation_name",
            IR::Function::Linkage::External
        );
        if (getNameFunc) {
            auto* entry = getNameFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entry);
            builder_->CreateRet(builder_->getNullPtr());  // Placeholder
        }
    }

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

    // IR: Create void return for processor
    if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
        builder_->CreateRetVoid();
    }

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

std::unique_ptr<Parser::MethodDecl> LLVMBackend::cloneAndSubstituteMethodDecl(
    Parser::MethodDecl* original,
    const std::string& newName,
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

    // Create method with the new (mangled) name
    auto clonedMethod = std::make_unique<Parser::MethodDecl>(
        newName,  // Use the provided mangled name
        std::move(clonedRetType),
        std::move(clonedParams),
        std::move(clonedBody),
        original->location
    );

    // Clear template params since this is an instantiated (concrete) method
    clonedMethod->templateParams.clear();

    return clonedMethod;
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
        auto clonedTarget = cloneExpression(assign->target.get(), typeMap);
        auto clonedValue = cloneExpression(assign->value.get(), typeMap);
        return std::make_unique<Parser::AssignmentStmt>(
            std::move(clonedTarget),
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
        // Check for __typename intrinsic - replaces __typename(T) with the actual type name string
        bool isTypenameIntrinsic = false;
        if (auto* calleeIdent = dynamic_cast<const Parser::IdentifierExpr*>(call->callee.get())) {
            isTypenameIntrinsic = (calleeIdent->name == "__typename");
        } else if (auto* calleeMember = dynamic_cast<const Parser::MemberAccessExpr*>(call->callee.get())) {
            // Also support Syscall::typename syntax
            if (auto* baseIdent = dynamic_cast<const Parser::IdentifierExpr*>(calleeMember->object.get())) {
                isTypenameIntrinsic = (baseIdent->name == "Syscall" && calleeMember->member == "::typename");
            }
        }

        if (isTypenameIntrinsic && call->arguments.size() == 1) {
            // Check if the argument is a template parameter
            if (auto* argIdent = dynamic_cast<const Parser::IdentifierExpr*>(call->arguments[0].get())) {
                auto it = typeMap.find(argIdent->name);
                if (it != typeMap.end()) {
                    // Found! Replace the entire call with a string literal of the type name
                    return std::make_unique<Parser::StringLiteralExpr>(it->second, call->location);
                }
            }
        }

        // Not a __typename intrinsic, clone normally
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
    if (auto* thisExpr = dynamic_cast<const Parser::ThisExpr*>(expr)) {
        return std::make_unique<Parser::ThisExpr>(thisExpr->location);
    }

    // For unknown expression types, return nullptr
    return nullptr;
}

std::unique_ptr<Parser::LambdaExpr> LLVMBackend::cloneLambdaExpr(
    const Parser::LambdaExpr* lambda,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!lambda) return nullptr;

    // Clone captures (they don't have types that need substitution)
    std::vector<Parser::LambdaExpr::CaptureSpec> captures = lambda->captures;

    // Clone parameters with type substitution
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    for (const auto& param : lambda->parameters) {
        auto clonedType = cloneTypeRef(param->type.get(), typeMap);
        params.push_back(std::make_unique<Parser::ParameterDecl>(
            param->name, std::move(clonedType), param->location));
    }

    // Clone return type with substitution
    auto clonedReturnType = cloneTypeRef(lambda->returnType.get(), typeMap);

    // Clone body statements with type substitution
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;
    for (const auto& stmt : lambda->body) {
        if (auto clonedStmt = cloneStatement(stmt.get(), typeMap)) {
            bodyStmts.push_back(std::move(clonedStmt));
        }
    }

    auto cloned = std::make_unique<Parser::LambdaExpr>(
        std::move(captures),
        std::move(params),
        std::move(clonedReturnType),
        std::move(bodyStmts),
        lambda->location);

    cloned->isCompiletime = lambda->isCompiletime;
    // Don't copy templateParams - the cloned lambda is a concrete instantiation

    return cloned;
}

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

void LLVMBackend::generateReflectionMetadata() {
    if (reflectionMetadata_.empty()) {
        return;  // No classes to generate metadata for
    }

    emitLine("; ============================================");
    emitLine("; Reflection Metadata");
    emitLine("; ============================================");
    emitLine("");

    // Define reflection structures
    emitLine("; Reflection structure types");
    if (builder_) { (void)builder_->getPtrTy(); }  // IR marker

    emitLine("%ReflectionPropertyInfo = type { ptr, ptr, i32, i64 }");
    if (builder_) { (void)builder_->getInt8Ty(); }  // IR marker

    emitLine("%ReflectionParameterInfo = type { ptr, ptr, i32 }");
    if (builder_) { (void)builder_->getInt1Ty(); }  // IR marker

    emitLine("%ReflectionMethodInfo = type { ptr, ptr, i32, i32, ptr, ptr, i1, i1 }");  // name, returnType, returnOwnership, paramCount, params, funcPtr, isStatic, isCtor
    if (builder_) { (void)builder_->getInt32Ty(); }  // IR marker

    emitLine("%ReflectionTemplateParamInfo = type { ptr }");
    if (builder_) { (void)builder_->getInt64Ty(); }  // IR marker

    emitLine("%ReflectionTypeInfo = type { ptr, ptr, ptr, i1, i32, ptr, i32, ptr, i32, ptr, i32, ptr, ptr, i64 }");
    if (builder_) { (void)builder_->getFloatTy(); }  // IR marker

    emitLine("");

    // IR: Create reflection struct types
    if (module_) {
        module_->createStructType("ReflectionPropertyInfo");
        module_->createStructType("ReflectionParameterInfo");
        module_->createStructType("ReflectionMethodInfo");
        module_->createStructType("ReflectionTemplateParamInfo");
        module_->createStructType("ReflectionTypeInfo");
    }

    // Ownership enum values
    emitLine("; Ownership types: 0=Unknown, 1=Owned(^), 2=Reference(&), 3=Copy(%)");
    emitLine("");

    // Helper to convert ownership char to enum value
    auto ownershipToInt = [](const std::string& ownership) -> int32_t {
        if (ownership.empty()) return 0;
        char c = ownership[0];
        if (c == '^') return 1;  // Owned
        if (c == '&') return 2;  // Reference
        if (c == '%') return 3;  // Copy
        return 0;  // Unknown
    };

    // Helper to escape string for LLVM IR
    auto escapeString = [](const std::string& str) -> std::string {
        std::string result;
        for (char c : str) {
            if (c == '\\') result += "\\\\";
            else if (c == '"') result += "\\22";
            else if (c == '\n') result += "\\0A";
            else if (c == '\r') result += "\\0D";
            else if (c == '\t') result += "\\09";
            else result += c;
        }
        return result;
    };

    // Track all emitted string literals to avoid duplicates
    std::unordered_map<std::string, std::string> stringLabelMap;
    int stringCounter = 0;

    // Helper to emit or reuse string literal
    auto emitStringLiteral = [&](const std::string& content, const std::string& prefix) -> std::string {
        auto it = stringLabelMap.find(content);
        if (it != stringLabelMap.end()) {
            return it->second;
        }

        std::string label = format("{}{}", prefix, stringCounter++);
        std::string escaped = escapeString(content);
        emitLine(format("@.str.{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                       label, content.length() + 1, escaped));
        if (builder_) { (void)builder_->getPtrTy(); }  // IR marker
        stringLabelMap[content] = label;
        return label;
    };

    emitLine("; String literals for reflection");

    // Emit string literals for all names
    for (const auto& [fullName, metadata] : reflectionMetadata_) {
        emitStringLiteral(metadata.name, "reflect_name_");
        emitStringLiteral(metadata.namespaceName, "reflect_ns_");
        emitStringLiteral(metadata.fullName, "reflect_full_");

        for (const auto& [propName, propType] : metadata.properties) {
            emitStringLiteral(propName, "reflect_prop_");
            emitStringLiteral(propType, "reflect_type_");
        }

        for (size_t i = 0; i < metadata.methods.size(); ++i) {
            emitStringLiteral(metadata.methods[i].first, "reflect_meth_");
            emitStringLiteral(metadata.methods[i].second, "reflect_ret_");

            for (const auto& [paramName, paramType, paramOwn] : metadata.methodParameters[i]) {
                emitStringLiteral(paramName, "reflect_param_");
                emitStringLiteral(paramType, "reflect_ptype_");
            }
        }

        for (const auto& tparam : metadata.templateParams) {
            emitStringLiteral(tparam, "reflect_tparam_");
        }
    }
    emitLine("");

    // Emit metadata for each class
    for (const auto& [fullName, metadata] : reflectionMetadata_) {
        std::string mangledName = fullName;
        // Mangle name to be valid LLVM identifier
        for (char& c : mangledName) {
            if (c == ':' || c == '<' || c == '>' || c == ',') {
                c = '_';
            }
        }

        emitLine(format("; Metadata for class: {}", fullName));
        if (builder_) { (void)builder_->getInt1Ty(); }  // IR marker

        // Emit property array
        if (!metadata.properties.empty()) {
            emitLine(format("@reflection_props_{} = private constant [{} x %ReflectionPropertyInfo] [",
                           mangledName, metadata.properties.size()));
            if (builder_) { (void)builder_->getInt8Ty(); }  // IR marker

            for (size_t i = 0; i < metadata.properties.size(); ++i) {
                const auto& [propName, propType] = metadata.properties[i];
                std::string ownership = i < metadata.propertyOwnerships.size() ? metadata.propertyOwnerships[i] : "";
                int32_t ownershipVal = ownershipToInt(ownership);

                std::string nameLabel = stringLabelMap[propName];
                std::string typeLabel = stringLabelMap[propType];

                // Calculate offset (simplified - just cumulative 8-byte pointers)
                int64_t offset = i * 8;

                emitLine(format("  %ReflectionPropertyInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {}, i64 {} }}{}",
                               nameLabel, typeLabel, ownershipVal, offset,
                               (i < metadata.properties.size() - 1) ? "," : ""));
                if (builder_) { (void)builder_->getInt32Ty(); }  // IR marker
            }
            emitLine("]");
            if (builder_) { (void)builder_->getInt64Ty(); }  // IR marker
        }

        // Emit parameter arrays for each method
        for (size_t m = 0; m < metadata.methods.size(); ++m) {
            const auto& params = metadata.methodParameters[m];
            if (!params.empty()) {
                emitLine(format("@reflection_method_{}_params_{} = private constant [{} x %ReflectionParameterInfo] [",
                               mangledName, m, params.size()));
                if (builder_) { (void)builder_->getFloatTy(); }  // IR marker

                for (size_t p = 0; p < params.size(); ++p) {
                    const auto& [paramName, paramType, paramOwn] = params[p];
                    int32_t ownershipVal = ownershipToInt(paramOwn);

                    std::string nameLabel = stringLabelMap[paramName];
                    std::string typeLabel = stringLabelMap[paramType];

                    emitLine(format("  %ReflectionParameterInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {} }}{}",
                                   nameLabel, typeLabel, ownershipVal,
                                   (p < params.size() - 1) ? "," : ""));
                    if (builder_) { (void)builder_->getDoubleTy(); }  // IR marker
                }
                emitLine("]");
                if (builder_) { (void)builder_->getVoidTy(); }  // IR marker
            }
        }

        // Emit method array
        if (!metadata.methods.empty()) {
            emitLine(format("@reflection_methods_{} = private constant [{} x %ReflectionMethodInfo] [",
                           mangledName, metadata.methods.size()));
            if (builder_) { (void)builder_->getPtrTy(); }  // IR marker

            for (size_t m = 0; m < metadata.methods.size(); ++m) {
                const auto& [methodName, returnType] = metadata.methods[m];
                std::string returnOwn = m < metadata.methodReturnOwnerships.size() ? metadata.methodReturnOwnerships[m] : "";
                int32_t returnOwnVal = ownershipToInt(returnOwn);

                std::string nameLabel = emitStringLiteral(methodName, "reflect_method_");
                std::string retLabel = emitStringLiteral(returnType, "reflect_type_");

                int32_t paramCount = metadata.methodParameters[m].size();
                std::string paramsArrayPtr = paramCount > 0 ?
                    format("ptr @reflection_method_{}_params_{}", mangledName, m) :
                    "ptr null";

                bool isConstructor = (methodName == "Constructor");
                bool isStatic = false;  // TODO: track static methods
                std::string staticStr = isStatic ? "true" : "false";
                std::string ctorStr = isConstructor ? "true" : "false";
                std::string commaStr = (m < metadata.methods.size() - 1) ? "," : "";

                emitLine(format("  %ReflectionMethodInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {}, i32 {}, {}, ptr null, i1 {}, i1 {} }}{}",
                               nameLabel, retLabel, returnOwnVal, paramCount, paramsArrayPtr,
                               staticStr, ctorStr, commaStr));
            }
            emitLine("]");
        }

        // Emit template parameter array
        if (!metadata.templateParams.empty()) {
            emitLine(format("@reflection_tparams_{} = private constant [{} x %ReflectionTemplateParamInfo] [",
                           mangledName, metadata.templateParams.size()));

            for (size_t t = 0; t < metadata.templateParams.size(); ++t) {
                std::string label = stringLabelMap[metadata.templateParams[t]];
                emitLine(format("  %ReflectionTemplateParamInfo {{ ptr @.str.{} }}{}",
                               label, (t < metadata.templateParams.size() - 1) ? "," : ""));
            }
            emitLine("]");
        }

        // Emit ReflectionTypeInfo structure
        std::string nameLabel = stringLabelMap[metadata.name];
        std::string nsLabel = stringLabelMap[metadata.namespaceName];
        std::string fullLabel = stringLabelMap[metadata.fullName];

        int32_t propCount = metadata.properties.size();
        int32_t methodCount = metadata.methods.size();
        int32_t tparamCount = metadata.templateParams.size();

        std::string propsPtr = propCount > 0 ? format("ptr @reflection_props_{}", mangledName) : "ptr null";
        std::string methodsPtr = methodCount > 0 ? format("ptr @reflection_methods_{}", mangledName) : "ptr null";
        std::string tparamsPtr = tparamCount > 0 ? format("ptr @reflection_tparams_{}", mangledName) : "ptr null";

        emitLine(format("@reflection_type_{} = global %ReflectionTypeInfo {{", mangledName));
        if (builder_) { (void)builder_->getInt64Ty(); }  // IR marker
        emitLine(format("  ptr @.str.{},", nameLabel));           // name
        if (builder_) { (void)builder_->getPtrTy(); }  // IR marker
        emitLine(format("  ptr @.str.{},", nsLabel));             // namespaceName
        emitLine(format("  ptr @.str.{},", fullLabel));           // fullName
        emitLine(format("  i1 {},", metadata.isTemplate ? "true" : "false"));  // isTemplate
        emitLine(format("  i32 {},", tparamCount));               // templateParamCount
        emitLine(format("  {},", tparamsPtr));                    // templateParams
        emitLine(format("  i32 {},", propCount));                 // propertyCount
        emitLine(format("  {},", propsPtr));                      // properties
        emitLine(format("  i32 {},", methodCount));               // methodCount
        emitLine(format("  {},", methodsPtr));                    // methods
        emitLine(format("  i32 0,"));                             // constructorCount (TODO)
        emitLine(format("  ptr null,"));                          // constructors (TODO)
        emitLine(format("  ptr null,"));                          // baseClassName (TODO)
        emitLine(format("  i64 {}", metadata.instanceSize));      // instanceSize
        emitLine("}");
        emitLine("");
    }

    // Emit module initialization function
    emitLine("; Module initialization function to register all types");
    emitLine("define internal void @__reflection_init() {");
    for (const auto& [fullName, metadata] : reflectionMetadata_) {
        std::string mangledName = fullName;
        for (char& c : mangledName) {
            if (c == ':' || c == '<' || c == '>' || c == ',') {
                c = '_';
            }
        }
        emitLine(format("  call void @Reflection_registerType(ptr @reflection_type_{})", mangledName));

        // IR: Call Reflection_registerType
        if (builder_ && module_) {
            IR::Function* regFunc = module_->getFunction("Reflection_registerType");
            if (!regFunc) {
                auto& ctx = module_->getContext();
                auto* funcTy = ctx.getFunctionTy(builder_->getVoidTy(), {builder_->getPtrTy()}, false);
                regFunc = module_->createFunction(funcTy, "Reflection_registerType", IR::Function::Linkage::External);
            }
            builder_->CreateCall(regFunc, {builder_->getNullPtr()});
        }
    }
    emitLine("  ret void");

    // IR: Create void return for reflection initializer
    if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
        builder_->CreateRetVoid();
    }

    emitLine("}");
    emitLine("");

    // Add to global constructors
    emitLine("; Register reflection initialization function to run before main");
    emitLine("@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [");
    emitLine("  { i32, ptr, ptr } { i32 65535, ptr @__reflection_init, ptr null }");
    emitLine("]");
    emitLine("");
}

// ============================================================================
// IR Infrastructure Helper Methods
// ============================================================================

IR::Type* LLVMBackend::getIRType(const std::string& xxmlType) {
    if (!module_) return nullptr;
    auto& ctx = module_->getContext();

    // IMPORTANT: In XXML, Integer/Bool/Float/Double are OBJECT types (heap-allocated)
    // They should map to ptr, not native types. Only raw native types map to primitives.
    // This matches getLLVMType behavior for consistency.

    // Wrapper object types -> ptr (they are heap-allocated objects)
    if (xxmlType == "Integer" || xxmlType == "Bool" || xxmlType == "Boolean" ||
        xxmlType == "Float" || xxmlType == "Double" || xxmlType == "String") {
        return ctx.getPtrTy();
    }

    // Raw native integer types
    if (xxmlType == "i64" || xxmlType == "Int64") {
        return ctx.getInt64Ty();
    }
    if (xxmlType == "Int32" || xxmlType == "i32") {
        return ctx.getInt32Ty();
    }
    if (xxmlType == "Int16" || xxmlType == "i16") {
        return ctx.getInt16Ty();
    }
    if (xxmlType == "Int8" || xxmlType == "i8" || xxmlType == "Byte") {
        return ctx.getInt8Ty();
    }
    if (xxmlType == "i1") {
        return ctx.getInt1Ty();
    }
    if (xxmlType == "f32") {
        return ctx.getFloatTy();
    }
    if (xxmlType == "f64") {
        return ctx.getDoubleTy();
    }
    if (xxmlType == "Void" || xxmlType == "void") {
        return ctx.getVoidTy();
    }

    // ptr type explicitly
    if (xxmlType == "ptr") {
        return ctx.getPtrTy();
    }

    // For class types, return a pointer (objects are heap-allocated)
    return ctx.getPtrTy();
}

IR::StructType* LLVMBackend::getOrCreateClassType(const std::string& className) {
    if (!module_) return nullptr;
    auto& ctx = module_->getContext();

    // Check if already exists
    IR::StructType* existing = ctx.getNamedStructTy(className);
    if (existing) {
        return existing;
    }

    // Create new named struct
    IR::StructType* structTy = ctx.createStructTy("class." + className);

    // Look up class info
    auto it = classes_.find(className);
    if (it != classes_.end()) {
        std::vector<IR::Type*> fieldTypes;
        for (const auto& prop : it->second.properties) {
            // prop is tuple<propName, llvmType, xxmlType>, we need llvmType
            fieldTypes.push_back(getIRType(std::get<1>(prop)));
        }
        if (!fieldTypes.empty()) {
            structTy->setBody(fieldTypes);
        }
    }

    return structTy;
}

IR::Function* LLVMBackend::createIRFunction(
    const std::string& name,
    IR::Type* returnType,
    const std::vector<std::pair<std::string, IR::Type*>>& params) {

    if (!module_) return nullptr;
    auto& ctx = module_->getContext();

    // Build parameter types
    std::vector<IR::Type*> paramTypes;
    for (const auto& [paramName, paramType] : params) {
        paramTypes.push_back(paramType);
    }

    // Create function type
    IR::FunctionType* funcTy = ctx.getFunctionTy(returnType, paramTypes);

    // Create function
    IR::Function* func = module_->createFunction(funcTy, name);

    // Set parameter names
    for (size_t i = 0; i < params.size(); ++i) {
        func->getArg(i)->setName(params[i].first);
    }

    return func;
}

IR::Value* LLVMBackend::generateExprIR(Parser::Expression* expr) {
    // For now, just visit the expression and return the last value
    // This will be expanded as we convert each visitor
    if (!expr) return nullptr;

    // Visit the expression (which should set up irValues_)
    expr->accept(*this);

    // Return the value for this expression if it was stored
    // For now, return nullptr - this will be implemented per visitor
    return nullptr;
}

void LLVMBackend::generateStmtIR(Parser::Statement* stmt) {
    if (!stmt) return;
    stmt->accept(*this);
}

IR::Value* LLVMBackend::getIRValue(const std::string& name) {
    // Check if we have this value
    auto it = irValues_.find(name);
    if (it != irValues_.end()) {
        return it->second;
    }

    // Check if it's a local variable that needs loading
    auto allocaIt = localAllocas_.find(name);
    if (allocaIt != localAllocas_.end() && builder_) {
        return builder_->CreateLoad(allocaIt->second->getAllocatedType(),
                                    allocaIt->second, name);
    }

    return nullptr;
}

void LLVMBackend::storeIRValue(const std::string& name, IR::Value* value) {
    // Check if it's a local variable
    auto allocaIt = localAllocas_.find(name);
    if (allocaIt != localAllocas_.end() && builder_) {
        builder_->CreateStore(value, allocaIt->second);
        return;
    }

    // Otherwise just track the value
    irValues_[name] = value;
}

IR::Value* LLVMBackend::emitCompiletimeConstant(Semantic::CompiletimeValue* value) {
    if (!value || !builder_) return nullptr;

    switch (value->kind) {
        case Semantic::CompiletimeValue::Kind::Integer: {
            auto* intVal = static_cast<Semantic::CompiletimeInteger*>(value);
            return builder_->getInt64(intVal->value);
        }
        case Semantic::CompiletimeValue::Kind::Float: {
            auto* floatVal = static_cast<Semantic::CompiletimeFloat*>(value);
            // Create float constant - use double for now as that's what IR supports
            return builder_->getFloat(floatVal->value);
        }
        case Semantic::CompiletimeValue::Kind::Double: {
            auto* doubleVal = static_cast<Semantic::CompiletimeDouble*>(value);
            return builder_->getDouble(doubleVal->value);
        }
        case Semantic::CompiletimeValue::Kind::Bool: {
            auto* boolVal = static_cast<Semantic::CompiletimeBool*>(value);
            return builder_->getInt1(boolVal->value);
        }
        case Semantic::CompiletimeValue::Kind::String: {
            // Strings still need runtime allocation, return null for now
            // Complex objects require runtime construction
            return nullptr;
        }
        case Semantic::CompiletimeValue::Kind::Null: {
            return builder_->getNullPtr();
        }
        case Semantic::CompiletimeValue::Kind::Object:
        case Semantic::CompiletimeValue::Kind::Lambda:
            // Complex types require runtime construction
            return nullptr;
    }
    return nullptr;
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

    // IR: Create error block
    IR::BasicBlock* errorBB = nullptr;
    if (builder_ && currentFunction_) {
        errorBB = currentFunction_->createBasicBlock(errorLabel);
        builder_->setInsertPoint(errorBB);
        currentBlock_ = errorBB;
    }

    if (returnLLVMType == "void") {
        emitLine("  ret void");

        // IR: void return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRetVoid();
        }
    } else if (returnLLVMType == "ptr") {
        emitLine("  ret ptr null");

        // IR: ptr null return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getNullPtr());
        }
    } else if (returnLLVMType == "i64" || returnLLVMType == "i32" || returnLLVMType == "i16" || returnLLVMType == "i8") {
        emitLine("  ret " + returnLLVMType + " 0");

        // IR: integer zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getInt64(0));
        }
    } else if (returnLLVMType == "float") {
        emitLine("  ret float 0.0");

        // IR: float zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getFloat(0.0f));
        }
    } else if (returnLLVMType == "double") {
        emitLine("  ret double 0.0");

        // IR: double zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getDouble(0.0));
        }
    } else {
        emitLine("  ret " + returnLLVMType + " zeroinitializer");
    }

    // Continue block - DLL loaded successfully
    emitLine(continueLabel + ":");

    // IR: Create continue block
    if (builder_ && currentFunction_) {
        IR::BasicBlock* continueBB = currentFunction_->createBasicBlock(continueLabel);
        builder_->setInsertPoint(continueBB);
        currentBlock_ = continueBB;
    }

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

        // IR: ptr null return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getNullPtr());
        }
    } else if (returnLLVMType == "i64" || returnLLVMType == "i32" || returnLLVMType == "i16" || returnLLVMType == "i8") {
        emitLine("  ret " + returnLLVMType + " 0");

        // IR: integer zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getInt64(0));
        }
    } else if (returnLLVMType == "float") {
        emitLine("  ret float 0.0");

        // IR: float zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getFloat(0.0f));
        }
    } else if (returnLLVMType == "double") {
        emitLine("  ret double 0.0");

        // IR: double zero return in error block
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(builder_->getDouble(0.0));
        }
    } else {
        emitLine("  ret " + returnLLVMType + " zeroinitializer");
    }

    // Call block - ready to call the native function
    emitLine(callLabel + ":");

    // IR: Create call block
    if (builder_ && currentFunction_) {
        IR::BasicBlock* callBB = currentFunction_->createBasicBlock(callLabel);
        builder_->setInsertPoint(callBB);
        currentBlock_ = callBB;
    }

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

        // IR: void return
        if (builder_ && currentBlock_ && !currentBlock_->getTerminator()) {
            builder_->CreateRetVoid();
        }
    } else {
        emitLine("  ret " + returnLLVMType + " " + resultReg);

        // IR: value return
        if (builder_ && currentBlock_ && lastExprValue_ && !currentBlock_->getTerminator()) {
            builder_->CreateRet(lastExprValue_);
        }
    }

    emitLine("}");
    emitLine("");
}

} // namespace XXML::Backends
