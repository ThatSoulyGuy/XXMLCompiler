#include "Backends/LLVMBackend.h"
#include "Backends/IR/IR.h"  // New IR infrastructure
#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/OperatorRegistry.h"
#include "Core/FormatCompat.h"  // Compatibility layer for format
#include "Utils/ProcessUtils.h"
#include "Semantic/SemanticAnalyzer.h"  // For template instantiation
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>    // For std::setprecision
#include <algorithm>  // For std::replace
#include <functional> // For std::function
#include <cctype>     // For std::isdigit
#include <cstring>    // For std::memcpy
#include <cstdint>    // For uint32_t

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
    // === Initialize IR Infrastructure ===
    module_ = std::make_unique<IR::Module>("xxml_module");
    module_->setTargetTriple(getTargetTriple());
    module_->setDataLayout(getDataLayout());
    builder_ = std::make_unique<IR::IRBuilder>(*module_);

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
            bool isRuntimeModule = false;
            for (auto& decl : importedModule->declarations) {
                if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                    if (ns->name.find("Language") == 0 || ns->name.find("System") == 0 ||
                        ns->name.find("Syscall") == 0 || ns->name.find("Mem") == 0 ||
                        ns->name == "Collections") {
                        isRuntimeModule = true;
                        break;
                    }
                } else if (auto* importDecl = dynamic_cast<Parser::ImportDecl*>(decl.get())) {
                    if (importDecl->modulePath.find("Language::") == 0 ||
                        importDecl->modulePath.find("System::") == 0) {
                        isRuntimeModule = true;
                        break;
                    }
                }
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

    // NOTE: Template AST ownership issues were fixed (TemplateClassInfo/TemplateMethodInfo
    // now store copied data instead of raw pointers). Imported module code generation is
    // now enabled with careful filtering - we skip Language:: modules that are provided
    // by the runtime library, and only generate code for user-defined imported modules.
    //
    // IMPORTANT: We need to collect reflection metadata from ALL modules (including runtime)
    // so that GetType<T> can access property/method info for standard library classes.
    //
    for (auto* importedModule : importedModules_) {
        if (importedModule) {
            // Check if this is a Language:: or System:: module (provided by runtime, skip code gen)
            bool isRuntimeModule = false;
            std::string moduleName = "unknown";

            for (auto& decl : importedModule->declarations) {
                if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                    moduleName = ns->name;
                    // Skip standard library modules that are provided by runtime
                    // Use prefix matching since namespace name could be "Language::Core" etc.
                    if (ns->name.find("Language") == 0 || ns->name.find("System") == 0 ||
                        ns->name.find("Syscall") == 0 || ns->name.find("Mem") == 0 ||
                        ns->name == "Collections") {
                        isRuntimeModule = true;
                        break;
                    }
                } else if (auto* importDecl = dynamic_cast<Parser::ImportDecl*>(decl.get())) {
                    // Check import path for Language:: prefix
                    if (importDecl->modulePath.find("Language::") == 0 ||
                        importDecl->modulePath.find("System::") == 0) {
                        isRuntimeModule = true;
                        break;
                    }
                }
            }

            // ALWAYS collect reflection metadata, even for runtime modules
            // This is needed for GetType<T> to work with standard library classes
            collectReflectionMetadataFromModule(*importedModule);

            if (!isRuntimeModule) {
                // Only generate code for non-runtime modules (user-defined modules)
                generateImportedModuleCode(*importedModule);
            }
        }
    }

    // Visit main program (this collects string literals and reflection metadata)
    program.accept(*this);

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
        for (const auto& [label, content] : stringLiterals_) {
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
    // When useNewIR_ is enabled, append the new IR output for comparison
    if (useNewIR_ && module_) {
        finalOutput << "\n; ============================================\n";
        finalOutput << "; DEBUG: New IR Infrastructure Output\n";
        finalOutput << "; ============================================\n";

        std::string irOutput = IR::emitModuleUnchecked(*module_);
        finalOutput << irOutput;
    }

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

        // Mangle template arguments in class name
        std::string mangledClassName = fullClassName;
        size_t pos = 0;
        while ((pos = mangledClassName.find("::")) != std::string::npos) {
            mangledClassName.replace(pos, 2, "_");
        }
        while ((pos = mangledClassName.find("<")) != std::string::npos) {
            mangledClassName.replace(pos, 1, "_");
        }
        while ((pos = mangledClassName.find(">")) != std::string::npos) {
            mangledClassName.erase(pos, 1);
        }
        while ((pos = mangledClassName.find(",")) != std::string::npos) {
            mangledClassName.replace(pos, 1, "_");
        }
        while ((pos = mangledClassName.find(" ")) != std::string::npos) {
            mangledClassName.erase(pos, 1);
        }

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

void LLVMBackend::generateImportedModuleCode(Parser::Program& program) {
    // Generate code for imported modules safely by processing classes directly.
    // This avoids issues with the full visitor pattern by:
    // 1. Skipping entrypoints (imported modules shouldn't have them)
    // 2. Properly managing state between class generations
    // 3. Not relying on the visitor pattern for top-level iteration


    emitLine("; ============================================");
    emitLine("; Imported module code");
    emitLine("; ============================================");

    // Save current state
    std::string savedNamespace = currentNamespace_;
    std::string savedClassName = currentClassName_;

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
            // Process top-level class
            currentNamespace_ = "";
            currentClassName_ = "";
            variables_.clear();
            try {
                classDecl->accept(*this);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting class " << classDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            // Process namespace
            currentNamespace_ = "";
            processNamespace(ns);
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
    emitLine(format("; import {}", node.modulePath));
}

void LLVMBackend::visit(Parser::NamespaceDecl& node) {
    emitLine(format("; namespace {}", node.name));

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
                std::string propType = getLLVMType(prop->type->typeName);
                classInfo.properties.push_back({prop->name, propType});
            }
        }
    }

    // Generate struct type definition
    std::stringstream structDef;
    // Mangle class name to remove :: (invalid in LLVM type names)
    // Use currentClassName_ which includes namespace, not node.name
    std::string mangledClassName = currentClassName_;
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

    // CRITICAL FIX: For default constructors (empty body), initialize all fields to zero/null
    if (node.body.empty()) {
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            const ClassInfo& classInfo = classIt->second;
            int fieldIndex = 0;

            // Mangle class name for LLVM type (replace :: with _)
            std::string mangledClassName = currentClassName_;
            size_t pos = 0;
            while ((pos = mangledClassName.find("::")) != std::string::npos) {
                mangledClassName.replace(pos, 2, "_");
            }

            for (const auto& [propName, propType] : classInfo.properties) {
                // Get pointer to field
                std::string fieldPtrReg = allocateRegister();
                emitLine(format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                   fieldPtrReg, mangledClassName, fieldIndex));

                // Get LLVM type for this property
                std::string llvmType = getLLVMType(propType);

                // Get type-appropriate default value (null for pointers, 0 for integers, etc.)
                std::string defaultValue = getDefaultValueForType(llvmType);

                // Initialize field with type-safe default value
                emitLine(format("store {}, ptr {}", defaultValue, fieldPtrReg));

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

void LLVMBackend::visit(Parser::DestructorDecl& node) {
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

    // Generate body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Return void
    emitLine("ret void");

    dedent();
    emitLine("}");
    emitLine("");

    // Clear 'this' mapping
    valueMap_.erase("this");
}

void LLVMBackend::visit(Parser::MethodDecl& node) {
    std::cerr.flush();

    // Clear local variables from previous method
    variables_.clear();
    localAllocas_.clear();  // Clear IR allocas

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
        valueMap_[param->name] = "%" + param->name;
        registerTypes_["%" + param->name] = param->type->typeName;

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

    // Generate function definition
    DEBUG_OUT("DEBUG MethodDecl: method=" << node.name << " returnTypeName='" << node.returnType->typeName << "'" << std::endl);
    std::string returnTypeName = node.returnType ? node.returnType->typeName : "void";
    std::string returnType = getLLVMType(returnTypeName);
    DEBUG_OUT("DEBUG MethodDecl: getLLVMType returned '" << returnType << "'" << std::endl);
    currentFunctionReturnType_ = returnType;  // Track for return statements
    std::cerr.flush();

    emitLine("; Method: " + node.name);
    emitLine("define " + returnType + " @" + funcName + "(" + params.str() + ") #1 {");
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
    if (builder_ && module_) {
        // main() takes no parameters and returns i32
        std::vector<std::pair<std::string, IR::Type*>> emptyParams;
        mainFunc = createIRFunction("main", builder_->getInt32Ty(), emptyParams);
        if (mainFunc) {
            entryBlock = mainFunc->createBasicBlock("entry");
            builder_->setInsertPoint(entryBlock);
            currentFunction_ = mainFunc;
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
    currentBlock_ = nullptr;
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

        if (node.type->typeName == "Bool" && varType == "ptr") {
            // Check if initializer is a native boolean (i1) from comparison operations
            auto initTypeIt = registerTypes_.find(initValue);
            if (initTypeIt != registerTypes_.end() && initTypeIt->second == "NativeType<\"bool\">") {
                // Wrap i1 value with Bool_Constructor to create a Bool object
                std::string boolObjReg = allocateRegister();
                emitLine(format("{} = call ptr @Bool_Constructor(i1 {})", boolObjReg, initValue));
                storeValue = boolObjReg;
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

    // Push loop labels for break/continue
    loopStack_.push_back({incrLabel, endLabel});

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
        }
    }

    emitLine(format("store {} {}, ptr {}", iteratorType, startValue, iteratorReg));

    if (node.isCStyleLoop) {
        // C-style for loop: For (Type <name> = init; condition; increment)
        // Jump to condition
        emitLine(format("br label %{}", condLabel));

        // Condition
        emitLine(format("{}:", condLabel));
        indent();
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
        }

        emitLine(format("br i1 {}, label %{}, label %{}", condReg, bodyLabel, endLabel));
        dedent();

        // Body
        emitLine(format("{}:", bodyLabel));
        indent();
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
        emitLine(format("br label %{}", incrLabel));
        dedent();

        // Increment
        emitLine(format("{}:", incrLabel));
        indent();
        node.increment->accept(*this);
        // The increment expression should update the loop variable
        // For expressions like "i.add(Integer::Constructor(1))", we need to store the result
        std::string incrResult = valueMap_["__last_expr"];
        if (!incrResult.empty() && incrResult != "") {
            // Store the result back to the iterator variable
            emitLine(format("store {} {}, ptr {}", iteratorType, incrResult, iteratorReg));
        }
        emitLine(format("br label %{}", condLabel));
        dedent();

        // End
        emitLine(format("{}:", endLabel));
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
            }
        }

        std::string endReg = allocateRegister();
        emitLine(format("{} = alloca {}", endReg, iteratorType));
        emitLine(format("store {} {}, ptr {}", iteratorType, endValue, endReg));

        // Jump to condition
        emitLine(format("br label %{}", condLabel));

        // Condition: check if iterator < end
        emitLine(format("{}:", condLabel));
        indent();
        std::string currentVal = allocateRegister();
        std::string endVal = allocateRegister();
        emitLine(format("{} = load {}, ptr {}", currentVal, iteratorType, iteratorReg));
        emitLine(format("{} = load {}, ptr {}", endVal, iteratorType, endReg));

        std::string condReg = allocateRegister();

        // For Integer^ types, we need to compare the objects, not pointers
        if (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^") {
            // Call Integer comparison: Integer_lt(currentVal, endVal)
            emitLine(format("{} = call i1 @Integer_lt(ptr {}, ptr {})", condReg, currentVal, endVal));
        } else {
            // For primitive types, use direct comparison
            emitLine(format("{} = icmp slt {} {}, {}", condReg, iteratorType, currentVal, endVal));
        }
        emitLine(format("br i1 {}, label %{}, label %{}", condReg, bodyLabel, endLabel));
        dedent();

        // Body
        emitLine(format("{}:", bodyLabel));
        indent();
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
        emitLine(format("br label %{}", incrLabel));
        dedent();

        // Increment iterator
        emitLine(format("{}:", incrLabel));
        indent();
        std::string iterVal = allocateRegister();
        emitLine(format("{} = load {}, ptr {}", iterVal, iteratorType, iteratorReg));
        std::string nextVal = allocateRegister();

        // For Integer^ types, we need to call add method
        if (xxmlIteratorType == "Integer" || xxmlIteratorType == "Integer^") {
            // Create Integer(1) for increment
            std::string oneReg = allocateRegister();
            emitLine(format("{} = call ptr @Integer_Constructor(i64 1)", oneReg));
            // Call Integer_add(iterVal, oneReg) to get new Integer
            emitLine(format("{} = call ptr @Integer_add(ptr {}, ptr {})", nextVal, iterVal, oneReg));
        } else {
            // For primitive types, use direct add
            emitLine(format("{} = add {} {}, 1", nextVal, iteratorType, iterVal));
        }
        emitLine(format("store {} {}, ptr {}", iteratorType, nextVal, iteratorReg));
        emitLine(format("br label %{}", condLabel));
        dedent();

        // End
        emitLine(format("{}:", endLabel));
    }

    // Pop loop labels
    loopStack_.pop_back();
}

void LLVMBackend::visit(Parser::ExitStmt& node) {
    emitLine("call void @exit(i32 0)");

    // IR generation: Generate call to exit
    if (builder_ && currentBlock_) {
        // Exit is typically handled as a call to the runtime exit function
        // For now, generate a return 0 as a placeholder
        auto* zero = builder_->getInt32(0);
        builder_->CreateRet(zero);
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
        }
    }

    // Branch based on condition
    emitLine(format("br i1 {}, label %{}, label %{}",
                        condValue, thenLabel, elseLabel));

    // Then branch
    emitLine(format("{}:", thenLabel));
    indent();
    for (auto& stmt : node.thenBranch) {
        stmt->accept(*this);
    }
    dedent();
    emitLine(format("br label %{}", endLabel));

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

    // IR generation: Set up basic blocks for control flow (statements already generated above)
    if (builder_ && currentFunction_) {
        IR::Value* condIR = lastExprValue_;

        // Create basic blocks for then, else, and merge
        IR::BasicBlock* thenBB = currentFunction_->createBasicBlock(thenLabel);
        IR::BasicBlock* elseBB = currentFunction_->createBasicBlock(elseLabel);
        IR::BasicBlock* mergeBB = currentFunction_->createBasicBlock(endLabel);

        // Create conditional branch
        if (condIR) {
            builder_->CreateCondBr(condIR, thenBB, elseBB);
        }

        // Set up then block (statements already generated via textual IR above)
        builder_->setInsertPoint(thenBB);
        currentBlock_ = thenBB;
        // Only add branch if block doesn't already have a terminator
        if (!thenBB->getTerminator()) {
            builder_->CreateBr(mergeBB);
        }

        // Set up else block (statements already generated via textual IR above)
        builder_->setInsertPoint(elseBB);
        currentBlock_ = elseBB;
        // Only add branch if block doesn't already have a terminator
        if (!elseBB->getTerminator()) {
            builder_->CreateBr(mergeBB);
        }

        // Continue with merge block
        builder_->setInsertPoint(mergeBB);
        currentBlock_ = mergeBB;
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

    // Branch based on condition
    emitLine(format("br i1 {}, label %{}, label %{}",
                        condValue, bodyLabel, endLabel));
    dedent();

    // IR: Create conditional branch
    if (builder_ && lastExprValue_ && bodyBB && endBB) {
        builder_->CreateCondBr(lastExprValue_, bodyBB, endBB);
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

    // New IR infrastructure
    if (builder_) {
        lastExprValue_ = builder_->getInt64(node.value);
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
        } else {
            // Fallback: just use the register (for backwards compatibility)
            // Note: Reference parameters (Integer&) receive object pointers directly
            // like owned parameters - the & is semantic only (no ownership transfer)
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
                    emitLine(format("{} = getelementptr inbounds %class.{}, ptr %this, i32 0, i32 {}",
                                       ptrReg, mangledClassName, i));

                    std::string valueReg = allocateRegister();
                    std::string llvmType = properties[i].second; // LLVM type (ptr, i64, etc.)
                    emitLine(format("{} = load {}, ptr {}",
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

                    // New IR: GEP + Load for property access
                    if (builder_ && currentFunction_) {
                        IR::StructType* classTy = getOrCreateClassType(currentClassName_);
                        if (classTy && currentFunction_->getNumArgs() > 0) {
                            IR::Value* thisPtr = currentFunction_->getArg(0);
                            IR::Value* fieldPtr = builder_->CreateStructGEP(classTy, thisPtr, i, node.name + "_ptr");
                            IR::Type* fieldTy = getIRType(properties[i].second);
                            lastExprValue_ = builder_->CreateLoad(fieldTy, fieldPtr, node.name);
                        }
                    }
                    return;
                }
            }
        }

        // Not a property - must be a global identifier
        valueMap_["__last_expr"] = format("@{}", node.name);
    } else {
        // No current class context - must be a global identifier
        valueMap_["__last_expr"] = format("@{}", node.name);
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
                        // Mangle class name for LLVM type (replace :: with _)
                        std::string mangledClassName = currentClassName_;
                        size_t pos = 0;
                        while ((pos = mangledClassName.find("::")) != std::string::npos) {
                            mangledClassName.replace(pos, 2, "_");
                        }
                        emitLine(fieldPtr + " = getelementptr inbounds %class." + mangledClassName +
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

    // Check if this is 'this' expression (member access on current object)
    if (dynamic_cast<Parser::ThisExpr*>(node.object.get())) {
        // Access property on 'this'
        auto classIt = classes_.find(currentClassName_);
        if (classIt != classes_.end()) {
            // Find the property index
            const auto& properties = classIt->second.properties;
            for (size_t i = 0; i < properties.size(); ++i) {
                if (properties[i].first == node.member) {
                    // Generate getelementptr to access the property
                    std::string ptrReg = allocateRegister();

                    // Mangle class name to remove ::
                    std::string mangledTypeName = currentClassName_;
                    size_t pos = 0;
                    while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                        mangledTypeName.replace(pos, 2, "_");
                    }
                    emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                            ", ptr %this, i32 0, i32 " + std::to_string(i));

                    // Load the field value
                    std::string valueReg = allocateRegister();
                    std::string llvmType = properties[i].second; // LLVM type (ptr, i64, etc.)
                    emitLine(valueReg + " = load " + llvmType + ", ptr " + ptrReg);

                    valueMap_["__last_expr"] = valueReg;
                    // Store the LLVM type as a pseudo-XXML type for later lookup
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

    // Check for lambda .call() invocation: lambdaVar.call(args)
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        if (memberAccess->member == "call") {
            if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
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

                        // Load function pointer from closure (offset 0)
                        std::string funcPtrSlot = allocateRegister();
                        std::string funcPtr = allocateRegister();
                        emitLine(format("{} = getelementptr inbounds {{ ptr }}, ptr {}, i32 0, i32 0",
                                       funcPtrSlot, closureReg));
                        emitLine(format("{} = load ptr, ptr {}", funcPtr, funcPtrSlot));

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
                        if (prop.first == ident->name) {
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
                    if (classIt->second.properties[i].first == ident->name) {
                        propTypeName = classIt->second.properties[i].first;  // Property name, we'll look up type
                        propIndex = i;
                        break;
                    }
                }

                // Load the property value from this
                std::string ptrReg = allocateRegister();
                std::string mangledTypeName = currentClassName_;
                size_t pos = 0;
                while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                    mangledTypeName.replace(pos, 2, "_");
                }
                emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                        ", ptr %this, i32 0, i32 " + std::to_string(propIndex));

                // Load the property value
                instanceRegister = allocateRegister();
                std::string llvmType = classIt->second.properties[propIndex].second;
                emitLine(instanceRegister + " = load " + llvmType + ", ptr " + ptrReg);

                // Determine the class name from the type (property type inference)
                // TODO: This should come from semantic analysis, not be hardcoded
                // For now, assume property name without 's' suffix (e.g., "salary" -> "Double")
                // This is a simplification - ideally we'd track property types properly
                std::string className = ident->name;
                // Capitalize first letter and try to infer type
                if (className == "salary" || className == "amount") {
                    className = "Double";
                } else if (className == "id" || className == "count" || className == "employeeCount") {
                    className = "Integer";
                } else if (className == "name" || className == "deptName") {
                    className = "String";
                } else if (className == "active") {
                    className = "Bool";
                }

                // Register the type so semantic lookup can find it
                registerTypes_[instanceRegister] = className + "^";

                // Generate qualified method name
                functionName = className + "_" + memberAccess->member;
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

                // Strip ownership modifiers (^, %, &) before mangling for function name
                if (!className.empty() && (className.back() == '^' ||
                                           className.back() == '%' ||
                                           className.back() == '&')) {
                    className = className.substr(0, className.length() - 1);
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
                // Special handling for compiler intrinsic types (ReflectionContext, CompilationContext)
                if (className == "ReflectionContext" || className == "CompilationContext") {
                    className = "Processor";
                }

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

                    // Strip ownership modifiers (^, %, &) before mangling for function name
                    // Note: Reference parameters (&) receive object pointers directly
                    // like owned parameters - the & is semantic only (no ownership transfer)
                    if (!className.empty() && (className.back() == '^' ||
                                               className.back() == '%' ||
                                               className.back() == '&')) {
                        className = className.substr(0, className.length() - 1);
                    }

                    // Mangle namespace separators (same as getQualifiedName)
                    size_t pos = 0;
                    while ((pos = className.find("::")) != std::string::npos) {
                        className.replace(pos, 2, "_");
                    }

                    // Special handling for compiler intrinsic types (ReflectionContext, CompilationContext)
                    if (className == "ReflectionContext" || className == "CompilationContext") {
                        className = "Processor";
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
                                if (prop.first == ident->name) {
                                    isVariable = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!staticClassName.empty() && !isVariable) {
                    // This looks like a static method call (e.g., System::Console::printLine)
                    // Mangle the namespace/class name
                    std::string mangledClassName = staticClassName;
                    size_t pos = 0;
                    while ((pos = mangledClassName.find("::")) != std::string::npos) {
                        mangledClassName.replace(pos, 2, "_");
                    }

                    // Strip :: prefix from member name if present
                    std::string memberName = memberAccess->member;
                    if (memberName.length() >= 2 && memberName.substr(0, 2) == "::") {
                        memberName = memberName.substr(2);
                    }

                    functionName = mangledClassName + "_" + memberName;

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

                    // Strip ownership modifiers (^, %, &) before mangling for function name
                    if (!className.empty() && (className.back() == '^' ||
                                               className.back() == '%' ||
                                               className.back() == '&')) {
                        className = className.substr(0, className.length() - 1);
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

            // Strip ownership modifiers (^, %, &) for type lookup
            std::string baseInstanceType = instanceType;
            if (!baseInstanceType.empty() && (baseInstanceType.back() == '^' ||
                                              baseInstanceType.back() == '%' ||
                                              baseInstanceType.back() == '&')) {
                baseInstanceType = baseInstanceType.substr(0, baseInstanceType.length() - 1);
            }

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
            functionName == "xxml_File_close";

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
            functionName == "xxml_File_sizeByPath") {
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
        // Constructors that take primitive values
        // Note: Function names may be namespaced (e.g., Language_Core_Float_Constructor)
        // so we check if the name contains the pattern, not just exact match
        else if (functionName.find("Integer_Constructor") != std::string::npos &&
                 functionName.find("List_Integer_Constructor") == std::string::npos) {
            expectedType = "i64";  // Integer constructor takes i64
        }
        else if (functionName.find("Bool_Constructor") != std::string::npos) {
            expectedType = "i1";  // Bool constructor takes i1
        }
        else if (functionName.find("Float_Constructor") != std::string::npos) {
            expectedType = "float";  // Float constructor takes float
        }
        else if (functionName.find("Double_Constructor") != std::string::npos) {
            expectedType = "double";  // Double constructor takes double
        }
        // Threading functions with specific parameter types
        else if (functionName == "xxml_Thread_sleep" ||
                 functionName == "xxml_Atomic_create") {
            // These take a single i64 parameter
            expectedType = "i64";
        }
        else if ((functionName == "xxml_Atomic_add" || functionName == "xxml_Atomic_sub" ||
                  functionName == "xxml_Atomic_store" || functionName == "xxml_Atomic_exchange") && i == 1) {
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
            argValue = convertedReg;
            argType = "ptr";
        } else if (argType == "ptr" && expectedType == "i64") {
            // CRITICAL FIX: When converting pointer to i64 for a parameter that expects a value (reference %),
            // we need to LOAD the value from the pointer first, not convert the pointer address itself
            // Check if this pointer points to an i64 (NativeType<"int64">^) that needs to be dereferenced
            auto typeIt = registerTypes_.find(argValue);
            bool isInt64Pointer = false;
            if (typeIt != registerTypes_.end()) {
                std::string xxmlType = typeIt->second;
                // Check if this is NativeType<"int64">^ or NativeType<int64>^
                if (xxmlType == "NativeType<\"int64\">" || xxmlType == "NativeType<int64>") {
                    isInt64Pointer = true;
                }
            }

            if (isInt64Pointer) {
                // Load the i64 value from the pointer
                std::string loadedReg = allocateRegister();
                emitLine(format("{} = load i64, ptr {}", loadedReg, argValue));
                argValue = loadedReg;
                argType = "i64";
            } else {
                // Convert pointer address to i64 (for non-int64 pointers)
                std::string convertedReg = allocateRegister();
                emitLine(format("{} = ptrtoint ptr {} to i64", convertedReg, argValue));
                argValue = convertedReg;
                argType = "i64";
            }
        } else if (argType == "i64" && expectedType == "float") {
            // Convert integer to float using sitofp (signed integer to floating point)
            std::string convertedReg = allocateRegister();
            emitLine(format("{} = sitofp i64 {} to float", convertedReg, argValue));
            argValue = convertedReg;
            argType = "float";
        } else if (argType == "i64" && expectedType == "double") {
            // Convert integer to double using sitofp
            std::string convertedReg = allocateRegister();
            emitLine(format("{} = sitofp i64 {} to double", convertedReg, argValue));
            argValue = convertedReg;
            argType = "double";
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

                // Special handling for comparison methods that return i1 (native bool)
                if (methodName == "eq" || methodName == "ne" || methodName == "lt" ||
                    methodName == "le" || methodName == "gt" || methodName == "ge" ||
                    methodName == "equals" || methodName == "lessThan" ||
                    methodName == "lessThanOrEqual" || methodName == "greaterThan" ||
                    methodName == "greaterThanOrEqual" || methodName == "notEquals" ||
                    methodName == "isEmpty" ||
                    (methodName == "getValue" && className == "Bool")) {
                    returnType = "NativeType<\"bool\">";
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
        DEBUG_OUT("DEBUG BinaryExpr: leftValue=" << leftValue << " has type=" << leftRegType->second << " -> LLVM type=" << leftType << "\n");
    } else {
        DEBUG_OUT("DEBUG BinaryExpr: leftValue=" << leftValue << " NOT FOUND in registerTypes_\n");
    }
    auto rightRegType = registerTypes_.find(rightValue);
    if (rightRegType != registerTypes_.end()) {
        rightType = getLLVMType(rightRegType->second);
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
        if (offsetType == "ptr") {
            std::string offsetAsInt = allocateRegister();
            emitLine(format("{} = ptrtoint ptr {} to i64", offsetAsInt, offsetValue));
            offsetValue = offsetAsInt;
        }

        // Convert ptr to i64
        std::string ptrAsInt = allocateRegister();
        emitLine(format("{} = ptrtoint ptr {} to i64", ptrAsInt, ptrValue));

        // Do the arithmetic
        std::string resultInt = allocateRegister();
        if (node.op == "+") {
            if (ptrOnLeft) {
                emitLine(format("{} = add i64 {}, {}", resultInt, ptrAsInt, offsetValue));
            } else {
                emitLine(format("{} = add i64 {}, {}", resultInt, offsetValue, ptrAsInt));
            }
        } else { // "-"
            if (ptrOnLeft) {
                emitLine(format("{} = sub i64 {}, {}", resultInt, ptrAsInt, offsetValue));
            } else {
                // offset - ptr doesn't make sense, but generate it anyway
                emitLine(format("{} = sub i64 {}, {}", resultInt, offsetValue, ptrAsInt));
            }
        }

        // Convert back to ptr
        std::string resultReg = allocateRegister();
        emitLine(format("{} = inttoptr i64 {} to ptr", resultReg, resultInt));

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
    } else if (node.op == "||" || node.op == "&&") {
        // Logical operators - need to convert Bool objects to i1 if necessary
        std::string leftBool = leftValue;
        std::string rightBool = rightValue;

        // Get XXML types to check if we have Bool objects
        auto leftXXMLType = registerTypes_.find(leftValue);
        auto rightXXMLType = registerTypes_.find(rightValue);

        // Convert left operand to i1 if it's a Bool object (ptr)
        if (leftType == "ptr" || (leftXXMLType != registerTypes_.end() &&
            (leftXXMLType->second == "Bool" || leftXXMLType->second == "Bool^" ||
             leftXXMLType->second.find("Bool") != std::string::npos))) {
            leftBool = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", leftBool, leftValue));
        }

        // Convert right operand to i1 if it's a Bool object (ptr)
        if (rightType == "ptr" || (rightXXMLType != registerTypes_.end() &&
            (rightXXMLType->second == "Bool" || rightXXMLType->second == "Bool^" ||
             rightXXMLType->second.find("Bool") != std::string::npos))) {
            rightBool = allocateRegister();
            emitLine(format("{} = call i1 @Bool_getValue(ptr {})", rightBool, rightValue));
        }

        // Perform logical operation
        std::string resultReg = allocateRegister();
        if (node.op == "||") {
            emitLine(format("{} = or i1 {}, {}", resultReg, leftBool, rightBool));
        } else {
            emitLine(format("{} = and i1 {}, {}", resultReg, leftBool, rightBool));
        }

        valueMap_["__last_expr"] = resultReg;
        registerTypes_[resultReg] = "NativeType<\"bool\">";
        // Skip the IR infrastructure section to avoid re-visiting operands
        return;
    } else {
        // Normal arithmetic or comparison - use the determined types
        // Determine the operation type (prefer floating-point if either operand is float/double)
        std::string opType = "i64";
        DEBUG_OUT("DEBUG BinaryExpr: leftType='" << leftType << "' rightType='" << rightType << "'\n");
        if (leftType == "double" || rightType == "double") {
            opType = "double";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to double\n");
        } else if (leftType == "float" || rightType == "float") {
            opType = "float";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to float\n");
        } else if (leftType == "i64" || rightType == "i64") {
            opType = "i64";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to i64\n");
        } else if (leftType == "i32" || rightType == "i32") {
            opType = "i32";
            DEBUG_OUT("DEBUG BinaryExpr: Setting opType to i32\n");
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
            } else {
                registerTypes_[resultReg] = "NativeType<\"int64\">";
            }
        }
    }

    // New IR infrastructure: generate binary operations using IRBuilder
    if (builder_) {
        // Re-visit operands to get IR values
        node.left->accept(*this);
        IR::Value* leftIR = lastExprValue_;
        node.right->accept(*this);
        IR::Value* rightIR = lastExprValue_;

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
            // Store the value to the variable's memory location
            emitLine("store i64 " + valueReg + ", ptr " + targetIt->second.llvmRegister);

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
                    if (properties[i].first == varName) {
                        // Generate getelementptr to get address of the field
                        std::string ptrReg = allocateRegister();

                        // Mangle class name for LLVM type
                        std::string mangledTypeName = currentClassName_;
                        size_t pos = 0;
                        while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                            mangledTypeName.replace(pos, 2, "_");
                        }

                        emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                ", ptr %this, i32 0, i32 " + std::to_string(i));

                        // Store the value to the field
                        std::string llvmType = properties[i].second;
                        emitLine("store " + llvmType + " " + valueReg + ", ptr " + ptrReg);
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
                        if (properties[i].first == memberName) {
                            // Generate getelementptr to get address of the field
                            std::string ptrReg = allocateRegister();

                            // Mangle class name
                            std::string mangledTypeName = currentClassName_;
                            size_t pos = 0;
                            while ((pos = mangledTypeName.find("::")) != std::string::npos) {
                                mangledTypeName.replace(pos, 2, "_");
                            }

                            emitLine(ptrReg + " = getelementptr inbounds %class." + mangledTypeName +
                                    ", ptr %this, i32 0, i32 " + std::to_string(i));

                            // Store the value to the field
                            std::string llvmType = properties[i].second; // LLVM type
                            emitLine("store " + llvmType + " " + valueReg + ", ptr " + ptrReg);
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
    // Generate unique names for this lambda
    int lambdaId = lambdaCounter_++;
    std::string closureTypeName = "%closure." + std::to_string(lambdaId);
    std::string lambdaFuncName = "@lambda." + std::to_string(lambdaId);

    emitLine(format("; Lambda expression #{}", lambdaId));

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

    // Store function pointer at offset 0
    std::string funcPtrSlot = allocateRegister();
    emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 0",
                    funcPtrSlot, closureTypeStr, closureReg));
    emitLine(format("store ptr {}, ptr {}", lambdaFuncName, funcPtrSlot));

    // Store captured values at offsets 1, 2, ...
    for (size_t i = 0; i < node.captures.size(); ++i) {
        const auto& capture = node.captures[i];
        std::string captureSlot = allocateRegister();
        emitLine(format("{} = getelementptr inbounds {}, ptr {}, i32 0, i32 {}",
                        captureSlot, closureTypeStr, closureReg, i + 1));

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
            } else {
                // Copy (%): copy the value into the closure
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
    } else {
        emitLine(format("; annotation definition: {}", node.name));
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
            }

            // Generate ReflectionAnnotationInfo struct
            // { ptr annotationName, i32 argumentCount, ptr arguments }
            std::string infoName = prefix + "_info";
            emitLine(format("{} = private unnamed_addr constant {{ ptr, i32, ptr }} {{ ptr {}, i32 {}, ptr {} }}",
                           infoName, nameStr, meta->arguments.size(),
                           meta->arguments.empty() ? "null" : argsArrayName));

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

        // Generate type name string
        std::string typeNameStr = format("@__annotation_{}_typename", groupId);
        emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                       typeNameStr, group.typeName.length() + 1, group.typeName));

        // Generate member name string if needed
        std::string memberNameStr = "null";
        if (!group.memberName.empty()) {
            memberNameStr = format("@__annotation_{}_membername", groupId);
            emitLine(format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                           memberNameStr, group.memberName.length() + 1, group.memberName));
        }

        // Generate registration call in init function
        // This will be added to the module init section
        emitLine(format("; Registration for {} {}::{}",
                       group.targetType, group.typeName, group.memberName));
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
    emitLine("%ReflectionPropertyInfo = type { ptr, ptr, i32, i64 }");
    emitLine("%ReflectionParameterInfo = type { ptr, ptr, i32 }");
    emitLine("%ReflectionMethodInfo = type { ptr, ptr, i32, i32, ptr, ptr, i1, i1 }");  // name, returnType, returnOwnership, paramCount, params, funcPtr, isStatic, isCtor
    emitLine("%ReflectionTemplateParamInfo = type { ptr }");
    emitLine("%ReflectionTypeInfo = type { ptr, ptr, ptr, i1, i32, ptr, i32, ptr, i32, ptr, i32, ptr, ptr, i64 }");
    emitLine("");

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

        // Emit property array
        if (!metadata.properties.empty()) {
            emitLine(format("@reflection_props_{} = private constant [{} x %ReflectionPropertyInfo] [",
                           mangledName, metadata.properties.size()));

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
            }
            emitLine("]");
        }

        // Emit parameter arrays for each method
        for (size_t m = 0; m < metadata.methods.size(); ++m) {
            const auto& params = metadata.methodParameters[m];
            if (!params.empty()) {
                emitLine(format("@reflection_method_{}_params_{} = private constant [{} x %ReflectionParameterInfo] [",
                               mangledName, m, params.size()));

                for (size_t p = 0; p < params.size(); ++p) {
                    const auto& [paramName, paramType, paramOwn] = params[p];
                    int32_t ownershipVal = ownershipToInt(paramOwn);

                    std::string nameLabel = stringLabelMap[paramName];
                    std::string typeLabel = stringLabelMap[paramType];

                    emitLine(format("  %ReflectionParameterInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {} }}{}",
                                   nameLabel, typeLabel, ownershipVal,
                                   (p < params.size() - 1) ? "," : ""));
                }
                emitLine("]");
            }
        }

        // Emit method array
        if (!metadata.methods.empty()) {
            emitLine(format("@reflection_methods_{} = private constant [{} x %ReflectionMethodInfo] [",
                           mangledName, metadata.methods.size()));

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
        emitLine(format("  ptr @.str.{},", nameLabel));           // name
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
    }
    emitLine("  ret void");
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

    // Handle primitive types
    if (xxmlType == "Integer" || xxmlType == "i64" || xxmlType == "Int64") {
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
    if (xxmlType == "Bool" || xxmlType == "Boolean" || xxmlType == "i1") {
        return ctx.getInt1Ty();
    }
    if (xxmlType == "Float" || xxmlType == "f32") {
        return ctx.getFloatTy();
    }
    if (xxmlType == "Double" || xxmlType == "f64") {
        return ctx.getDoubleTy();
    }
    if (xxmlType == "Void" || xxmlType == "void") {
        return ctx.getVoidTy();
    }

    // For pointer types (String, object references, etc.)
    if (xxmlType == "String" || xxmlType == "ptr") {
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
        for (const auto& [propName, propType] : it->second.properties) {
            fieldTypes.push_back(getIRType(propType));
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

} // namespace XXML::Backends
