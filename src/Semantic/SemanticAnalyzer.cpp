#include "../../include/Semantic/SemanticAnalyzer.h"
#include "../../include/Semantic/TypeCanonicalizer.h"
#include "../../include/Semantic/TemplateExpander.h"
#include "../../include/Semantic/OwnershipAnalyzer.h"
#include "../../include/Semantic/LayoutComputer.h"
#include "../../include/Semantic/ABILowering.h"
#include "../../include/Semantic/SemanticVerifier.h"
#include "../../include/Semantic/SemanticError.h"
#include "../../include/Core/CompilationContext.h"
#include "../../include/Core/TypeRegistry.h"
#include "../../include/Core/FormatCompat.h"
#include "../../include/Common/Debug.h"
#include <iostream>

namespace XXML {
namespace Semantic {

// ✅ REMOVED STATIC MEMBERS - now instance-based

// New constructor with CompilationContext
SemanticAnalyzer::SemanticAnalyzer(Core::CompilationContext& context, Common::ErrorReporter& reporter)
    : symbolTable_(&context.symbolTable()),
      context_(&context),
      errorReporter(reporter),
      annotationProcessor_(reporter),
      currentClass(""),
      currentNamespace(""),
      enableValidation(true),
      inTemplateDefinition(false) {
    // Pre-populate symbol table with built-in types from registry
    const auto& typeRegistry = context.types();

    for (const auto& typeName : typeRegistry.getAllRegisteredTypes()) {
        if (typeRegistry.getTypeInfo(typeName)->isBuiltin) {
            auto sym = std::make_unique<Symbol>(
                typeName, SymbolKind::Class, typeName,
                Parser::OwnershipType::Owned, Common::SourceLocation()
            );
            symbolTable_->define(typeName, std::move(sym));
        }
    }

    // Register built-in @NativeFunction annotation for FFI
    AnnotationInfo nativeFuncAnnotation;
    nativeFuncAnnotation.name = "NativeFunction";
    nativeFuncAnnotation.allowedTargets = {Parser::AnnotationTarget::Methods};
    nativeFuncAnnotation.parameters = {
        {"path", "String", Parser::OwnershipType::Owned, false},
        {"name", "String", Parser::OwnershipType::Owned, false},
        {"convention", "String", Parser::OwnershipType::Owned, true}  // Optional, defaults to "*"
    };
    nativeFuncAnnotation.retainAtRuntime = false;
    nativeFuncAnnotation.astNode = nullptr;  // Built-in, no AST node
    annotationRegistry_["NativeFunction"] = nativeFuncAnnotation;

    // Initialize intrinsic method return types for Console, Mem, Syscall
    // These methods bypass normal validation but still need type information

    // Console methods (System::Console and Console namespaces)
    intrinsicMethods_["System::Console::print"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["System::Console::printLine"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["System::Console::printInteger"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["System::Console::printIntegerLine"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["System::Console::readLine"] = {"String", Parser::OwnershipType::Owned};
    intrinsicMethods_["System::Console::readInt"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["System::Console::getTime"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["System::Console::getTimeMillis"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["System::Console::exit"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Console::print"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Console::printLine"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Console::printInteger"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Console::printIntegerLine"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Console::readLine"] = {"String", Parser::OwnershipType::Owned};
    intrinsicMethods_["Console::readInt"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Console::getTime"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Console::getTimeMillis"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Console::exit"] = {"None", Parser::OwnershipType::None};

    // Mem methods
    intrinsicMethods_["Mem::alloc"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Mem::free"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Mem::move"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Mem::copy"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Language::Core::Mem::alloc"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Language::Core::Mem::free"] = {"None", Parser::OwnershipType::None};

    // Syscall - memory operations
    intrinsicMethods_["Syscall::call"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Language::Core::Syscall::call"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::malloc"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::free"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::memcpy"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::ptr_read"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::ptr_write"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::int64_read"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::int64_write"] = {"None", Parser::OwnershipType::None};

    // Syscall - string operations
    intrinsicMethods_["Syscall::string_create"] = {"NativeType<\"string_ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_cstr"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_length"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_concat"] = {"NativeType<\"string_ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_copy"] = {"NativeType<\"string_ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_equals"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_hash"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_charAt"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::string_destroy"] = {"None", Parser::OwnershipType::None};

    // Syscall - type conversion
    intrinsicMethods_["Syscall::double_to_string"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::float_to_string"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::int_to_string"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};

    // Syscall - reflection
    intrinsicMethods_["Syscall::reflection_type_getName"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getFullName"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getNamespace"] = {"NativeType<\"cstr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_isTemplate"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getTemplateParamCount"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getPropertyCount"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getProperty"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getPropertyByName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getMethodCount"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::reflection_type_getMethod"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};

    // Syscall - annotation processor argument access
    intrinsicMethods_["Syscall::Processor_argGetName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_argAsInt"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_argAsString"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_argAsBool"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_argAsDouble"] = {"NativeType<\"double\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getArgCount"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getArgAt"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getArg"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};

    // Syscall - annotation processor reflection context
    intrinsicMethods_["Syscall::Processor_getTargetKind"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getTargetName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getTypeName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getClassName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getNamespaceName"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getSourceFile"] = {"NativeType<\"ptr\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getLineNumber"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};
    intrinsicMethods_["Syscall::Processor_getColumnNumber"] = {"NativeType<\"int64\">", Parser::OwnershipType::Owned};

    // Syscall - annotation processor compilation context (diagnostics)
    intrinsicMethods_["Syscall::Processor_message"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::Processor_warning"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::Processor_warningAt"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::Processor_error"] = {"None", Parser::OwnershipType::None};
    intrinsicMethods_["Syscall::Processor_errorAt"] = {"None", Parser::OwnershipType::None};

    // String constructors and methods
    intrinsicMethods_["String::Constructor"] = {"String", Parser::OwnershipType::Owned};
    intrinsicMethods_["Language::Core::String::Constructor"] = {"String", Parser::OwnershipType::Owned};
    intrinsicMethods_["String::length"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["String::concat"] = {"String", Parser::OwnershipType::Owned};
    intrinsicMethods_["String::substring"] = {"String", Parser::OwnershipType::Owned};

    // Integer constructors
    intrinsicMethods_["Integer::Constructor"] = {"Integer", Parser::OwnershipType::Owned};
    intrinsicMethods_["Language::Core::Integer::Constructor"] = {"Integer", Parser::OwnershipType::Owned};
}

// Legacy constructor (deprecated but kept for compatibility)
SemanticAnalyzer::SemanticAnalyzer(Common::ErrorReporter& reporter)
    : symbolTable_(nullptr),
      context_(nullptr),
      errorReporter(reporter),
      annotationProcessor_(reporter),
      currentClass(""),
      currentNamespace(""),
      enableValidation(true),
      inTemplateDefinition(false) {
    // WARNING: This constructor doesn't have access to CompilationContext
    // Should only be used for testing or transitional code
    std::cerr << "Warning: SemanticAnalyzer created without CompilationContext\n";
}

void SemanticAnalyzer::analyze(Parser::Program& program) {
    program.accept(*this);
}

//==============================================================================
// MULTI-STAGE ANALYSIS PIPELINE
//==============================================================================

CompilationPassResults SemanticAnalyzer::runPipeline(Parser::Program& program) {
    passResults_ = CompilationPassResults{};

    //==========================================================================
    // STAGE 1: TYPE RESOLUTION (TypeCanonicalizer)
    //==========================================================================
    TypeCanonicalizer typeCanonicalizer(errorReporter, validNamespaces_);
    passResults_.typeResolution = typeCanonicalizer.run(program);

    // Copy resolved types and namespaces for subsequent passes
    for (const auto& ns : passResults_.typeResolution.validNamespaces) {
        validNamespaces_.insert(ns);
    }

    // Continue even if there are unresolved types - they may be cross-module refs

    //==========================================================================
    // STAGE 2 & 3: SEMANTIC ANALYSIS (Two-Pass)
    //==========================================================================
    // First pass: collect declarations (validation disabled)
    nativeMethods_.clear();  // Clear native methods list for fresh collection
    setValidationEnabled(false);
    analyze(program);

    // Second pass: full validation (validation enabled)
    setValidationEnabled(true);
    resetMovedVariables();
    analyze(program);

    // Populate semantic validation result
    for (const auto& [expr, type] : expressionTypes) {
        ResolvedExprType resolved;
        resolved.typeName = type;
        resolved.ownership = getExpressionOwnership(const_cast<Parser::Expression*>(expr));
        passResults_.semanticValidation.expressionTypes[const_cast<Parser::Expression*>(expr)] = resolved;
    }
    passResults_.semanticValidation.success = !errorReporter.hasErrors();

    // If semantic analysis failed, return early
    if (errorReporter.hasErrors()) {
        return passResults_;
    }

    //==========================================================================
    // STAGE 4: TEMPLATE EXPANSION (TemplateExpander)
    //==========================================================================
    // Convert internal instantiation requests to pass format
    std::set<TemplateExpander::InstantiationRequest> classInstRequests;
    for (const auto& inst : templateInstantiations) {
        TemplateExpander::InstantiationRequest req;
        req.templateName = inst.templateName;
        req.arguments = inst.arguments;
        classInstRequests.insert(req);
    }

    std::set<TemplateExpander::MethodInstantiationRequest> methodInstRequests;
    for (const auto& inst : methodTemplateInstantiations) {
        TemplateExpander::MethodInstantiationRequest req;
        req.className = inst.className;
        req.instantiatedClassName = inst.instantiatedClassName;
        req.methodName = inst.methodName;
        req.arguments = inst.arguments;
        methodInstRequests.insert(req);
    }

    std::set<TemplateExpander::LambdaInstantiationRequest> lambdaInstRequests;
    for (const auto& inst : lambdaTemplateInstantiations_) {
        TemplateExpander::LambdaInstantiationRequest req;
        req.variableName = inst.variableName;
        req.arguments = inst.arguments;
        lambdaInstRequests.insert(req);
    }

    TemplateExpander templateExpander(errorReporter, passResults_.typeResolution);
    passResults_.templateExpansion = templateExpander.run(
        classInstRequests,
        methodInstRequests,
        lambdaInstRequests,
        templateClasses,
        templateMethods,
        templateLambdas_
    );

    //==========================================================================
    // STAGE 5: OWNERSHIP ANALYSIS (OwnershipAnalyzer)
    //==========================================================================
    OwnershipAnalyzer ownershipAnalyzer(errorReporter);
    passResults_.ownershipAnalysis = ownershipAnalyzer.run(program, passResults_.semanticValidation);

    //==========================================================================
    // STAGE 6: LAYOUT COMPUTATION (LayoutComputer)
    //==========================================================================
    LayoutComputer layoutComputer(errorReporter, passResults_.typeResolution, passResults_.semanticValidation);
    passResults_.layoutComputation = layoutComputer.run(classRegistry_);

    //==========================================================================
    // STAGE 7: ABI LOWERING (ABILowering)
    //==========================================================================
    // Determine target platform
    ABILowering::TargetPlatform platform = ABILowering::TargetPlatform::Windows_x64;
#ifdef __linux__
    platform = ABILowering::TargetPlatform::Linux_x64;
#elif defined(__APPLE__)
    #if defined(__arm64__) || defined(__aarch64__)
    platform = ABILowering::TargetPlatform::macOS_ARM64;
    #else
    platform = ABILowering::TargetPlatform::macOS_x64;
    #endif
#endif

    ABILowering abiLowering(errorReporter, passResults_.typeResolution,
                            passResults_.layoutComputation, platform);
    passResults_.abiLowering = abiLowering.run(nativeMethods_, callbackTypeRegistry_);

    return passResults_;
}

// Type checking helpers
bool SemanticAnalyzer::isCompatibleType(const std::string& expected, const std::string& actual) {
    // Exact match
    if (expected == actual) return true;

    // None is compatible with everything (void/null)
    if (expected == "None" || actual == "None") return true;

    // __DynamicValue is compatible with anything (used for processor target values)
    if (expected == "__DynamicValue" || actual == "__DynamicValue") return true;

    // NativeType has special compatibility rules - check these FIRST before general template logic

    // NativeType<X> is compatible with NativeType<Y> for any X, Y (used for type conversions and bitwise operations)
    if (expected.find("NativeType<") != std::string::npos && actual.find("NativeType<") != std::string::npos) {
        return true;
    }

    // NativeType<int64> accepts Integer
    if (expected.find("NativeType<int64>") != std::string::npos && actual == "Integer") return true;
    if (expected.find("NativeType<bool>") != std::string::npos && actual == "Bool") return true;
    if (expected.find("NativeType<float>") != std::string::npos && actual == "Float") return true;
    if (expected.find("NativeType<double>") != std::string::npos && actual == "Double") return true;
    if (expected.find("NativeType<string_ptr>") != std::string::npos && actual == "String") return true;
    if (expected.find("NativeType<cstr>") != std::string::npos && actual == "String") return true;
    if (expected.find("NativeType<ptr>") != std::string::npos) return true; // NativeType<ptr> accepts any pointer

    // Pointer types accept Integer for null pointer initialization (0)
    if ((expected.find("NativeType<string_ptr>") != std::string::npos ||
         expected.find("NativeType<ptr>") != std::string::npos ||
         expected.find("NativeType<cstr>") != std::string::npos) && actual == "Integer") {
        return true;
    }

    // Allow implicit conversions for NativeType<float> and NativeType<double>
    if (expected.find("NativeType<float>") != std::string::npos && actual == "Integer") return true; // int to float conversion
    if (expected.find("NativeType<double>") != std::string::npos && actual == "Integer") return true; // int to double conversion

    // Reverse: wrapper types can be used where NativeType is expected (implicit conversion)
    if (actual.find("NativeType<int64>") != std::string::npos && expected == "Integer") return true;
    if (actual.find("NativeType<bool>") != std::string::npos && expected == "Bool") return true;
    if (actual.find("NativeType<ptr>") != std::string::npos) return true; // NativeType<ptr> is compatible with any pointer

    // Normalize type names for comparison (handle namespace variations)
    // e.g., "Math::Vector2<Float>" and "Math_Vector2_Float" should be compared properly
    auto normalizeType = [](const std::string& type) -> std::string {
        std::string normalized = type;
        // Replace :: with _ for namespace normalization
        size_t pos = 0;
        while ((pos = normalized.find("::")) != std::string::npos) {
            normalized.replace(pos, 2, "_");
        }
        return normalized;
    };

    std::string normalizedExpected = normalizeType(expected);
    std::string normalizedActual = normalizeType(actual);

    // Check if types match after normalization
    if (normalizedExpected == normalizedActual) return true;

    // Extract base type and template arguments for comparison
    // Skip NativeType since we already handled it above
    auto extractTemplateInfo = [](const std::string& type) -> std::pair<std::string, std::string> {
        // Skip NativeType - it has special compatibility rules
        if (type.find("NativeType<") != std::string::npos) {
            return {type, ""};
        }

        size_t anglePos = type.find('<');
        if (anglePos != std::string::npos) {
            std::string baseType = type.substr(0, anglePos);
            std::string templateArgs = type.substr(anglePos);
            return {baseType, templateArgs};
        }
        return {type, ""};
    };

    auto [expectedBase, expectedArgs] = extractTemplateInfo(normalizedExpected);
    auto [actualBase, actualArgs] = extractTemplateInfo(normalizedActual);

    // For template types: base type AND template arguments must match
    // This prevents Vector2<Float> from being compatible with Vector2<Integer>
    if (!expectedArgs.empty() || !actualArgs.empty()) {
        // If one has template args and the other doesn't, they're incompatible
        if (expectedArgs.empty() != actualArgs.empty()) return false;
        // Both must have same base type AND same template arguments
        if (expectedBase != actualBase) return false;
        if (expectedArgs != actualArgs) return false;
        return true;
    }

    // Future: implement inheritance checking
    return false;
}

bool SemanticAnalyzer::isCompatibleOwnership(Parser::OwnershipType expected, Parser::OwnershipType actual) {
    // Check ownership compatibility rules
    if (expected == actual) return true;

    // Copy can accept owned or reference (creates a copy)
    if (expected == Parser::OwnershipType::Copy) return true;

    // Reference can accept owned (temporary reference)
    if (expected == Parser::OwnershipType::Reference && actual == Parser::OwnershipType::Owned) {
        return true;
    }

    return false;
}

// Parse template arguments with depth-aware splitting
// Handles nested templates like List<Box<Integer>>, Map<String, List<Integer>>
std::vector<std::string> SemanticAnalyzer::parseTemplateArguments(const std::string& argsStr) {
    std::vector<std::string> args;
    size_t start = 0;
    int depth = 0;

    for (size_t i = 0; i < argsStr.size(); ++i) {
        char c = argsStr[i];
        if (c == '<' || c == '(') {
            depth++;
        } else if (c == '>' || c == ')') {
            depth--;
        } else if (c == ',' && depth == 0) {
            // Found a comma at depth 0 - this is a template argument separator
            std::string arg = argsStr.substr(start, i - start);
            // Trim whitespace
            size_t first = arg.find_first_not_of(" \t");
            size_t last = arg.find_last_not_of(" \t");
            if (first != std::string::npos && last != std::string::npos) {
                args.push_back(arg.substr(first, last - first + 1));
            } else if (!arg.empty()) {
                args.push_back(arg);
            }
            start = i + 1;
        }
    }

    // Add the last argument
    if (start < argsStr.size()) {
        std::string arg = argsStr.substr(start);
        // Trim whitespace
        size_t first = arg.find_first_not_of(" \t");
        size_t last = arg.find_last_not_of(" \t");
        if (first != std::string::npos && last != std::string::npos) {
            args.push_back(arg.substr(first, last - first + 1));
        } else if (!arg.empty()) {
            args.push_back(arg);
        }
    }

    return args;
}

std::string SemanticAnalyzer::getExpressionType(Parser::Expression* expr) {
    auto it = expressionTypes.find(expr);
    if (it != expressionTypes.end()) {
        return it->second;
    }
    return UNKNOWN_TYPE;
}

Parser::OwnershipType SemanticAnalyzer::getExpressionOwnership(Parser::Expression* expr) {
    auto it = expressionOwnerships.find(expr);
    if (it != expressionOwnerships.end()) {
        return it->second;
    }
    return Parser::OwnershipType::Owned;
}

// ============================================================================
// TypeContext Population - Bridge XXML types to C++ understanding
// ============================================================================

std::string SemanticAnalyzer::convertXXMLTypeToCpp(const std::string& xxmlType,
                                                   Parser::OwnershipType ownership) {
    // Convert XXML type names to fully qualified C++ types
    std::string cppType;

    // Handle core types
    if (xxmlType == "Integer" || xxmlType == "Language::Core::Integer") {
        cppType = "Language::Core::Integer";
    } else if (xxmlType == "String" || xxmlType == "Language::Core::String") {
        cppType = "Language::Core::String";
    } else if (xxmlType == "Bool" || xxmlType == "Language::Core::Bool") {
        cppType = "Language::Core::Bool";
    } else if (xxmlType == "Float" || xxmlType == "Language::Core::Float") {
        cppType = "Language::Core::Float";
    } else if (xxmlType == "Double" || xxmlType == "Language::Core::Double") {
        cppType = "Language::Core::Double";
    } else if (xxmlType == "None" || xxmlType == "Language::Core::None") {
        cppType = "void";
    } else {
        // User-defined types or already qualified
        cppType = xxmlType;
    }

    // Wrap in Owned<T> if ownership is Owned (^)
    // IMPORTANT: Must match what CodeGenerator does in getOwnershipType()
    // ALL types are wrapped in Owned<T> when ownership is Owned, including value types
    if (ownership == Parser::OwnershipType::Owned) {
        cppType = "Language::Runtime::Owned<" + cppType + ">";
    } else if (ownership == Parser::OwnershipType::Reference) {
        cppType += "&";
    } else if (ownership == Parser::OwnershipType::Copy) {
        // Copy is just the value type
    }

    return cppType;
}

void SemanticAnalyzer::registerExpressionType(Parser::Expression* expr,
                                              const std::string& xxmlType,
                                              Parser::OwnershipType ownership) {
    // Store in legacy maps for backward compatibility
    expressionTypes[expr] = xxmlType;
    expressionOwnerships[expr] = ownership;

    // Create C++-aware ResolvedType and store in TypeContext
    Core::ResolvedType resolved;
    resolved.xxmlTypeName = xxmlType;
    resolved.ownership = ownership;

    // Convert to C++ type
    std::string cppType = convertXXMLTypeToCpp(xxmlType, ownership);
    resolved.cppTypeName = cppType;

    // Analyze C++ type semantics
    resolved.cppTypeInfo = typeContext_.getCppAnalyzer().analyze(cppType);

    // Set flags
    resolved.isBuiltin = (xxmlType == "Integer" || xxmlType == "String" ||
                         xxmlType == "Bool" || xxmlType == "Float" ||
                         xxmlType == "Double" || xxmlType == "None");
    resolved.isPrimitive = resolved.isBuiltin && xxmlType != "None";

    // Store in TypeContext
    typeContext_.setExpressionType(expr, resolved);
}

void SemanticAnalyzer::registerVariableType(const std::string& varName,
                                           const std::string& xxmlType,
                                           Parser::OwnershipType ownership) {
    // Create C++-aware ResolvedType
    Core::ResolvedType resolved;
    resolved.xxmlTypeName = xxmlType;
    resolved.ownership = ownership;

    // Convert to C++ type
    std::string cppType = convertXXMLTypeToCpp(xxmlType, ownership);
    resolved.cppTypeName = cppType;

    // Analyze C++ type semantics
    resolved.cppTypeInfo = typeContext_.getCppAnalyzer().analyze(cppType);

    // Set flags
    resolved.isBuiltin = (xxmlType == "Integer" || xxmlType == "String" ||
                         xxmlType == "Bool" || xxmlType == "Float" ||
                         xxmlType == "Double" || xxmlType == "None");
    resolved.isPrimitive = resolved.isBuiltin && xxmlType != "None";

    // Store in TypeContext
    typeContext_.setVariableType(varName, resolved);
}

void SemanticAnalyzer::validateOwnershipSemantics(Parser::TypeRef* type, const Common::SourceLocation& loc) {
    // Validate ownership rules
    // This is where we'd check things like:
    // - Can't have multiple owned references to same object
    // - References must point to valid owned objects
    // - Etc.
}

void SemanticAnalyzer::validateMethodCall(Parser::CallExpr& node) {
    // Validate that the method exists and parameters match
    // This would check the symbol table for the method signature
}

void SemanticAnalyzer::validateConstructorCall(Parser::CallExpr& node) {
    // Validate constructor calls
}

bool SemanticAnalyzer::isTemporaryExpression(Parser::Expression* expr) {
    // Temporaries (rvalues) are expressions that don't refer to existing objects
    // They are created on-the-fly and destroyed at the end of the statement

    if (!expr) return false;

    // CallExpr creates a temporary (constructor call or method call returning value)
    if (dynamic_cast<Parser::CallExpr*>(expr)) {
        return true;
    }

    // Literals are temporaries
    if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr) ||
        dynamic_cast<Parser::StringLiteralExpr*>(expr) ||
        dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        return true;
    }

    // Binary expressions create temporaries
    if (dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return true;
    }

    // IdentifierExpr refers to an existing variable (lvalue)
    if (dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        return false;
    }

    // MemberAccessExpr can be lvalue if accessing a property/field
    if (dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        return false;  // Accessing a member is an lvalue
    }

    // ReferenceExpr creates a reference to an lvalue
    if (dynamic_cast<Parser::ReferenceExpr*>(expr)) {
        return false;
    }

    // Default to temporary for safety
    return true;
}

// Visitor implementations
void SemanticAnalyzer::visit(Parser::Program& node) {
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }

    // Register this module in the symbol table registry after processing all declarations
    if (symbolTable_) {
        symbolTable_->registerModule();
    }
}

void SemanticAnalyzer::visit(Parser::ImportDecl& node) {
    // Import all exported symbols from the specified module path
    // The import path might be a namespace like "Language::Core"
    // We need to import all matching modules

    // Try to import from all modules that match this import path
    // Since modules are already parsed and in the registry, we can access them
    symbolTable_->importAllFrom(node.modulePath);

    // Track this namespace as imported for unqualified name lookup
    importedNamespaces_.insert(node.modulePath);

    // Register the imported namespace as valid so qualified names can be resolved
    // e.g., #import GLFW; allows GLFW::Window to be recognized
    validNamespaces_.insert(node.modulePath);

    // Also register parent namespaces (e.g., for "Language::Reflection", register "Language")
    std::string ns = node.modulePath;
    size_t pos;
    while ((pos = ns.rfind("::")) != std::string::npos) {
        ns = ns.substr(0, pos);
        validNamespaces_.insert(ns);
    }

    // Also register the namespace import in the symbol table for ambiguity detection
    symbolTable_->addNamespaceImport(node.modulePath);

    // Note: If the module isn't found in the registry, importAllFrom will print a warning
    // This is acceptable since not all imports may be resolved at semantic analysis time
}

void SemanticAnalyzer::visit(Parser::NamespaceDecl& node) {
    std::string previousNamespace = currentNamespace;
    currentNamespace = node.name;

    // Register namespace as valid
    validNamespaces_.insert(node.name);

    // Also register parent namespaces (e.g., for "Language::Reflection", register "Language")
    std::string ns = node.name;
    size_t pos;
    while ((pos = ns.rfind("::")) != std::string::npos) {
        ns = ns.substr(0, pos);
        validNamespaces_.insert(ns);
    }

    // Track this namespace as imported (for classes in imported modules to be accessible)
    importedNamespaces_.insert(node.name);

    // Create namespace scope
    symbolTable_->enterScope(node.name);

    // Define the namespace symbol
    auto symbol = std::make_unique<Symbol>(
        node.name,
        SymbolKind::Namespace,
        "",
        Parser::OwnershipType::Owned,
        node.location
    );
    symbolTable_->define(node.name, std::move(symbol));

    // Process declarations within the namespace
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }

    // Also register this module with the namespace name so imports can find it
    // This allows "#import Language::Core;" to import from files in that namespace
    if (symbolTable_) {
        std::string oldModuleName = symbolTable_->getModuleName();
        symbolTable_->setModuleName(node.name);
        symbolTable_->registerModule();
        // Restore original module name
        symbolTable_->setModuleName(oldModuleName);
    }

    symbolTable_->exitScope();
    currentNamespace = previousNamespace;
}

void SemanticAnalyzer::visit(Parser::ClassDecl& node) {
    std::string previousClass = currentClass;
    currentClass = node.name;

    // Track this class for processor compilation (allows processors to reference local classes)
    localClasses_.push_back(&node);

    // Validate annotations on this class
    for (auto& annotation : node.annotations) {
        annotation->accept(*this);
        validateAnnotationUsage(*annotation, Parser::AnnotationTarget::Classes, node.name, node.location, &node);
    }

    // Record template class if it has template parameters
    if (!node.templateParams.empty()) {
        std::string qualifiedTemplateName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;

        // ✅ SAFE: Create TemplateClassInfo with COPIED data, not raw pointers
        TemplateClassInfo templateInfo;
        templateInfo.qualifiedName = qualifiedTemplateName;
        templateInfo.templateParams = node.templateParams;  // Copy template params
        templateInfo.baseClassName = node.baseClass;  // Copy base class name
        templateInfo.astNode = &node;  // Only valid for same-module access

        templateClasses[qualifiedTemplateName] = templateInfo;
        // Also register without namespace for convenience
        if (!currentNamespace.empty()) {
            templateClasses[node.name] = templateInfo;
        }

        // Also register in global template registry for cross-module access
        if (context_) {
            auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
            if (!globalTemplates) {
                context_->setCustomData("globalTemplateClasses", std::unordered_map<std::string, TemplateClassInfo>{});
                globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
            }
            if (globalTemplates) {
                // For global registry, don't set astNode since it may become invalid
                TemplateClassInfo globalInfo = templateInfo;
                globalInfo.astNode = nullptr;  // Clear astNode for cross-module safety
                (*globalTemplates)[qualifiedTemplateName] = globalInfo;
                if (!currentNamespace.empty()) {
                    (*globalTemplates)[node.name] = globalInfo;
                }
            }
        }
    }

    // Define the class symbol in the CURRENT (parent) scope first
    auto symbol = std::make_unique<Symbol>(
        node.name,
        SymbolKind::Class,
        node.name,
        Parser::OwnershipType::Owned,
        node.location
    );
    symbol->astNode = &node;
    symbolTable_->define(node.name, std::move(symbol));

    // Export the class if we're at global scope (making it available to other modules)
    if (symbolTable_->getCurrentScope() == symbolTable_->getGlobalScope()) {
        symbolTable_->exportSymbol(node.name);
    }

    // Check base class if it exists (but don't error if not found yet - forward references)
    if (!node.baseClass.empty() && node.baseClass != "None") {
        Symbol* baseClassSym = symbolTable_->resolve(node.baseClass);
        // Note: Commenting out the error for now to allow forward references
        // In a production compiler, this would be a two-pass system
        /*
        if (!baseClassSym || baseClassSym->kind != SymbolKind::Class) {
            errorReporter.reportError(
                Common::ErrorCode::UndefinedType,
                "Base class '" + node.baseClass + "' not found",
                node.location
            );
        }
        */
    }

    // Now enter class scope for processing members
    symbolTable_->enterScope(node.name);

    // ✅ Phase 5: Set template context and register template parameters
    bool wasInTemplateDefinition = inTemplateDefinition;
    std::set<std::string> previousTemplateParams = templateTypeParameters;

    if (!node.templateParams.empty()) {
        inTemplateDefinition = true;
        // Populate template type parameters
        for (const auto& templateParam : node.templateParams) {
            templateTypeParameters.insert(templateParam.name);
        }
    }

    // Register template parameters as valid types within this class scope
    for (const auto& templateParam : node.templateParams) {
        auto templateTypeSymbol = std::make_unique<Symbol>(
            templateParam.name,
            SymbolKind::Class,  // Treat template parameters as types
            templateParam.name,
            Parser::OwnershipType::Owned,
            templateParam.location
        );
        symbolTable_->define(templateParam.name, std::move(templateTypeSymbol));
    }

    // Create ClassInfo entry for validation
    std::string qualifiedClassName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;
    ClassInfo classInfo;
    classInfo.qualifiedName = qualifiedClassName;
    // ✅ SAFE: Copy data from AST instead of storing raw pointer
    classInfo.baseClassName = node.baseClass;  // Copy base class name
    classInfo.templateParams = node.templateParams;  // Copy template params
    classInfo.isTemplate = !node.templateParams.empty();
    classInfo.isCompiletime = node.isCompiletime;
    classInfo.astNode = &node;  // Only valid for same-module access

    // Collect methods and properties by processing sections
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Register template methods
                if (!methodDecl->templateParams.empty()) {
                    std::string methodKey = qualifiedClassName + "::" + methodDecl->name;
                    // ✅ SAFE: Create TemplateMethodInfo with COPIED data
                    TemplateMethodInfo methodInfo;
                    methodInfo.className = qualifiedClassName;
                    methodInfo.methodName = methodDecl->name;
                    if (methodDecl->returnType) {
                        methodInfo.returnTypeName = methodDecl->returnType->typeName;  // Copy return type
                    }
                    methodInfo.templateParams = methodDecl->templateParams;  // Copy params
                    // ✅ Extract calls from body at registration time (AST is valid here)
                    methodInfo.callsInBody = extractCallsFromMethodBody(methodDecl, methodDecl->templateParams);
                    methodInfo.astNode = methodDecl;  // Only valid for same-module access
                    templateMethods[methodKey] = methodInfo;
                }

                MethodInfo methodInfo;
                // Reconstruct full return type including template arguments
                std::string fullReturnType = methodDecl->returnType->typeName;
                if (!methodDecl->returnType->templateArgs.empty()) {
                    fullReturnType += "<";
                    for (size_t i = 0; i < methodDecl->returnType->templateArgs.size(); ++i) {
                        if (i > 0) fullReturnType += ", ";
                        fullReturnType += methodDecl->returnType->templateArgs[i].typeArg;
                    }
                    fullReturnType += ">";
                }
                methodInfo.returnType = fullReturnType;
                methodInfo.returnOwnership = methodDecl->returnType->ownership;
                methodInfo.isConstructor = (methodDecl->name == "Constructor");
                methodInfo.isCompiletime = methodDecl->isCompiletime;
                for (auto& param : methodDecl->parameters) {
                    // Reconstruct full parameter type including template arguments
                    std::string fullParamType = param->type->typeName;
                    if (!param->type->templateArgs.empty()) {
                        fullParamType += "<";
                        for (size_t i = 0; i < param->type->templateArgs.size(); ++i) {
                            if (i > 0) fullParamType += ", ";
                            fullParamType += param->type->templateArgs[i].typeArg;
                        }
                        fullParamType += ">";
                    }
                    methodInfo.parameters.push_back({fullParamType, param->type->ownership});
                }

                classInfo.methods[methodDecl->name] = methodInfo;
            } else if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                classInfo.properties[propDecl->name] = {propDecl->type->typeName, propDecl->type->ownership};
            } else if (auto* ctorDecl = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                MethodInfo methodInfo;
                methodInfo.returnType = node.name;
                methodInfo.returnOwnership = Parser::OwnershipType::Owned;
                methodInfo.isConstructor = true;
                methodInfo.isCompiletime = ctorDecl->isCompiletime;
                // Collect constructor parameters
                for (auto& param : ctorDecl->parameters) {
                    methodInfo.parameters.push_back({param->type->typeName, param->type->ownership});
                }

                classInfo.methods["Constructor"] = methodInfo;
            }
        }
    }

    // Register the class in the registry (✅ now instance-based)
    classRegistry_[qualifiedClassName] = classInfo;
    if (!currentNamespace.empty()) {
        // Also register without namespace prefix for easier lookup
        classRegistry_[node.name] = classInfo;
    }

    // ✅ FIX: Also register in global registry shared across all analyzers
    if (context_) {
        auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
        if (!globalRegistry) {
            // Create global registry if it doesn't exist
            context_->setCustomData("globalClassRegistry", std::unordered_map<std::string, ClassInfo>{});
            globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
        }
        if (globalRegistry) {
            // ✅ SAFE: Create copy with astNode cleared for cross-module safety
            ClassInfo globalCopy = classInfo;
            globalCopy.astNode = nullptr;  // Clear raw pointer for cross-module safety
            (*globalRegistry)[qualifiedClassName] = globalCopy;
            if (!currentNamespace.empty()) {
                (*globalRegistry)[node.name] = globalCopy;
            }
        }
    }

    // ✅ FIX: First pass - Define all method symbols before processing bodies
    // This ensures methods can call each other regardless of declaration order
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Check if method already exists
                Symbol* existing = symbolTable_->getCurrentScope()->resolveLocal(methodDecl->name);
                if (!existing) {
                    // Define the method symbol (signature only)
                    auto symbol = std::make_unique<Symbol>(
                        methodDecl->name,
                        SymbolKind::Method,
                        methodDecl->returnType->typeName,
                        methodDecl->returnType->ownership,
                        methodDecl->location
                    );
                    symbolTable_->define(methodDecl->name, std::move(symbol));
                }
            }
        }
    }

    // Process access sections (normal visitor pattern)
    for (auto& section : node.sections) {
        section->accept(*this);
    }

    if (symbolTable_) {
        symbolTable_->exitScope();
    }

    // ✅ Phase 5: Restore template context
    inTemplateDefinition = wasInTemplateDefinition;
    templateTypeParameters = previousTemplateParams;

    currentClass = previousClass;
}

void SemanticAnalyzer::visit(Parser::NativeStructureDecl& node) {
    // Validate NativeStructure declaration
    // 1. All properties must be NativeType
    // 2. Alignment must be power of 2
    // 3. No methods allowed (just properties)

    // Validate alignment is power of 2
    if (node.alignment != 0 && (node.alignment & (node.alignment - 1)) != 0) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "NativeStructure alignment must be a power of 2, got " + std::to_string(node.alignment),
            node.location
        );
    }

    // Validate all properties are NativeType
    for (const auto& prop : node.properties) {
        if (prop->type && prop->type->typeName.find("NativeType<") != 0) {
            errorReporter.reportError(
                Common::ErrorCode::TypeMismatch,
                "NativeStructure property '" + prop->name + "' must be a NativeType, got '" + prop->type->typeName + "'",
                prop->location
            );
        }
    }

    // Register the native structure as a type
    // (This allows it to be used in method parameters/returns)
    std::string fullName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;
    if (classRegistry_.find(fullName) != classRegistry_.end()) {
        // During validation phase, types may already exist from merged registries - skip silently
        // Only report error during Phase 1 (registration phase) when validation is disabled
        if (!enableValidation) {
            errorReporter.reportError(
                Common::ErrorCode::DuplicateDeclaration,
                "Type '" + fullName + "' is already defined",
                node.location
            );
        }
    } else {
        ClassInfo info;
        info.qualifiedName = fullName;
        for (const auto& prop : node.properties) {
            info.properties[prop->name] = {prop->type->typeName, prop->type->ownership};
        }
        classRegistry_[fullName] = info;
    }
}

void SemanticAnalyzer::visit(Parser::CallbackTypeDecl& node) {
    // Register the callback type in the callback type registry
    std::string fullName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;

    // Check for duplicate
    if (callbackTypeRegistry_.find(fullName) != callbackTypeRegistry_.end()) {
        // During validation phase, types may already exist from merged registries - skip silently
        if (!enableValidation) {
            errorReporter.reportError(
                Common::ErrorCode::DuplicateDeclaration,
                "CallbackType '" + fullName + "' is already defined",
                node.location
            );
        }
        return;
    }

    // Validate return type exists (if not Void)
    if (node.returnType && node.returnType->typeName != "Void") {
        // Basic type validation - ensure it's a known type
        // For FFI callbacks, we accept NativeType, primitive types, and pointer types
        // More sophisticated validation could be added here
    }

    // Build callback type info
    CallbackTypeInfo info;
    info.name = node.name;
    info.qualifiedName = fullName;
    info.convention = node.convention;
    info.returnType = node.returnType ? node.returnType->typeName : "Void";
    info.returnOwnership = node.returnType ? node.returnType->ownership : Parser::OwnershipType::Copy;
    info.astNode = &node;

    // Process parameters
    for (const auto& param : node.parameters) {
        CallbackParamInfo paramInfo;
        paramInfo.name = param->name;
        paramInfo.typeName = param->type ? param->type->typeName : UNKNOWN_TYPE;
        paramInfo.ownership = param->type ? param->type->ownership : Parser::OwnershipType::Copy;
        info.parameters.push_back(paramInfo);
    }

    callbackTypeRegistry_[fullName] = info;
}

void SemanticAnalyzer::visit(Parser::EnumValueDecl& node) {
    // Individual enum values are validated in context of their parent enumeration
}

void SemanticAnalyzer::visit(Parser::EnumerationDecl& node) {
    // Register the enumeration in the enum registry
    std::string fullName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;

    // Check for duplicate
    if (enumRegistry_.find(fullName) != enumRegistry_.end()) {
        // During validation phase, enums may already exist from merged registries - skip silently
        // Only report error during Phase 1 (registration phase) when validation is disabled
        if (!enableValidation) {
            errorReporter.reportError(
                Common::ErrorCode::DuplicateDeclaration,
                "Enumeration '" + fullName + "' is already defined",
                node.location
            );
        }
        return;
    }

    // Build enum info
    EnumInfo enumInfo;
    enumInfo.name = node.name;
    enumInfo.qualifiedName = fullName;

    std::set<std::string> valueNames;
    std::set<int64_t> valueValues;

    for (const auto& val : node.values) {
        // Check for duplicate value names
        if (valueNames.count(val->name)) {
            errorReporter.reportError(
                Common::ErrorCode::DuplicateDeclaration,
                "Duplicate enum value name '" + val->name + "' in enumeration '" + node.name + "'",
                val->location
            );
        } else {
            valueNames.insert(val->name);
        }

        // Note: We allow duplicate values (e.g., aliases) but not duplicate names
        EnumValueInfo valInfo;
        valInfo.name = val->name;
        valInfo.value = val->value;
        enumInfo.values.push_back(valInfo);
    }

    enumRegistry_[fullName] = enumInfo;
}

void SemanticAnalyzer::visit(Parser::AccessSection& node) {
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }
}

void SemanticAnalyzer::visit(Parser::PropertyDecl& node) {
    // Validate annotations on this property
    for (auto& annotation : node.annotations) {
        annotation->accept(*this);
        validateAnnotationUsage(*annotation, Parser::AnnotationTarget::Properties, node.name, node.location, &node);
    }

    // Visit type to record template instantiations
    node.type->accept(*this);

    // Check if property already exists
    Symbol* existing = symbolTable_->getCurrentScope()->resolveLocal(node.name);
    if (existing) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateDeclaration,
            "Property '" + node.name + "' already declared",
            node.location
        );
        errorReporter.reportNote(
            "Previous declaration here",
            existing->location
        );
        return;
    }

    // Validate the type exists (allow NativeType and builtin types)
    Symbol* typeSym = symbolTable_->resolve(node.type->typeName);
    bool foundInClassRegistry = false;

    // If not found and we have a current namespace, try with namespace prefix
    if (!typeSym && !currentNamespace.empty() &&
        node.type->typeName.find("::") == std::string::npos) {
        std::string qualifiedType = currentNamespace + "::" + node.type->typeName;
        typeSym = symbolTable_->resolve(qualifiedType);
        // Also check class registry for cross-module same-namespace types
        if (!typeSym && classRegistry_.find(qualifiedType) != classRegistry_.end()) {
            foundInClassRegistry = true;
        }
        // Also check global class registry in CompilationContext
        if (!typeSym && !foundInClassRegistry && context_) {
            auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
            if (globalRegistry && globalRegistry->find(qualifiedType) != globalRegistry->end()) {
                foundInClassRegistry = true;
            }
        }
    }

    // Also check classRegistry directly for global-scope NativeStructures
    if (!typeSym && !foundInClassRegistry) {
        if (classRegistry_.find(node.type->typeName) != classRegistry_.end()) {
            foundInClassRegistry = true;
        }
        // Also check global class registry
        if (!foundInClassRegistry && context_) {
            auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
            if (globalRegistry && globalRegistry->find(node.type->typeName) != globalRegistry->end()) {
                foundInClassRegistry = true;
            }
        }
    }

    if (!typeSym && !foundInClassRegistry && node.type->typeName.find("NativeType<") != 0) {
        // ✅ Phase 5: Skip validation if this is a template parameter in a template definition
        bool isTemplateParam = (inTemplateDefinition &&
                                templateTypeParameters.find(node.type->typeName) != templateTypeParameters.end());

        // Only error if it's not a NativeType, builtin type, function type, template parameter,
        // or compiler intrinsic type (when in processor context)
        bool isIntrinsicType = (inProcessorContext_ && isCompilerIntrinsicType(node.type->typeName));
        if (!isTemplateParam && !isIntrinsicType &&
            node.type->typeName != "Integer" &&
            node.type->typeName != "String" &&
            node.type->typeName != "Bool" &&
            node.type->typeName != "Float" &&
            node.type->typeName != "Double" &&
            node.type->typeName != "None" &&
            node.type->typeName != "__function") {
            errorReporter.reportError(
                Common::ErrorCode::UndefinedType,
                "Type '" + node.type->typeName + "' not found",
                node.location
            );
        }
    }

    // Define the property symbol
    auto symbol = std::make_unique<Symbol>(
        node.name,
        SymbolKind::Property,
        node.type->typeName,
        node.type->ownership,
        node.location
    );
    symbolTable_->define(node.name, std::move(symbol));
}

void SemanticAnalyzer::visit(Parser::ConstructorDecl& node) {
    symbolTable_->enterScope("Constructor");

    // Process parameters
    for (auto& param : node.parameters) {
        param->accept(*this);
    }

    // Process body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    symbolTable_->exitScope();
}

void SemanticAnalyzer::visit(Parser::DestructorDecl& node) {
    symbolTable_->enterScope("Destructor");

    // Process body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    symbolTable_->exitScope();

    // Note: Tracking that this class has a destructor is done in ClassDecl visit
}

void SemanticAnalyzer::visit(Parser::MethodDecl& node) {
    // Validate annotations on this method
    for (auto& annotation : node.annotations) {
        annotation->accept(*this);
        validateAnnotationUsage(*annotation, Parser::AnnotationTarget::Methods, node.name, node.location, &node);
    }

    // Process @NativeFunction annotation to populate FFI fields
    for (const auto& annotation : node.annotations) {
        if (annotation->annotationName == "NativeFunction") {
            node.isNative = true;

            // Extract annotation arguments
            for (const auto& arg : annotation->arguments) {
                if (arg.first == "path") {
                    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.second.get())) {
                        node.nativePath = strLit->value;
                    }
                } else if (arg.first == "name") {
                    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.second.get())) {
                        node.nativeSymbol = strLit->value;
                    }
                } else if (arg.first == "convention") {
                    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.second.get())) {
                        std::string conv = strLit->value;
                        if (conv == "cdecl") {
                            node.callingConvention = Parser::CallingConvention::CDecl;
                        } else if (conv == "stdcall") {
                            node.callingConvention = Parser::CallingConvention::StdCall;
                        } else if (conv == "fastcall") {
                            node.callingConvention = Parser::CallingConvention::FastCall;
                        } else {
                            node.callingConvention = Parser::CallingConvention::Auto;
                        }
                    }
                }
            }
            break;  // Only process one @NativeFunction annotation
        }
    }

    // Track native methods for ABI lowering pass
    if (node.isNative) {
        nativeMethods_.push_back(&node);
    }

    // Visit return type to record template instantiations
    node.returnType->accept(*this);

    // Check if method already exists
    Symbol* existing = symbolTable_->getCurrentScope()->resolveLocal(node.name);
    if (!existing) {
        // Define the method symbol if not already defined
        // (It should already be defined from the first pass in ClassDecl)
        auto symbol = std::make_unique<Symbol>(
            node.name,
            SymbolKind::Method,
            node.returnType->typeName,
            node.returnType->ownership,
            node.location
        );
        symbolTable_->define(node.name, std::move(symbol));
    }

    // Enter method scope
    symbolTable_->enterScope(node.name);

    // Reset move tracking for this function scope
    resetMovedVariables();

    // ✅ Phase 5: Set template context for template methods
    bool wasInTemplateDefinition = inTemplateDefinition;
    std::set<std::string> previousTemplateParams = templateTypeParameters;

    if (!node.templateParams.empty()) {
        inTemplateDefinition = true;
        // Add method template parameters to the set
        for (const auto& templateParam : node.templateParams) {
            templateTypeParameters.insert(templateParam.name);

            // Register template parameters as valid types within method scope
            auto templateTypeSymbol = std::make_unique<Symbol>(
                templateParam.name,
                SymbolKind::Class,  // Treat template parameters as types
                templateParam.name,
                Parser::OwnershipType::Owned,
                templateParam.location
            );
            symbolTable_->define(templateParam.name, std::move(templateTypeSymbol));
        }
    }

    // Process parameters
    for (auto& param : node.parameters) {
        param->accept(*this);
    }

    // Process body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // ✅ Phase 5: Restore template context
    inTemplateDefinition = wasInTemplateDefinition;
    templateTypeParameters = previousTemplateParams;

    symbolTable_->exitScope();
}

void SemanticAnalyzer::visit(Parser::ParameterDecl& node) {
    // Visit type to record template instantiations
    node.type->accept(*this);

    // Check if parameter already exists
    Symbol* existing = symbolTable_->getCurrentScope()->resolveLocal(node.name);
    if (existing) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateDeclaration,
            "Parameter '" + node.name + "' already declared",
            node.location
        );
        return;
    }

    // Define the parameter symbol
    auto symbol = std::make_unique<Symbol>(
        node.name,
        SymbolKind::Parameter,
        node.type->typeName,
        node.type->ownership,
        node.location
    );
    symbolTable_->define(node.name, std::move(symbol));
}

void SemanticAnalyzer::visit(Parser::EntrypointDecl& node) {
    symbolTable_->enterScope("Entrypoint");

    // Reset move tracking for entrypoint scope
    resetMovedVariables();

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    symbolTable_->exitScope();
}

// Statement visitors
void SemanticAnalyzer::visit(Parser::InstantiateStmt& node) {
    // Validate annotations on this variable
    for (auto& annotation : node.annotations) {
        annotation->accept(*this);
        validateAnnotationUsage(*annotation, Parser::AnnotationTarget::Variables, node.variableName, node.location, &node);
    }

    // Check if variable already exists
    Symbol* existing = symbolTable_->getCurrentScope()->resolveLocal(node.variableName);
    if (existing) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateDeclaration,
            "Variable '" + node.variableName + "' already declared",
            node.location
        );
        return;
    }

    // Validate the type exists (allow qualified names, NativeType, and user-defined types)
    Symbol* typeSym = symbolTable_->resolve(node.type->typeName);
    bool foundInClassRegistry = false;

    // If not found and we have a current namespace, try with namespace prefix
    if (!typeSym && !currentNamespace.empty() &&
        node.type->typeName.find("::") == std::string::npos) {
        std::string qualifiedType = currentNamespace + "::" + node.type->typeName;
        typeSym = symbolTable_->resolve(qualifiedType);
        // Also check class registry for cross-module same-namespace types
        if (!typeSym && classRegistry_.find(qualifiedType) != classRegistry_.end()) {
            foundInClassRegistry = true;
        }
        // Also check global class registry in CompilationContext
        if (!typeSym && !foundInClassRegistry && context_) {
            auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
            if (globalRegistry && globalRegistry->find(qualifiedType) != globalRegistry->end()) {
                foundInClassRegistry = true;
            }
        }
    }

    // Also check classRegistry directly for global-scope NativeStructures
    if (!typeSym && !foundInClassRegistry) {
        if (classRegistry_.find(node.type->typeName) != classRegistry_.end()) {
            foundInClassRegistry = true;
        }
        // Also check global class registry
        if (!foundInClassRegistry && context_) {
            auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
            if (globalRegistry && globalRegistry->find(node.type->typeName) != globalRegistry->end()) {
                foundInClassRegistry = true;
            }
        }
    }

    // ✅ Phase 5: Skip validation if this is a template parameter in a template definition
    bool isTemplateParam = (inTemplateDefinition &&
                            templateTypeParameters.find(node.type->typeName) != templateTypeParameters.end());

    // Skip validation for types in known namespaces (they'll be resolved at link time)
    bool isInKnownNamespace = !currentNamespace.empty() &&
        (currentNamespace.find("Language::") == 0 || validNamespaces_.count(currentNamespace) > 0);

    // Check for compiler intrinsic types when in processor context
    bool isIntrinsicType = (inProcessorContext_ && isCompilerIntrinsicType(node.type->typeName));

    // Don't error if type is qualified (contains ::), NativeType, template parameter, intrinsic, or a builtin type
    if (!typeSym && !foundInClassRegistry && !isTemplateParam && !isInKnownNamespace && !isIntrinsicType &&
        node.type->typeName.find("::") == std::string::npos &&
        node.type->typeName.find("NativeType<") != 0) {
        // Only error for simple unqualified types that aren't found
        if (node.type->typeName != "Integer" &&
            node.type->typeName != "String" &&
            node.type->typeName != "Bool" &&
            node.type->typeName != "Float" &&
            node.type->typeName != "Double" &&
            node.type->typeName != "None" &&
            node.type->typeName != "__function") {
            errorReporter.reportError(
                Common::ErrorCode::UndefinedType,
                "Type '" + node.type->typeName + "' not found",
                node.location
            );
        }
    }

    // VALIDATION: Check for unqualified types (ownership must be specified)
    // ✅ Phase 5: Skip ownership validation for template parameters in template definitions
    if (node.type->ownership == Parser::OwnershipType::None && !isTemplateParam) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidOwnership,
            "Type '" + node.type->typeName + "' must have an ownership qualifier (^, &, or %). "
            "Unqualified types are only allowed in template parameters.",
            node.location
        );
        return;
    }

    // VALIDATION: Check if type is a template class but no template arguments provided
    std::string baseTypeName = node.type->typeName;
    // Remove namespace prefix for template class lookup
    std::string simpleTypeName = baseTypeName;
    size_t lastColon = baseTypeName.rfind("::");
    if (lastColon != std::string::npos) {
        simpleTypeName = baseTypeName.substr(lastColon + 2);
    }

    // Check if this is a template class that requires template parameters
    if (node.type->templateArgs.empty()) {
        // Look up if this is a template class (check local first, then global)
        // ✅ SAFE: Use TemplateClassInfo pointer instead of raw ClassDecl pointer
        const TemplateClassInfo* templateInfo = nullptr;

        auto it = templateClasses.find(baseTypeName);
        if (it != templateClasses.end()) {
            templateInfo = &it->second;
        } else if (lastColon != std::string::npos) {
            // Try without namespace
            it = templateClasses.find(simpleTypeName);
            if (it != templateClasses.end()) {
                templateInfo = &it->second;
            }
        }

        // Check global registry if not found locally
        if (!templateInfo && context_) {
            auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
            if (globalTemplates) {
                auto git = globalTemplates->find(baseTypeName);
                if (git != globalTemplates->end()) {
                    templateInfo = &git->second;
                } else if (lastColon != std::string::npos) {
                    git = globalTemplates->find(simpleTypeName);
                    if (git != globalTemplates->end()) {
                        templateInfo = &git->second;
                    }
                }
            }
        }

        // ✅ SAFE: Access copied templateParams from TemplateClassInfo
        if (templateInfo && !templateInfo->templateParams.empty()) {
            // This is a template class that requires parameters but none were provided
            std::string paramList = "<";
            for (size_t i = 0; i < templateInfo->templateParams.size(); ++i) {
                if (i > 0) paramList += ", ";
                paramList += templateInfo->templateParams[i].name;
            }
            paramList += ">";

            errorReporter.reportError(
                Common::ErrorCode::InvalidSyntax,
                "Template class '" + baseTypeName + "' requires template parameters " + paramList +
                ". Usage: " + baseTypeName + "<Type>^",
                node.location
            );
            return;
        }
    }

    // Visit the type to record template instantiations
    node.type->accept(*this);

    // Analyze initializer first to check if it's a temporary
    node.initializer->accept(*this);

    // VALIDATION: Check for reference to temporary
    if (node.type->ownership == Parser::OwnershipType::Reference) {
        if (isTemporaryExpression(node.initializer.get())) {
            errorReporter.reportError(
                Common::ErrorCode::InvalidReference,
                "Cannot bind reference (&) to temporary value. "
                "Constructor calls and expressions create temporary objects that are destroyed immediately. "
                "Use owned (^) to take ownership, or reference an existing variable.",
                node.location
            );
            return;
        }
    }

    // Build full type name including template arguments
    std::string fullTypeName = node.type->typeName;
    if (!node.type->templateArgs.empty()) {
        fullTypeName += "<";
        for (size_t i = 0; i < node.type->templateArgs.size(); ++i) {
            if (i > 0) fullTypeName += ", ";
            fullTypeName += node.type->templateArgs[i].typeArg;
        }
        fullTypeName += ">";
    }

    // Type check initializer with full template type names
    std::string initType = getExpressionType(node.initializer.get());

    // Skip type check if:
    // 1. initType is Unknown or Deferred (template-dependent expression)
    // 2. Target type is a template parameter (will be resolved at instantiation)
    // 3. We're in a template definition (defer all type checks)
    bool isTargetTemplateParam = (templateTypeParameters.find(fullTypeName) != templateTypeParameters.end());
    if (initType != UNKNOWN_TYPE && initType != DEFERRED_TYPE &&
        !isTargetTemplateParam && !inTemplateDefinition &&
        !isCompatibleType(fullTypeName, initType)) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Cannot assign value of type '" + initType + "' to variable of type '" + fullTypeName + "'. " +
            "Template types with different parameters are incompatible (e.g., Vector2<Float> != Vector2<Integer>).",
            node.location
        );
        return;
    }

        // VALIDATION: Compile-time variable constraints
    if (node.isCompiletime && enableValidation) {
        // Compile-time variable must have a compile-time capable type
        if (!isCompiletimeType(node.type->typeName)) {
            errorReporter.reportError(
                Common::ErrorCode::TypeMismatch,
                "Cannot create compile-time variable of runtime type '" + node.type->typeName + "'. "
                "Use a built-in type (Integer, Bool, Float, Double, String) or a class declared Compiletime.",
                node.location
            );
        }
    }    // Define the variable symbol
    auto symbol = std::make_unique<Symbol>(
        node.variableName,
        SymbolKind::LocalVariable,
        fullTypeName,  // Use full type name with template args
        node.type->ownership,
        node.location
    );
    symbol->isCompiletime = node.isCompiletime;
    symbolTable_->define(node.variableName, std::move(symbol));

    // Register variable type in TypeContext for code generation
    registerVariableType(node.variableName, fullTypeName, node.type->ownership);

    // If initializer is a template lambda, register it for template instantiation tracking
    if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(node.initializer.get())) {
        if (!lambdaExpr->templateParams.empty()) {
            TemplateLambdaInfo lambdaInfo;
            lambdaInfo.variableName = node.variableName;
            lambdaInfo.templateParams = lambdaExpr->templateParams;
            lambdaInfo.astNode = lambdaExpr;
            // Store captured variable types for IR generation
            for (const auto& capture : lambdaExpr->captures) {
                Symbol* capturedSymbol = symbolTable_->resolve(capture.varName);
                if (capturedSymbol) {
                    std::string capturedType = capturedSymbol->typeName;
                    // Add ownership modifier
                    switch (capturedSymbol->ownership) {
                        case Parser::OwnershipType::Owned: capturedType += "^"; break;
                        case Parser::OwnershipType::Reference: capturedType += "&"; break;
                        case Parser::OwnershipType::Copy: capturedType += "%"; break;
                        default: break;
                    }
                    lambdaInfo.capturedVarTypes[capture.varName] = capturedType;
                }
            }
            templateLambdas_[node.variableName] = lambdaInfo;
        }
    }

    // If this is a function type, register it for .call() ownership validation
    if (auto* funcTypeRef = dynamic_cast<Parser::FunctionTypeRef*>(node.type.get())) {
        registerFunctionType(node.variableName, funcTypeRef);
    }
}

void SemanticAnalyzer::visit(Parser::AssignmentStmt& node) {
    // Analyze the target expression (lvalue)
    node.target->accept(*this);

    // Verify it's a valid lvalue (for now, we allow all expressions that were analyzed successfully)
    // In a more complete implementation, we would check that it's actually assignable

    // Analyze the value expression (rvalue)
    node.value->accept(*this);

    // Type check (get types from both sides)
    std::string targetType = getExpressionType(node.target.get());
    std::string valueType = getExpressionType(node.value.get());

    // Skip type check if either type is Unknown or Deferred (template-dependent)
    if (targetType != UNKNOWN_TYPE && targetType != DEFERRED_TYPE &&
        valueType != UNKNOWN_TYPE && valueType != DEFERRED_TYPE &&
        !isCompatibleType(targetType, valueType)) {
        // Warn about type mismatch but don't error
    }
}

void SemanticAnalyzer::visit(Parser::RunStmt& node) {
    node.expression->accept(*this);
}

void SemanticAnalyzer::visit(Parser::ForStmt& node) {
    symbolTable_->enterScope("ForLoop");

    // Define iterator variable
    auto symbol = std::make_unique<Symbol>(
        node.iteratorName,
        SymbolKind::LocalVariable,
        node.iteratorType->typeName,
        node.iteratorType->ownership,
        node.location
    );
    symbolTable_->define(node.iteratorName, std::move(symbol));

    // Analyze initialization
    node.rangeStart->accept(*this);

    if (node.isCStyleLoop) {
        // C-style for loop: analyze condition and increment
        if (node.condition) {
            node.condition->accept(*this);
        }
        if (node.increment) {
            node.increment->accept(*this);
        }
    } else {
        // Range-based for loop: analyze range end
        node.rangeEnd->accept(*this);

        // Check that range expressions are integers
        std::string startType = getExpressionType(node.rangeStart.get());
        std::string endType = getExpressionType(node.rangeEnd.get());

        if (startType != "Integer") {
            errorReporter.reportError(
                Common::ErrorCode::TypeMismatch,
                "For loop range start must be Integer, got " + startType,
                node.rangeStart->location
            );
        }

        if (endType != "Integer") {
            errorReporter.reportError(
                Common::ErrorCode::TypeMismatch,
                "For loop range end must be Integer, got " + endType,
                node.rangeEnd->location
            );
        }
    }

    // Analyze body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    symbolTable_->exitScope();
}

void SemanticAnalyzer::visit(Parser::ExitStmt& node) {
    node.exitCode->accept(*this);

    // Check that exit code is Integer
    std::string exitType = getExpressionType(node.exitCode.get());
    if (exitType != "Integer") {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Exit code must be Integer, got " + exitType,
            node.exitCode->location
        );
    }
}

void SemanticAnalyzer::visit(Parser::ReturnStmt& node) {
    if (node.value) {
        node.value->accept(*this);
    }
}

void SemanticAnalyzer::visit(Parser::IfStmt& node) {
    // Analyze condition
    node.condition->accept(*this);

    // Check that condition is Bool (skip if Unknown or Deferred - template-dependent)
    std::string condType = getExpressionType(node.condition.get());
    if (condType != "Bool" && condType != UNKNOWN_TYPE && condType != DEFERRED_TYPE) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "If condition must be Bool, got " + condType,
            node.condition->location
        );
    }

    // Analyze then branch
    symbolTable_->enterScope("IfThen");
    for (auto& stmt : node.thenBranch) {
        stmt->accept(*this);
    }
    symbolTable_->exitScope();

    // Analyze else branch if present
    if (!node.elseBranch.empty()) {
        symbolTable_->enterScope("IfElse");
        for (auto& stmt : node.elseBranch) {
            stmt->accept(*this);
        }
        symbolTable_->exitScope();
    }
}

void SemanticAnalyzer::visit(Parser::WhileStmt& node) {
    // Analyze condition
    node.condition->accept(*this);

    // Check that condition is Bool (skip if Unknown or Deferred - template-dependent)
    std::string condType = getExpressionType(node.condition.get());
    if (condType != "Bool" && condType != UNKNOWN_TYPE && condType != DEFERRED_TYPE) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "While condition must be Bool, got " + condType,
            node.condition->location
        );
    }

    // Analyze body
    symbolTable_->enterScope("WhileBody");
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    symbolTable_->exitScope();
}

void SemanticAnalyzer::visit(Parser::BreakStmt& node) {
    // Break statements are valid - no additional analysis needed
    // In a more complete implementation, we could check that we're inside a loop
}

void SemanticAnalyzer::visit(Parser::ContinueStmt& node) {
    // Continue statements are valid - no additional analysis needed
    // In a more complete implementation, we could check that we're inside a loop
}

// Expression visitors
void SemanticAnalyzer::visit(Parser::IntegerLiteralExpr& node) {
    registerExpressionType(&node, "Integer", Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::FloatLiteralExpr& node) {
    registerExpressionType(&node, "Float", Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::DoubleLiteralExpr& node) {
    registerExpressionType(&node, "Double", Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::StringLiteralExpr& node) {
    registerExpressionType(&node, "String", Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::BoolLiteralExpr& node) {
    registerExpressionType(&node, "Bool", Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::ThisExpr& node) {
    // 'this' can only be used inside a class method
    if (currentClass.empty()) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidSyntax,
            "'this' can only be used inside a class method",
            node.location
        );
        expressionTypes[&node] = UNKNOWN_TYPE;
        expressionOwnerships[&node] = Parser::OwnershipType::Reference;
        return;
    }

    // 'this' has the type of the current class and is a reference
    expressionTypes[&node] = currentClass;
    expressionOwnerships[&node] = Parser::OwnershipType::Reference;
}

void SemanticAnalyzer::visit(Parser::IdentifierExpr& node) {
    // Check if this is a qualified name (contains ::)
    if (node.name.find("::") != std::string::npos) {
        // Try to resolve qualified names like System::Console or String::Constructor
        // Extract the class name (everything before the last ::)
        std::string qualifiedName = node.name;

        // Check if this is an exact class match
        ClassInfo* classInfo = findClass(qualifiedName);
        if (classInfo) {
            // This is a class type reference (e.g., "System::Console")
            expressionTypes[&node] = qualifiedName;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Check if this is a static method reference (Class::Method)
        size_t lastColonPos = qualifiedName.rfind("::");
        if (lastColonPos != std::string::npos && lastColonPos > 0) {
            std::string className = qualifiedName.substr(0, lastColonPos);
            std::string memberName = qualifiedName.substr(lastColonPos + 2);

            classInfo = findClass(className);
            if (classInfo) {
                // Check if it's a method
                auto methodIt = classInfo->methods.find(memberName);
                if (methodIt != classInfo->methods.end()) {
                    // Static method reference - use the method's return type
                    expressionTypes[&node] = methodIt->second.returnType;
                    expressionOwnerships[&node] = methodIt->second.returnOwnership;
                    return;
                }
                // Check if it's a property
                auto propIt = classInfo->properties.find(memberName);
                if (propIt != classInfo->properties.end()) {
                    expressionTypes[&node] = propIt->second.first;
                    expressionOwnerships[&node] = propIt->second.second;
                    return;
                }
                // Class exists but member not found - still set class type for static context
                expressionTypes[&node] = className;
                expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                return;
            }

            // Check if this is an enum value (EnumName::Value)
            // Try direct lookup first
            auto enumIt = enumRegistry_.find(className);
            std::string resolvedEnumName = className;

            // If not found, try with current namespace prefix
            if (enumIt == enumRegistry_.end() && !currentNamespace.empty()) {
                std::string qualifiedEnumName = currentNamespace + "::" + className;
                enumIt = enumRegistry_.find(qualifiedEnumName);
                if (enumIt != enumRegistry_.end()) {
                    resolvedEnumName = qualifiedEnumName;
                }
            }

            if (enumIt != enumRegistry_.end()) {
                // Verify the value exists in this enum
                const auto& enumInfo = enumIt->second;
                for (const auto& val : enumInfo.values) {
                    if (val.name == memberName) {
                        // Found enum value - type is the enum type, not the qualified value name
                        expressionTypes[&node] = resolvedEnumName;
                        expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                        return;
                    }
                }
            }
        }

        // Fallback: assume it's a valid qualified reference (namespace, intrinsic, etc.)
        // Use Deferred in template context, Unknown otherwise
        expressionTypes[&node] = inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE;
        expressionOwnerships[&node] = Parser::OwnershipType::Owned;
        return;
    }

    Symbol* sym = symbolTable_->resolve(node.name);

    if (!sym) {
        // Check if this is a namespace or class name (used as part of a qualified name)
        bool isValidClass = classRegistry_.find(node.name) != classRegistry_.end();

        // Also check with current namespace prefix (same-namespace class lookup)
        if (!isValidClass && !currentNamespace.empty()) {
            std::string qualifiedName = currentNamespace + "::" + node.name;
            if (classRegistry_.find(qualifiedName) != classRegistry_.end()) {
                isValidClass = true;
            }
            // Also check global class registry in CompilationContext
            if (!isValidClass && context_) {
                auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
                if (globalRegistry && globalRegistry->find(qualifiedName) != globalRegistry->end()) {
                    isValidClass = true;
                }
            }
        }

        // Check if this is an enumeration name
        bool isValidEnum = enumRegistry_.find(node.name) != enumRegistry_.end();
        if (!isValidEnum && !currentNamespace.empty()) {
            std::string qualifiedName = currentNamespace + "::" + node.name;
            isValidEnum = enumRegistry_.find(qualifiedName) != enumRegistry_.end();
        }

        if (isValidClass) {
            // It's a valid class name - set type to the class itself
            // Try to find the fully qualified name
            std::string resolvedClass = node.name;
            if (classRegistry_.find(node.name) == classRegistry_.end() && !currentNamespace.empty()) {
                std::string qualifiedName = currentNamespace + "::" + node.name;
                if (classRegistry_.find(qualifiedName) != classRegistry_.end()) {
                    resolvedClass = qualifiedName;
                }
            }
            expressionTypes[&node] = resolvedClass;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        if (isValidEnum) {
            // Enumeration name - set to the enum type
            std::string resolvedEnum = node.name;
            if (enumRegistry_.find(node.name) == enumRegistry_.end() && !currentNamespace.empty()) {
                std::string qualifiedName = currentNamespace + "::" + node.name;
                if (enumRegistry_.find(qualifiedName) != enumRegistry_.end()) {
                    resolvedEnum = qualifiedName;
                }
            }
            expressionTypes[&node] = resolvedEnum;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        if (validNamespaces_.find(node.name) != validNamespaces_.end()) {
            // It's a namespace name - type is the namespace itself (for qualified access)
            expressionTypes[&node] = node.name;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Check if it's a known intrinsic namespace (Syscall, System, Console, Mem, Language, IO, Test, etc.)
        if (node.name == "Syscall" ||
            node.name == "System" || node.name == "Console" || node.name == "Mem" ||
            node.name == "Language" || node.name == "IO" || node.name == "Test" ||
            node.name == "__typename") {
            // These are namespace/module names - type is the namespace
            expressionTypes[&node] = node.name;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Don't error on qualified names or template instantiations
        if (node.name.find("::") != std::string::npos) {
            // Already handled above for qualified names, but just in case
            expressionTypes[&node] = inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Check for template instantiation like List<Integer> or identity<Integer>
        if (node.name.find("<") != std::string::npos) {
            size_t angleStart = node.name.find('<');
            std::string baseName = node.name.substr(0, angleStart);
            size_t angleEnd = node.name.rfind('>');

            // Parse template arguments
            std::vector<Parser::TemplateArgument> args;
            if (angleEnd != std::string::npos && angleEnd > angleStart) {
                std::string argsStr = node.name.substr(angleStart + 1, angleEnd - angleStart - 1);
                std::vector<std::string> argStrings = parseTemplateArguments(argsStr);
                for (const auto& argStr : argStrings) {
                    args.emplace_back(argStr, node.location);
                }
            }

            // Check if the base name is a template class
            if (isTemplateClass(baseName)) {
                // Record the class template instantiation
                recordTemplateInstantiation(baseName, args);
                // Set type to the full instantiated type name
                expressionTypes[&node] = node.name;
                expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                return;
            }

            // Check if the base name is a registered template lambda
            if (templateLambdas_.find(baseName) != templateLambdas_.end()) {
                // Record the lambda template instantiation
                recordLambdaTemplateInstantiation(baseName, args);
                expressionTypes[&node] = node.name;
                expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                return;
            }

            // Unknown template type - set the name as the type for further resolution
            expressionTypes[&node] = node.name;
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Only error during validation phase
        if (enableValidation) {
            errorReporter.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Undeclared identifier '" + node.name + "'",
                node.location
            );
        }
        // Use Deferred in template context (might be resolved at instantiation)
        expressionTypes[&node] = inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE;
        expressionOwnerships[&node] = Parser::OwnershipType::Owned;
        return;
    }

    // Check if variable has been moved
    if (enableValidation) {
        checkVariableNotMoved(node.name, node.location);
    }

    // Register in TypeContext for C++ type understanding
    registerExpressionType(&node, sym->typeName, sym->ownership);
}

void SemanticAnalyzer::visit(Parser::ReferenceExpr& node) {
    node.expr->accept(*this);

    std::string exprType = getExpressionType(node.expr.get());
    expressionTypes[&node] = exprType;
    expressionOwnerships[&node] = Parser::OwnershipType::Reference;
}

void SemanticAnalyzer::visit(Parser::MemberAccessExpr& node) {
    node.object->accept(*this);

    std::string objectType = getExpressionType(node.object.get());

    // If member starts with "::", this is a static method call syntax (e.g., Integer::Constructor)
    // Build the qualified name for proper type resolution
    if (!node.member.empty() && node.member[0] == ':') {
        // Strip the leading :: from member
        std::string actualMember = node.member;
        if (actualMember.length() >= 2 && actualMember[0] == ':' && actualMember[1] == ':') {
            actualMember = actualMember.substr(2);
        }

        // Build qualified path: objectType::member
        std::string qualifiedPath = objectType + "::" + actualMember;

        // Check if this is a class reference
        ClassInfo* classInfo = findClass(qualifiedPath);
        if (classInfo) {
            registerExpressionType(&node, qualifiedPath, Parser::OwnershipType::Owned);
            return;
        }

        // Check if this is a namespace reference
        if (validNamespaces_.find(qualifiedPath) != validNamespaces_.end()) {
            registerExpressionType(&node, qualifiedPath, Parser::OwnershipType::Owned);
            return;
        }

        // Check if objectType is a class and member is a method
        classInfo = findClass(objectType);
        if (classInfo) {
            auto methodIt = classInfo->methods.find(actualMember);
            if (methodIt != classInfo->methods.end()) {
                registerExpressionType(&node, methodIt->second.returnType, methodIt->second.returnOwnership);
                return;
            }
        }

        // Check if objectType is an enum and member is an enum value
        // Try direct lookup first
        auto enumIt = enumRegistry_.find(objectType);
        std::string resolvedEnumType = objectType;

        // If not found, try with current namespace prefix
        if (enumIt == enumRegistry_.end() && !currentNamespace.empty()) {
            std::string qualifiedEnumName = currentNamespace + "::" + objectType;
            enumIt = enumRegistry_.find(qualifiedEnumName);
            if (enumIt != enumRegistry_.end()) {
                resolvedEnumType = qualifiedEnumName;
            }
        }

        if (enumIt != enumRegistry_.end()) {
            // Verify the value exists in this enum
            const auto& enumInfo = enumIt->second;
            for (const auto& val : enumInfo.values) {
                if (val.name == actualMember) {
                    // Found enum value - type is the enum type, not the qualified value name
                    registerExpressionType(&node, resolvedEnumType, Parser::OwnershipType::Owned);
                    return;
                }
            }
        }

        // For intrinsic static calls, set the qualified path as the type
        // CallExpr will handle determining the actual return type
        registerExpressionType(&node, qualifiedPath, Parser::OwnershipType::Owned);
        return;
    }

    // Strip ownership modifiers from object type (^, %, &)
    std::string baseType = objectType;
    if (!baseType.empty() && (baseType.back() == '^' ||
                               baseType.back() == '%' ||
                               baseType.back() == '&')) {
        baseType = baseType.substr(0, baseType.length() - 1);
    }

    // Handle template instantiation types (e.g., "Box<Integer>" -> "Box")
    // For template classes, we need to look up the base template class
    std::string lookupType = baseType;
    size_t angleBracket = baseType.find('<');
    if (angleBracket != std::string::npos) {
        lookupType = baseType.substr(0, angleBracket);
    }

    // Extract the actual member name (strip leading "." for instance access)
    std::string actualMember = node.member;
    if (!actualMember.empty() && actualMember[0] == '.') {
        actualMember = actualMember.substr(1);
    }

    // Look up the class in the registry
    ClassInfo* classInfo = findClass(lookupType);
    if (classInfo) {
        // Find the property
        auto propIt = classInfo->properties.find(actualMember);
        if (propIt != classInfo->properties.end()) {
            const auto& [propType, propOwnership] = propIt->second;
            registerExpressionType(&node, propType, propOwnership);
            return;
        }

        // Check if it's a method (for method reference expressions)
        auto methodIt = classInfo->methods.find(actualMember);
        if (methodIt != classInfo->methods.end()) {
            // This is a method reference, type is the return type (for callable)
            registerExpressionType(&node, methodIt->second.returnType, methodIt->second.returnOwnership);
            return;
        }
    }

    // If object type is a namespace, try to look up namespace::member as a class
    if (validNamespaces_.find(baseType) != validNamespaces_.end() ||
        baseType == "System" || baseType == "Language" || baseType == "IO" ||
        baseType == "Test" || baseType == "Syscall" || baseType == "Mem") {
        // Try qualified class lookup
        std::string qualifiedClass = baseType + "::" + actualMember;
        classInfo = findClass(qualifiedClass);
        if (classInfo) {
            // The member is a class within this namespace
            registerExpressionType(&node, qualifiedClass, Parser::OwnershipType::Owned);
            return;
        }
        // It might be a nested namespace, set type to the qualified name for further access
        std::string qualifiedNs = baseType + "::" + actualMember;
        if (validNamespaces_.find(qualifiedNs) != validNamespaces_.end()) {
            registerExpressionType(&node, qualifiedNs, Parser::OwnershipType::Owned);
            return;
        }
        // Try as a class in nested namespace pattern (e.g., Language::Collections)
        registerExpressionType(&node, qualifiedNs, Parser::OwnershipType::Owned);
        return;
    }

    // If object type is Unknown/Deferred, try to resolve the entire member access chain
    if (objectType == UNKNOWN_TYPE || objectType == DEFERRED_TYPE) {
        // Try to resolve the full chain to see if it's a class or namespace reference
        bool isClassRef = false;
        std::string resolvedName = resolveMemberAccessChain(&node, isClassRef);
        if (!resolvedName.empty()) {
            if (isClassRef) {
                registerExpressionType(&node, resolvedName, Parser::OwnershipType::Owned);
                return;
            }
            // Check if resolved name with member is a class
            std::string qualifiedWithMember = resolvedName;
            ClassInfo* resolved = findClass(qualifiedWithMember);
            if (resolved) {
                registerExpressionType(&node, qualifiedWithMember, Parser::OwnershipType::Owned);
                return;
            }
            // It's a namespace or partial resolution
            registerExpressionType(&node, resolvedName, Parser::OwnershipType::Owned);
            return;
        }
        // Can't resolve - use Deferred if in template definition, Unknown otherwise
        if (inTemplateDefinition) {
            registerExpressionType(&node, DEFERRED_TYPE, Parser::OwnershipType::Owned);
        } else {
            registerExpressionType(&node, UNKNOWN_TYPE, Parser::OwnershipType::Owned);
        }
        return;
    }

    // If object type is a template parameter, defer validation to instantiation
    if (templateTypeParameters.find(baseType) != templateTypeParameters.end() ||
        templateTypeParameters.find(lookupType) != templateTypeParameters.end()) {
        registerExpressionType(&node, DEFERRED_TYPE, Parser::OwnershipType::Owned);
        return;
    }

    // If class not found or property not found, try to preserve useful type info
    // instead of setting Unknown for valid-looking type references

    // For qualified names that look like class references (e.g., "System::Console")
    // keep the qualified path as the type - codegen can resolve these
    if (objectType.find("::") != std::string::npos ||
        (!objectType.empty() && std::isupper(objectType[0]))) {
        // Build a qualified path for method access: objectType.member
        std::string resultType = objectType;
        // If this looks like a method call on a qualified type, register the type
        // The actual return type will be determined by CallExpr
        registerExpressionType(&node, resultType, Parser::OwnershipType::Owned);
        return;
    }

    // For template instance types (e.g., "List<Integer>"), keep the type
    if (baseType.find('<') != std::string::npos) {
        registerExpressionType(&node, baseType, Parser::OwnershipType::Owned);
        return;
    }

    // Only report errors and set Unknown for truly unresolvable types
    if (enableValidation && !objectType.empty() && objectType != UNKNOWN_TYPE && objectType != DEFERRED_TYPE) {
        if (!classInfo) {
            errorReporter.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Cannot access member '" + actualMember + "' on unknown type '" + objectType + "'",
                node.location
            );
        } else {
            errorReporter.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Member '" + actualMember + "' not found on type '" + objectType + "'",
                node.location
            );
        }
    }
    // Use Deferred for template contexts, Unknown for actual errors
    if (inTemplateDefinition) {
        registerExpressionType(&node, DEFERRED_TYPE, Parser::OwnershipType::Owned);
    } else {
        registerExpressionType(&node, UNKNOWN_TYPE, Parser::OwnershipType::Owned);
    }
}

void SemanticAnalyzer::visit(Parser::CallExpr& node) {
    node.callee->accept(*this);

    for (auto& arg : node.arguments) {
        arg->accept(*this);
    }

    // Validate the method call
    std::string className;
    std::string methodName;

    // Build the full qualified name from the expression tree
    // This handles both IdentifierExpr and MemberAccessExpr chains
    std::string fullName = buildQualifiedName(node.callee.get());

    // DEBUG: Print the full name and parsing results
    if (!fullName.empty()) {
        DEBUG_OUT("DEBUG: fullName = '" << fullName << "'" << std::endl);
    }

    // Check if this is a qualified static call like Class::method()
    if (!fullName.empty()) {
        // Use template-aware parsing to handle names like "List<Integer>::Constructor"
        className = extractClassName(fullName);
        methodName = extractMethodName(fullName);

        DEBUG_OUT("DEBUG: className = '" << className << "', methodName = '" << methodName << "'" << std::endl);

        // Only treat it as a static call if we successfully extracted a class name
        if (!className.empty()) {
            // Check if this is a template instantiation (e.g., "Test::Box<Integer>")
            size_t angleBracketPos = className.find('<');
            if (angleBracketPos != std::string::npos) {
                // Extract template name and arguments
                std::string templateName = className.substr(0, angleBracketPos);
                DEBUG_OUT("DEBUG: Found template instantiation, templateName = '" << templateName << "'" << std::endl);

                // Check if it's a template class
                bool isTemplate = isTemplateClass(templateName);
                DEBUG_OUT("DEBUG: isTemplateClass('" << templateName << "') = " << isTemplate << std::endl);
                if (isTemplate) {
                    // Parse template arguments from the string using depth-aware parsing
                    size_t endBracket = className.rfind('>');
                    if (endBracket != std::string::npos) {
                        std::string argsStr = className.substr(angleBracketPos + 1, endBracket - angleBracketPos - 1);

                        // Use depth-aware parsing to handle nested templates like List<Box<Integer>>
                        std::vector<std::string> argStrings = parseTemplateArguments(argsStr);
                        std::vector<Parser::TemplateArgument> args;
                        for (const auto& argStr : argStrings) {
                            args.emplace_back(argStr, node.location);
                        }

                        // Record the template instantiation
                        recordTemplateInstantiation(templateName, args);
                    }
                }
            }

            // Validate the qualified identifier
            validateQualifiedIdentifier(className, node.location);

            // Look up the method
            MethodInfo* method = findMethod(className, methodName);

            // Only validate if validation is enabled
            if (enableValidation) {
                // Check if this is a template instantiation (contains <...>)
                bool isTemplateInstantiation = (className.find('<') != std::string::npos);

                // Only exempt Syscall, Mem, Console, Language namespaces, function types, and template instantiations from validation
                bool isIntrinsic = (className == "Syscall" || className == "Mem" ||
                                    className == "Console" || className == "System::Console" ||
                                    className == "__function" ||
                                    className.find("Language::") == 0);

                // Skip validation if the class is a template parameter (will be validated at instantiation)
                bool isTemplateParameter = (templateTypeParameters.find(className) != templateTypeParameters.end());

                // Special handling for function type .call() method
                bool isFunctionCall = (className == "__function" && methodName == "call");

                if (!method && !isIntrinsic && !isTemplateInstantiation && !isTemplateParameter && !isFunctionCall) {
                    errorReporter.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Method '" + methodName + "' not found in class '" + className + "'",
                        node.location
                    );
                    // Use Deferred in template context (method may exist on instantiated type)
                    registerExpressionType(&node, inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE, Parser::OwnershipType::Owned);
                    return;
                }
            }

            if (method) {
                // CRITICAL: Validate argument types match parameter types
                if (enableValidation) {
                    if (node.arguments.size() != method->parameters.size()) {
                        errorReporter.reportError(
                            Common::ErrorCode::InvalidMethodCall,
                            "Method '" + className + "::" + methodName + "' expects " +
                            std::to_string(method->parameters.size()) + " argument(s), but " +
                            std::to_string(node.arguments.size()) + " provided",
                            node.location
                        );
                    } else {
                        // Check each argument type
                        for (size_t i = 0; i < node.arguments.size(); ++i) {
                            std::string argType = getExpressionType(node.arguments[i].get());
                            const auto& [expectedType, expectedOwnership] = method->parameters[i];

                            // Check if this is a literal being passed where an object is expected
                            bool isLiteral = (dynamic_cast<Parser::IntegerLiteralExpr*>(node.arguments[i].get()) != nullptr ||
                                            dynamic_cast<Parser::StringLiteralExpr*>(node.arguments[i].get()) != nullptr ||
                                            dynamic_cast<Parser::BoolLiteralExpr*>(node.arguments[i].get()) != nullptr);

                            // CRITICAL: Check for literals being passed to pointer/reference types FIRST
                            // The ownership modifier is stored separately, so check expectedOwnership
                            bool expectsPointerType = (expectedOwnership == Parser::OwnershipType::Owned ||
                                                      expectedOwnership == Parser::OwnershipType::Reference);

                            // Allow integer literals for NativeType<float> and NativeType<double> (implicit conversion)
                            bool isIntegerLiteral = (dynamic_cast<Parser::IntegerLiteralExpr*>(node.arguments[i].get()) != nullptr);
                            bool expectsFloat = (expectedType.find("NativeType<\"float\">") != std::string::npos);
                            bool expectsDouble = (expectedType.find("NativeType<\"double\">") != std::string::npos);

                            if (isLiteral && expectsPointerType && !(isIntegerLiteral && (expectsFloat || expectsDouble))) {
                                errorReporter.reportError(
                                    Common::ErrorCode::TypeMismatch,
                                    "Argument " + std::to_string(i + 1) + " to method '" + methodName +
                                    "': Cannot pass literal value where '" + expectedType + "' is expected. " +
                                    "Did you mean to use '" + argType + "::Constructor(...)'?",
                                    node.arguments[i]->location
                                );
                                continue; // Skip further checks for this argument
                            }

                            // Strip ownership indicators from expected type for comparison
                            std::string baseExpectedType = expectedType;
                            if (!baseExpectedType.empty() && (baseExpectedType.back() == '^' ||
                                                              baseExpectedType.back() == '&' ||
                                                              baseExpectedType.back() == '%')) {
                                baseExpectedType = baseExpectedType.substr(0, baseExpectedType.length() - 1);
                            }

                            // Skip type checking if argument type is Deferred (template-dependent code)
                            if (argType == DEFERRED_TYPE || argType == UNKNOWN_TYPE) {
                                continue; // Can't validate template-dependent types until instantiation
                            }

                            // Check type compatibility
                            if (!isCompatibleType(baseExpectedType, argType)) {
                                errorReporter.reportError(
                                    Common::ErrorCode::TypeMismatch,
                                    "Argument " + std::to_string(i + 1) + " to method '" + methodName +
                                    "': Expected type '" + expectedType + "', but got '" + argType + "'",
                                    node.arguments[i]->location
                                );
                            }

                            // Check move semantics for owned parameters
                            if (expectedOwnership == Parser::OwnershipType::Owned) {
                                // If argument is an identifier, mark it as moved
                                if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.arguments[i].get())) {
                                    // Check if variable was already moved
                                    checkVariableNotMoved(identExpr->name, node.arguments[i]->location);
                                    // Mark variable as moved
                                    markVariableMoved(identExpr->name, node.arguments[i]->location);
                                }
                            }
                        }
                    }
                }

                // For constructors, return the fully-qualified class name (with template args if present)
                // instead of the generic return type from the method info
                if (methodName == "Constructor" || methodName == "constructor") {
                    registerExpressionType(&node, className, method->returnOwnership);
                } else {
                    registerExpressionType(&node, method->returnType, method->returnOwnership);
                }
            } else {
                // Check if this is a template instantiation constructor
                bool isTemplateInstantiation = (className.find('<') != std::string::npos);
                bool isConstructorCall = (methodName == "Constructor" || methodName == "constructor");

                // Check if this is an intrinsic namespace (Syscall, Mem, etc.)
                bool isIntrinsicNamespace = (className == "Syscall" || className == "Mem" ||
                                             className == "Console" || className == "System::Console" ||
                                             className == "__function" ||
                                             className.find("Language::") == 0);

                if (isTemplateInstantiation && isConstructorCall) {
                    // Template instantiation constructor - register the full type name
                    registerExpressionType(&node, className, Parser::OwnershipType::Owned);
                } else {
                    // Check intrinsic method registry for return type
                    std::string intrinsicKey = className + "::" + methodName;
                    auto it = intrinsicMethods_.find(intrinsicKey);
                    if (it != intrinsicMethods_.end()) {
                        registerExpressionType(&node, it->second.returnType, it->second.returnOwnership);
                    } else if (isConstructorCall) {
                        // Constructor call returns the class type
                        registerExpressionType(&node, className, Parser::OwnershipType::Owned);
                    } else if (inTemplateDefinition) {
                        // In template context - defer all unresolved types
                        registerExpressionType(&node, DEFERRED_TYPE, Parser::OwnershipType::Owned);
                    } else if (isIntrinsicNamespace) {
                        // Known intrinsic namespace method (runtime-provided) - use Void for side-effect calls
                        // This is used for Run statements where the return value is discarded
                        registerExpressionType(&node, "Void", Parser::OwnershipType::None);
                    } else {
                        // Unknown method - error
                        registerExpressionType(&node, UNKNOWN_TYPE, Parser::OwnershipType::Owned);
                    }
                }
            }
            return;
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        // obj.method() call
        std::string objectType = getExpressionType(memberExpr->object.get());
        methodName = memberExpr->member;

        // Strip ownership modifiers for type comparison (^, %, &)
        std::string baseObjectType = objectType;
        if (!baseObjectType.empty() && (baseObjectType.back() == '^' ||
                                         baseObjectType.back() == '%' ||
                                         baseObjectType.back() == '&')) {
            baseObjectType = baseObjectType.substr(0, baseObjectType.length() - 1);
        }

        // Special handling for ReflectionContext.getTargetValue() - returns the annotated element's type
        if (inProcessorContext_ && baseObjectType == "ReflectionContext" && methodName == "getTargetValue") {
            // Return type is the processor target type (set at annotation usage site)
            if (!processorTargetType_.empty()) {
                // We know the concrete type - use it
                registerExpressionType(&node, processorTargetType_, Parser::OwnershipType::Reference);
            } else {
                // During processor compilation, we don't know the target type yet.
                // Use "__DynamicValue" - a special type that allows any method call.
                // Actual type checking happens at runtime.
                registerExpressionType(&node, "__DynamicValue", Parser::OwnershipType::Reference);
            }
            return;
        }

        // Special handling for other ReflectionContext intrinsic methods
        if (inProcessorContext_ && baseObjectType == "ReflectionContext") {
            // Most methods return String^ or Integer^
            if (methodName == "getTargetKind" || methodName == "getTargetName" ||
                methodName == "getTypeName" || methodName == "getClassName" ||
                methodName == "getNamespaceName" || methodName == "getSourceFile" ||
                methodName == "getPropertyNameAt" || methodName == "getPropertyTypeAt" ||
                methodName == "getPropertyOwnershipAt" || methodName == "getMethodNameAt" ||
                methodName == "getMethodReturnTypeAt" || methodName == "getBaseClassName" ||
                methodName == "getParameterNameAt" || methodName == "getParameterTypeAt" ||
                methodName == "getReturnTypeName" || methodName == "getOwnership") {
                registerExpressionType(&node, "String", Parser::OwnershipType::Owned);
                return;
            }
            if (methodName == "getLineNumber" || methodName == "getColumnNumber" ||
                methodName == "getPropertyCount" || methodName == "getMethodCount" ||
                methodName == "getParameterCount" || methodName == "getTargetValueType" ||
                methodName == "getAnnotationArgCount") {
                registerExpressionType(&node, "Integer", Parser::OwnershipType::Owned);
                return;
            }
            if (methodName == "hasMethod" || methodName == "hasProperty" ||
                methodName == "isClassFinal" || methodName == "isMethodStatic" ||
                methodName == "hasDefaultValue" || methodName == "hasTargetValue") {
                registerExpressionType(&node, "Bool", Parser::OwnershipType::Owned);
                return;
            }
            // Type-aware annotation argument access: getAnnotationArg(name) returns the declared type
            if (methodName == "getAnnotationArg" && !currentAnnotationName_.empty()) {
                // Look up the annotation to get parameter types
                auto annotIt = annotationRegistry_.find(currentAnnotationName_);
                if (annotIt != annotationRegistry_.end()) {
                    // Try to get the parameter name from the argument (must be a constant string)
                    if (!node.arguments.empty()) {
                        // Check if first argument is a string literal or String::Constructor("...")
                        std::string paramName;
                        if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(node.arguments[0].get())) {
                            paramName = strLit->value;
                        } else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(node.arguments[0].get())) {
                            // String::Constructor("...")
                            if (!callExpr->arguments.empty()) {
                                if (auto* innerStr = dynamic_cast<Parser::StringLiteralExpr*>(callExpr->arguments[0].get())) {
                                    paramName = innerStr->value;
                                }
                            }
                        }

                        if (!paramName.empty()) {
                            // Find the parameter type in the annotation definition
                            for (const auto& param : annotIt->second.parameters) {
                                if (param.name == paramName) {
                                    registerExpressionType(&node, param.typeName, param.ownership);
                                    return;
                                }
                            }
                            // Parameter not found - report error
                            errorReporter.reportError(
                                Common::ErrorCode::UnknownArgument,
                                "Annotation '@" + currentAnnotationName_ + "' has no parameter named '" + paramName + "'",
                                node.location
                            );
                        }
                    }
                }
                // Fall through to generic handling if we couldn't determine the type
                registerExpressionType(&node, UNKNOWN_TYPE, Parser::OwnershipType::Owned);
                return;
            }

            // Legacy type-specific methods (for backwards compatibility and C API usage)
            if (methodName == "getAnnotationArgNameAt") {
                registerExpressionType(&node, "String", Parser::OwnershipType::Owned);
                return;
            }
        }

        // Special handling for CompilationContext intrinsic methods
        if (inProcessorContext_ && baseObjectType == "CompilationContext") {
            if (methodName == "message" || methodName == "warning" || methodName == "warningAt" ||
                methodName == "error" || methodName == "errorAt") {
                registerExpressionType(&node, "None", Parser::OwnershipType::None);
                return;
            }
            if (methodName == "hasErrors") {
                registerExpressionType(&node, "Bool", Parser::OwnershipType::Owned);
                return;
            }
        }

        if (objectType != UNKNOWN_TYPE && objectType != DEFERRED_TYPE) {
            // Check if this is a template instantiation (e.g., "List<Integer>")
            std::string baseType = objectType;
            std::unordered_map<std::string, std::string> templateSubstitutions;
            size_t angleBracketPos = objectType.find('<');

            if (angleBracketPos != std::string::npos) {
                // This is a template instantiation - extract base type and template args
                baseType = objectType.substr(0, angleBracketPos);

                // Find the template definition (check local first, then global)
                // ✅ SAFE: Use TemplateClassInfo pointer instead of raw ClassDecl pointer
                const TemplateClassInfo* templateInfo = nullptr;
                auto localIt = templateClasses.find(baseType);
                if (localIt != templateClasses.end()) {
                    templateInfo = &localIt->second;
                } else if (context_) {
                    auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
                    if (globalTemplates) {
                        auto globalIt = globalTemplates->find(baseType);
                        if (globalIt != globalTemplates->end()) {
                            templateInfo = &globalIt->second;
                        }
                    }
                }

                // ✅ SAFE: Access copied templateParams from TemplateClassInfo
                if (templateInfo) {
                    // Extract template arguments from the type string
                    size_t endBracket = objectType.rfind('>');
                    if (endBracket != std::string::npos) {
                        std::string argsStr = objectType.substr(angleBracketPos + 1, endBracket - angleBracketPos - 1);

                        // Parse template arguments (split by comma, respecting nested brackets)
                        std::vector<std::string> args;
                        size_t start = 0;
                        int depth = 0;
                        for (size_t i = 0; i < argsStr.size(); ++i) {
                            if (argsStr[i] == '<') depth++;
                            else if (argsStr[i] == '>') depth--;
                            else if (argsStr[i] == ',' && depth == 0) {
                                std::string arg = argsStr.substr(start, i - start);
                                size_t first = arg.find_first_not_of(" \t");
                                size_t last = arg.find_last_not_of(" \t");
                                if (first != std::string::npos) {
                                    args.push_back(arg.substr(first, last - first + 1));
                                }
                                start = i + 1;
                            }
                        }
                        // Add the last argument
                        std::string lastArg = argsStr.substr(start);
                        size_t first = lastArg.find_first_not_of(" \t");
                        size_t last = lastArg.find_last_not_of(" \t");
                        if (first != std::string::npos) {
                            args.push_back(lastArg.substr(first, last - first + 1));
                        }

                        // Build substitution map (T -> Integer, etc.)
                        for (size_t i = 0; i < templateInfo->templateParams.size() && i < args.size(); ++i) {
                            templateSubstitutions[templateInfo->templateParams[i].name] = args[i];
                        }
                    }
                }
            }

            // Check if this is a method template call (e.g., identity<Integer>)
            std::string actualMethodName = methodName;
            bool isMethodTemplateCall = false;
            std::vector<Parser::TemplateArgument> methodTemplateArgs;
            size_t methodAnglePos = methodName.find('<');

            if (methodAnglePos != std::string::npos) {
                // Extract base method name and template arguments
                std::string baseMethodName = methodName.substr(0, methodAnglePos);
                size_t endAngle = methodName.rfind('>');

                if (endAngle != std::string::npos && endAngle > methodAnglePos) {
                    std::string argsStr = methodName.substr(methodAnglePos + 1, endAngle - methodAnglePos - 1);

                    // Parse the template arguments (simple comma-separated list)
                    std::vector<std::string> argStrings;
                    size_t start = 0;
                    int depth = 0;
                    for (size_t i = 0; i < argsStr.size(); ++i) {
                        if (argsStr[i] == '<') depth++;
                        else if (argsStr[i] == '>') depth--;
                        else if (argsStr[i] == ',' && depth == 0) {
                            std::string arg = argsStr.substr(start, i - start);
                            // Trim whitespace
                            size_t first = arg.find_first_not_of(" \t");
                            size_t last = arg.find_last_not_of(" \t");
                            if (first != std::string::npos) {
                                argStrings.push_back(arg.substr(first, last - first + 1));
                            }
                            start = i + 1;
                        }
                    }
                    // Add the last argument
                    std::string lastArg = argsStr.substr(start);
                    size_t first = lastArg.find_first_not_of(" \t");
                    size_t last = lastArg.find_last_not_of(" \t");
                    if (first != std::string::npos) {
                        argStrings.push_back(lastArg.substr(first, last - first + 1));
                    }

                    // Convert to TemplateArguments
                    for (const auto& argStr : argStrings) {
                        methodTemplateArgs.emplace_back(argStr, node.location);
                    }

                    // Check if this is actually a template method
                    std::string methodKey = baseType + "::" + baseMethodName;
                    if (templateMethods.find(methodKey) != templateMethods.end()) {
                        isMethodTemplateCall = true;
                        actualMethodName = baseMethodName;

                        // Record the method template instantiation
                        // Pass objectType as the instantiated class name (e.g., "Holder<Integer>")
                        recordMethodTemplateInstantiation(baseType, objectType, baseMethodName, methodTemplateArgs);
                    }
                }
            }

            MethodInfo* method = findMethod(baseType, actualMethodName);

            // Only validate if validation is enabled
            if (enableValidation) {
                // Only exempt Syscall, Mem, Console, Language namespaces, function types from validation (intrinsic functions / stubs)
                // Also exempt __DynamicValue (used for processor target values with unknown types at compile time)
                bool isIntrinsic = (objectType == "Syscall" || objectType == "Mem" ||
                                    objectType == "Console" || objectType == "System::Console" ||
                                    objectType == "__function" || baseType == "__function" ||
                                    objectType == "__DynamicValue" || baseType == "__DynamicValue" ||
                                    objectType.find("Language::") == 0 || baseType.find("Language::") == 0);
                bool isTemplateInstantiation = !templateSubstitutions.empty() || isMethodTemplateCall;
                // Skip validation if the object is a template parameter (will be validated at instantiation)
                bool isTemplateParameter = (templateTypeParameters.find(baseType) != templateTypeParameters.end());

                // Special handling for function type .call() method
                bool isFunctionCall = ((objectType == "__function" || baseType == "__function") && methodName == "call");

                if (!method && !isIntrinsic && !isTemplateInstantiation && !isTemplateParameter && !isFunctionCall) {
                    errorReporter.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Method '" + methodName + "' not found in class '" + objectType + "'",
                        node.location
                    );
                    // Use Deferred in template context (method may exist on instantiated type)
                    registerExpressionType(&node, inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE, Parser::OwnershipType::Owned);
                    return;
                }
            }

            if (method) {
                // CRITICAL: Validate argument types match parameter types
                if (enableValidation) {
                    if (node.arguments.size() != method->parameters.size()) {
                        errorReporter.reportError(
                            Common::ErrorCode::InvalidMethodCall,
                            "Method '" + methodName + "' on type '" + objectType + "' expects " +
                            std::to_string(method->parameters.size()) + " argument(s), but " +
                            std::to_string(node.arguments.size()) + " provided",
                            node.location
                        );
                    } else {
                        // Check each argument type
                        for (size_t i = 0; i < node.arguments.size(); ++i) {
                            std::string argType = getExpressionType(node.arguments[i].get());
                            const auto& [expectedType, expectedOwnership] = method->parameters[i];

                            // Substitute template parameters in expected type
                            std::string substitutedExpectedType = expectedType;

                            // First, substitute class template parameters
                            for (const auto& [param, arg] : templateSubstitutions) {
                                // Handle direct match (T -> Integer)
                                if (substitutedExpectedType == param) {
                                    substitutedExpectedType = arg;
                                } else {
                                    // Handle parameterized types (Box<T> -> Box<Integer>)
                                    size_t pos = 0;
                                    while ((pos = substitutedExpectedType.find(param, pos)) != std::string::npos) {
                                        bool validStart = (pos == 0 || !std::isalnum(substitutedExpectedType[pos-1]));
                                        bool validEnd = (pos + param.length() == substitutedExpectedType.length() ||
                                                       !std::isalnum(substitutedExpectedType[pos + param.length()]));
                                        if (validStart && validEnd) {
                                            substitutedExpectedType.replace(pos, param.length(), arg);
                                            pos += arg.length();
                                        } else {
                                            pos++;
                                        }
                                    }
                                }
                            }

                            // Then, substitute method template parameters
                            if (isMethodTemplateCall && !methodTemplateArgs.empty()) {
                                std::string methodKey = baseType + "::" + actualMethodName;
                                auto methodIt = templateMethods.find(methodKey);
                                if (methodIt != templateMethods.end()) {
                                    const auto& methodInfo = methodIt->second;
                                    for (size_t j = 0; j < methodInfo.templateParams.size() && j < methodTemplateArgs.size(); ++j) {
                                        const std::string& paramName = methodInfo.templateParams[j].name;
                                        const std::string& argTypeStr = methodTemplateArgs[j].typeArg;
                                        if (substitutedExpectedType == paramName) {
                                            substitutedExpectedType = argTypeStr;
                                        } else {
                                            // Handle parameterized types (Box<U> -> Box<String>)
                                            size_t pos = 0;
                                            while ((pos = substitutedExpectedType.find(paramName, pos)) != std::string::npos) {
                                                bool validStart = (pos == 0 || !std::isalnum(substitutedExpectedType[pos-1]));
                                                bool validEnd = (pos + paramName.length() == substitutedExpectedType.length() ||
                                                               !std::isalnum(substitutedExpectedType[pos + paramName.length()]));
                                                if (validStart && validEnd) {
                                                    substitutedExpectedType.replace(pos, paramName.length(), argTypeStr);
                                                    pos += argTypeStr.length();
                                                } else {
                                                    pos++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Check if this is a literal being passed where an object is expected
                            bool isLiteral = (dynamic_cast<Parser::IntegerLiteralExpr*>(node.arguments[i].get()) != nullptr ||
                                            dynamic_cast<Parser::StringLiteralExpr*>(node.arguments[i].get()) != nullptr ||
                                            dynamic_cast<Parser::BoolLiteralExpr*>(node.arguments[i].get()) != nullptr);

                            // CRITICAL: Check for literals being passed to pointer/reference types FIRST
                            // The ownership modifier is stored separately, so check expectedOwnership
                            bool expectsPointerType = (expectedOwnership == Parser::OwnershipType::Owned ||
                                                      expectedOwnership == Parser::OwnershipType::Reference);

                            // Allow integer literals for NativeType<float> and NativeType<double> (implicit conversion)
                            bool isIntegerLiteral = (dynamic_cast<Parser::IntegerLiteralExpr*>(node.arguments[i].get()) != nullptr);
                            bool expectsFloat = (substitutedExpectedType.find("NativeType<\"float\">") != std::string::npos);
                            bool expectsDouble = (substitutedExpectedType.find("NativeType<\"double\">") != std::string::npos);

                            if (isLiteral && expectsPointerType && !(isIntegerLiteral && (expectsFloat || expectsDouble))) {
                                errorReporter.reportError(
                                    Common::ErrorCode::TypeMismatch,
                                    "Argument " + std::to_string(i + 1) + " to method '" + objectType + "." + methodName +
                                    "': Cannot pass literal value where '" + substitutedExpectedType + "' is expected. " +
                                    "Did you mean to use '" + argType + "::Constructor(...)'?",
                                    node.arguments[i]->location
                                );
                                continue; // Skip further checks for this argument
                            }

                            // Strip ownership indicators from expected type for comparison
                            std::string baseExpectedType = substitutedExpectedType;
                            if (!baseExpectedType.empty() && (baseExpectedType.back() == '^' ||
                                                              baseExpectedType.back() == '&' ||
                                                              baseExpectedType.back() == '%')) {
                                baseExpectedType = baseExpectedType.substr(0, baseExpectedType.length() - 1);
                            }

                            // Skip type checking if argument type is Deferred (template-dependent code)
                            if (argType == DEFERRED_TYPE || argType == UNKNOWN_TYPE) {
                                continue; // Can't validate template-dependent types until instantiation
                            }

                            // Check type compatibility
                            if (!isCompatibleType(baseExpectedType, argType)) {
                                errorReporter.reportError(
                                    Common::ErrorCode::TypeMismatch,
                                    "Argument " + std::to_string(i + 1) + " to method '" + objectType + "." + methodName +
                                    "': Expected type '" + substitutedExpectedType + "', but got '" + argType + "'",
                                    node.arguments[i]->location
                                );
                            }

                            // Check move semantics for owned parameters
                            if (expectedOwnership == Parser::OwnershipType::Owned) {
                                // If argument is an identifier, mark it as moved
                                if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.arguments[i].get())) {
                                    // Check if variable was already moved
                                    checkVariableNotMoved(identExpr->name, node.arguments[i]->location);
                                    // Mark variable as moved
                                    markVariableMoved(identExpr->name, node.arguments[i]->location);
                                }
                            }
                        }
                    }
                }

                // Substitute template parameters in return type
                std::string returnType = method->returnType;

                // First, substitute class template parameters
                for (const auto& [param, arg] : templateSubstitutions) {
                    // Handle direct match (T -> Integer)
                    if (returnType == param) {
                        returnType = arg;
                    } else {
                        // Handle parameterized types (FluentBox<T> -> FluentBox<Integer>)
                        size_t pos = 0;
                        while ((pos = returnType.find(param, pos)) != std::string::npos) {
                            bool validStart = (pos == 0 || !std::isalnum(returnType[pos-1]));
                            bool validEnd = (pos + param.length() == returnType.length() ||
                                           !std::isalnum(returnType[pos + param.length()]));
                            if (validStart && validEnd) {
                                returnType.replace(pos, param.length(), arg);
                                pos += arg.length();
                            } else {
                                pos++;
                            }
                        }
                    }
                }

                // Then, substitute method template parameters
                if (isMethodTemplateCall && !methodTemplateArgs.empty()) {
                    std::string methodKey = baseType + "::" + actualMethodName;
                    auto methodIt = templateMethods.find(methodKey);
                    if (methodIt != templateMethods.end()) {
                        const auto& methodInfo = methodIt->second;
                        for (size_t i = 0; i < methodInfo.templateParams.size() && i < methodTemplateArgs.size(); ++i) {
                            const std::string& paramName = methodInfo.templateParams[i].name;
                            const std::string& argType = methodTemplateArgs[i].typeArg;

                            // Handle direct match (T -> Integer)
                            if (returnType == paramName) {
                                returnType = argType;
                            } else {
                                // Handle parameterized types (Box<T> -> Box<Integer>)
                                size_t pos = 0;
                                while ((pos = returnType.find(paramName, pos)) != std::string::npos) {
                                    // Check it's a complete token (not part of larger identifier)
                                    bool validStart = (pos == 0 || !std::isalnum(returnType[pos-1]));
                                    bool validEnd = (pos + paramName.length() == returnType.length() ||
                                                   !std::isalnum(returnType[pos + paramName.length()]));
                                    if (validStart && validEnd) {
                                        returnType.replace(pos, paramName.length(), argType);
                                        pos += argType.length();
                                    } else {
                                        pos++;
                                    }
                                }
                            }
                        }
                    }
                }

                registerExpressionType(&node, returnType, method->returnOwnership);
            } else {
                // Check if this is a function type .call()
                bool isFunctionCall = ((objectType == "__function" || baseType == "__function") && methodName == "call");

                if (isFunctionCall && enableValidation) {
                    // Try to get function type parameter info from the callee
                    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(memberExpr->object.get())) {
                        auto* paramOwnerships = getFunctionTypeParams(identExpr->name);
                        if (paramOwnerships) {
                            // Validate arguments against function type parameters
                            size_t numParams = paramOwnerships->size();
                            if (node.arguments.size() != numParams) {
                                errorReporter.reportError(
                                    Common::ErrorCode::InvalidMethodCall,
                                    "Function '" + identExpr->name + "' expects " +
                                    std::to_string(numParams) + " argument(s), but " +
                                    std::to_string(node.arguments.size()) + " provided",
                                    node.location
                                );
                            } else {
                                // Check move semantics for owned parameters
                                for (size_t i = 0; i < node.arguments.size(); ++i) {
                                    if ((*paramOwnerships)[i] == Parser::OwnershipType::Owned) {
                                        if (auto* argIdent = dynamic_cast<Parser::IdentifierExpr*>(node.arguments[i].get())) {
                                            checkVariableNotMoved(argIdent->name, node.arguments[i]->location);
                                            markVariableMoved(argIdent->name, node.arguments[i]->location);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Special handling for __DynamicValue - return appropriate types for common methods
                if (objectType == "__DynamicValue" || baseType == "__DynamicValue") {
                    // Return type based on method name (common XXML type methods)
                    if (methodName == "toString" || methodName == "append" || methodName == "concat") {
                        registerExpressionType(&node, "String", Parser::OwnershipType::Owned);
                    } else if (methodName == "greaterThan" || methodName == "lessThan" ||
                               methodName == "equals" || methodName == "greaterThanOrEqual" ||
                               methodName == "lessThanOrEqual" || methodName == "isEmpty" ||
                               methodName == "contains" || methodName == "startsWith" ||
                               methodName == "endsWith") {
                        registerExpressionType(&node, "Bool", Parser::OwnershipType::Owned);
                    } else if (methodName == "add" || methodName == "subtract" ||
                               methodName == "multiply" || methodName == "divide" ||
                               methodName == "modulo" || methodName == "negate" ||
                               methodName == "length" || methodName == "size" ||
                               methodName == "hashCode" || methodName == "toInteger" ||
                               methodName == "compareTo") {
                        registerExpressionType(&node, "Integer", Parser::OwnershipType::Owned);
                    } else if (methodName == "toDouble") {
                        registerExpressionType(&node, "Double", Parser::OwnershipType::Owned);
                    } else {
                        // Unknown method on dynamic value - return __DynamicValue to allow chaining
                        registerExpressionType(&node, "__DynamicValue", Parser::OwnershipType::Owned);
                    }
                    return;
                }

                // Check intrinsic method registry for return type
                std::string intrinsicKey = baseType + "::" + methodName;
                auto it = intrinsicMethods_.find(intrinsicKey);

                // Check if this is an intrinsic namespace (Syscall, Mem, etc.)
                bool isIntrinsicNamespace = (baseType == "Syscall" || baseType == "Mem" ||
                                             baseType == "Console" || baseType == "System::Console" ||
                                             baseType == "__function" ||
                                             baseType.find("Language::") == 0);

                if (it != intrinsicMethods_.end()) {
                    registerExpressionType(&node, it->second.returnType, it->second.returnOwnership);
                } else if (methodName == "Constructor" || methodName == "constructor") {
                    // Constructor call returns the class type
                    registerExpressionType(&node, baseType, Parser::OwnershipType::Owned);
                } else if (inTemplateDefinition) {
                    // In template context - defer all unresolved types
                    registerExpressionType(&node, DEFERRED_TYPE, Parser::OwnershipType::Owned);
                } else if (isIntrinsicNamespace) {
                    // Known intrinsic namespace method (runtime-provided) - use Void for side-effect calls
                    registerExpressionType(&node, "Void", Parser::OwnershipType::None);
                } else {
                    // Unknown method - error
                    registerExpressionType(&node, UNKNOWN_TYPE, Parser::OwnershipType::Owned);
                }
            }
            return;
        }
    }

    // Default fallback - use Deferred in template context (resolved at instantiation)
    registerExpressionType(&node, inTemplateDefinition ? DEFERRED_TYPE : UNKNOWN_TYPE, Parser::OwnershipType::Owned);
}

void SemanticAnalyzer::visit(Parser::BinaryExpr& node) {
    if (node.left) {
        node.left->accept(*this);
    }

    node.right->accept(*this);

    // Type checking for binary operations
    if (node.left) {
        std::string leftType = getExpressionType(node.left.get());
        std::string rightType = getExpressionType(node.right.get());

        // Check if types are compatible (NativeType with numeric types is allowed)
        auto isNativeType = [](const std::string& type) {
            return type.find("NativeType<") == 0;
        };
        auto isNumericType = [](const std::string& type) {
            return type == "Integer" || type == "Double" || type == "Bool" ||
                   type.find("NativeType<") == 0;
        };

        bool typesCompatible = (leftType == rightType) ||
                               (isNativeType(leftType) && isNumericType(rightType)) ||
                               (isNumericType(leftType) && isNativeType(rightType));

        // For arithmetic operators, both sides should be compatible
        // Skip check for Unknown/Deferred types (template-dependent)
        if (!typesCompatible && leftType != UNKNOWN_TYPE && leftType != DEFERRED_TYPE &&
            rightType != UNKNOWN_TYPE && rightType != DEFERRED_TYPE) {
            errorReporter.reportWarning(
                Common::ErrorCode::TypeMismatch,
                "Type mismatch in binary operation: " + leftType + " " + node.op + " " + rightType,
                node.location
            );
        }

        // Comparison and logical operators return Bool
        if (node.op == "==" || node.op == "!=" ||
            node.op == "<" || node.op == ">" ||
            node.op == "<=" || node.op == ">=" ||
            node.op == "&&" || node.op == "||") {
            expressionTypes[&node] = "Bool";
        } else {
            // Arithmetic operators return the type of their operands
            expressionTypes[&node] = leftType;
        }
    } else {
        // Unary operation
        std::string rightType = getExpressionType(node.right.get());

        // Unary ! operator returns Bool
        if (node.op == "!") {
            expressionTypes[&node] = "Bool";
        } else {
            expressionTypes[&node] = rightType;
        }
    }

    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
}

void SemanticAnalyzer::visit(Parser::TypeRef& node) {
    // Record template instantiation if this type has template arguments
    if (!node.templateArgs.empty()) {
        recordTemplateInstantiation(node.typeName, node.templateArgs);
    }
}

std::string SemanticAnalyzer::resolveTypeArgToQualified(const std::string& typeArg) {
    // Handle nested templates recursively (e.g., List<List<Integer>>)
    size_t ltPos = typeArg.find('<');
    if (ltPos != std::string::npos) {
        std::string baseName = typeArg.substr(0, ltPos);
        std::string inner = typeArg.substr(ltPos + 1);
        // Remove trailing '>'
        if (!inner.empty() && inner.back() == '>') {
            inner.pop_back();
        }

        // Resolve base name
        std::string qualifiedBase = resolveTypeArgToQualified(baseName);

        // Parse and resolve inner arguments
        std::vector<std::string> innerArgs = parseTemplateArguments(inner);
        std::string resolvedInner;
        for (size_t i = 0; i < innerArgs.size(); ++i) {
            if (i > 0) resolvedInner += ", ";
            resolvedInner += resolveTypeArgToQualified(innerArgs[i]);
        }

        return qualifiedBase + "<" + resolvedInner + ">";
    }

    // If already qualified (contains ::), return as-is
    if (typeArg.find("::") != std::string::npos) {
        return typeArg;
    }

    // Primitive types should NOT be qualified - their constructors use simple names
    // (Bool_Constructor, Integer_Constructor, etc.) not qualified names
    // IMPORTANT: Check this BEFORE class lookup, because primitives are registered as classes
    static const std::unordered_set<std::string> primitives = {
        "Integer", "String", "Bool", "Float", "Double", "None", "Void",
        "Int", "Int8", "Int16", "Int32", "Int64", "Byte"
    };
    if (primitives.count(typeArg) > 0) {
        return typeArg;  // Return primitive types as-is
    }

    // Map processor intrinsic types to their actual implementations
    // These are special types understood by the semantic analyzer in processor context
    static const std::unordered_map<std::string, std::string> processorTypeAliases = {
        {"ReflectionContext", "Language::ProcessorCtx::ProcessorReflectionContext"},
        {"CompilationContext", "Language::ProcessorCtx::ProcessorCompilationContext"}
    };
    auto aliasIt = processorTypeAliases.find(typeArg);
    if (aliasIt != processorTypeAliases.end()) {
        return aliasIt->second;
    }

    // Try class lookup to get qualified name
    ClassInfo* info = findClass(typeArg);
    if (info && !info->qualifiedName.empty()) {
        return info->qualifiedName;
    }

    // Check template classes (may be a template class without instantiation)
    auto templateIt = templateClasses.find(typeArg);
    if (templateIt != templateClasses.end() && !templateIt->second.qualifiedName.empty()) {
        return templateIt->second.qualifiedName;
    }

    // Check global template registry
    if (context_) {
        auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
        if (globalTemplates) {
            auto globalIt = globalTemplates->find(typeArg);
            if (globalIt != globalTemplates->end() && !globalIt->second.qualifiedName.empty()) {
                return globalIt->second.qualifiedName;
            }
        }
    }

    // Return as-is - could be a template parameter like "T" that shouldn't be qualified
    return typeArg;
}

void SemanticAnalyzer::recordTemplateInstantiation(const std::string& templateName, const std::vector<Parser::TemplateArgument>& args) {
    // Check if this template class exists (check local first, then global)
    // ✅ SAFE: Use TemplateClassInfo pointer instead of raw ClassDecl pointer
    const TemplateClassInfo* templateInfo = nullptr;

    auto localIt = templateClasses.find(templateName);
    if (localIt != templateClasses.end()) {
        templateInfo = &localIt->second;
    } else if (context_) {
        // Check global registry
        auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
        if (globalTemplates) {
            auto globalIt = globalTemplates->find(templateName);
            if (globalIt != globalTemplates->end()) {
                templateInfo = &globalIt->second;
            }
        }
    }

    if (!templateInfo) {
        // Not a template class - maybe it's a regular class or error will be caught elsewhere
        return;
    }

    // Skip if any type argument is itself a template parameter (not a concrete type)
    // This prevents recording instantiations like HashMap<K, V> when K and V are unbound
    // template parameters (e.g., in self-referential return types within template class definitions)
    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Check if this type argument is a known template parameter
            bool isTemplateParam = false;

            // Check all registered template classes for matching parameter names
            for (const auto& [name, tplInfo] : templateClasses) {
                for (const auto& param : tplInfo.templateParams) {
                    if (param.name == arg.typeArg) {
                        isTemplateParam = true;
                        break;
                    }
                }
                if (isTemplateParam) break;
            }

            // Also check global template registry
            if (!isTemplateParam && context_) {
                auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, TemplateClassInfo>>("globalTemplateClasses");
                if (globalTemplates) {
                    for (const auto& [name, tplInfo] : *globalTemplates) {
                        for (const auto& param : tplInfo.templateParams) {
                            if (param.name == arg.typeArg) {
                                isTemplateParam = true;
                                break;
                            }
                        }
                        if (isTemplateParam) break;
                    }
                }
            }

            if (isTemplateParam) {
                // This is a self-reference with unbound template params - skip recording
                return;
            }
        }
    }

    // ✅ Phase 7: Improved error message for argument count mismatch
    // Validate argument count using copied templateParams
    if (templateInfo->templateParams.size() != args.size()) {
        // Build parameter list for better error message
        std::string paramList;
        for (size_t i = 0; i < templateInfo->templateParams.size(); ++i) {
            if (i > 0) paramList += ", ";
            paramList += templateInfo->templateParams[i].name;
        }

        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Template '" + templateName + "' expects " +
            std::to_string(templateInfo->templateParams.size()) +
            " argument(s) <" + paramList + "> but got " +
            std::to_string(args.size()),
            args.empty() ? Common::SourceLocation{} : args[0].location
        );
        return;
    }

    // Create instantiation record with QUALIFIED names
    TemplateInstantiation inst;
    // Qualify the template name itself
    inst.templateName = templateInfo->qualifiedName.empty() ? templateName : templateInfo->qualifiedName;

    // Qualify all type arguments before storing
    for (const auto& arg : args) {
        Parser::TemplateArgument qualifiedArg = arg;
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            qualifiedArg.typeArg = resolveTypeArgToQualified(arg.typeArg);
        }
        inst.arguments.push_back(qualifiedArg);
    }

    // Evaluate constant expressions for non-type parameters
    // ✅ SAFE: Access copied templateParams from TemplateClassInfo
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto& param = templateInfo->templateParams[i];

        // ✅ Phase 7: Improved wildcard error message
        // Wildcard can match any type parameter
        if (arg.kind == Parser::TemplateArgument::Kind::Wildcard) {
            if (param.kind != Parser::TemplateParameter::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Wildcard '?' can only be used for type parameters, not value parameters.\n"
                    "  Template parameter '" + param.name + "' expects a constant value (e.g., 10, 42)",
                    arg.location
                );
            }
            // Wildcard is valid, continue
            continue;
        }

        // ✅ Phase 7: Improved type/value mismatch error messages
        // Validate argument kind matches parameter kind
        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            if (arg.kind != Parser::TemplateArgument::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Template parameter '" + param.name + "' expects a type argument (e.g., Integer, String), but got a value.\n"
                    "  Usage: " + templateName + "<" + param.name + "=SomeType>",
                    arg.location
                );
            } else {
                // ✅ Phase 7: Improved constraint validation error message
                // Validate type constraints
                if (!validateConstraint(arg.typeArg, param.constraints, param.constraintsAreAnd)) {
                    // Build constraint list for error message
                    std::string constraintList;
                    std::string separator = param.constraintsAreAnd ? ", " : " | ";
                    for (size_t j = 0; j < param.constraints.size(); ++j) {
                        if (j > 0) constraintList += separator;
                        constraintList += param.constraints[j].toString();
                    }
                    std::string semanticsMsg = param.constraintsAreAnd
                        ? "  Constraint uses AND semantics - type must satisfy ALL constraints."
                        : "  Constraint uses OR semantics - type must satisfy at least one option.";
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        "Type '" + arg.typeArg + "' does not satisfy constraint for template parameter '" + param.name + "'.\n"
                        "  Required: " + constraintList + "\n"
                        "  Provided: " + arg.typeArg + "\n" + semanticsMsg,
                        arg.location
                    );
                }
            }
        } else {  // Non-type parameter
            if (arg.kind != Parser::TemplateArgument::Kind::Value) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Template parameter '" + param.name + "' expects a constant value (e.g., 10, 42), but got a type.\n"
                    "  Usage: " + templateName + "<" + param.name + "=42>",
                    arg.location
                );
            } else {
                // Evaluate the constant expression
                int64_t value = evaluateConstantExpression(arg.valueArg.get());
                inst.evaluatedValues.push_back(value);
            }
        }
    }

    // Add to instantiations set
    templateInstantiations.insert(std::move(inst));
}

int64_t SemanticAnalyzer::evaluateConstantExpression(Parser::Expression* expr) {
    if (!expr) return 0;

    // Integer literal
    if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        return intLit->value;
    }

    // Binary expression (arithmetic)
    if (auto* binExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        int64_t left = evaluateConstantExpression(binExpr->left.get());
        int64_t right = evaluateConstantExpression(binExpr->right.get());

        if (binExpr->op == "+") return left + right;
        if (binExpr->op == "-") return left - right;
        if (binExpr->op == "*") return left * right;
        if (binExpr->op == "/") {
            if (right == 0) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Division by zero in constant expression",
                    binExpr->location
                );
                return 0;
            }
            return left / right;
        }
        if (binExpr->op == "%") {
            if (right == 0) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Modulo by zero in constant expression",
                    binExpr->location
                );
                return 0;
            }
            return left % right;
        }

        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Unsupported operator '" + binExpr->op + "' in constant expression",
            binExpr->location
        );
        return 0;
    }

    // Identifier - could be a named constant (not yet supported)
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Constant expression must be a compile-time constant, not identifier '" + ident->name + "'",
            expr->location
        );
        return 0;
    }

    errorReporter.reportError(
        Common::ErrorCode::TypeMismatch,
        "Expression is not a valid constant expression",
        expr->location
    );
    return 0;
}

bool SemanticAnalyzer::isTemplateClass(const std::string& className) {
    // Check local registry first
    if (templateClasses.find(className) != templateClasses.end()) {
        return true;
    }

    // Check global registry for cross-module templates
    if (context_) {
        auto* globalTemplates = context_->getCustomData<std::unordered_map<std::string, Parser::ClassDecl*>>("globalTemplateClasses");
        if (globalTemplates && globalTemplates->find(className) != globalTemplates->end()) {
            return true;
        }
    }

    return false;
}

SemanticAnalyzer::ClassInfo* SemanticAnalyzer::findClass(const std::string& className) {
    // Try exact match first in local registry
    auto it = classRegistry_.find(className);
    if (it != classRegistry_.end()) {
        return &it->second;
    }

    // Try with current namespace prefix if not qualified
    if (className.find("::") == std::string::npos && !currentNamespace.empty()) {
        std::string qualifiedName = currentNamespace + "::" + className;
        it = classRegistry_.find(qualifiedName);
        if (it != classRegistry_.end()) {
            return &it->second;
        }
    }

    // Try each imported namespace if not qualified - check for ambiguity
    if (className.find("::") == std::string::npos) {
        std::vector<std::pair<std::string, ClassInfo*>> matches;
        for (const auto& importedNs : importedNamespaces_) {
            std::string qualifiedName = importedNs + "::" + className;
            it = classRegistry_.find(qualifiedName);
            if (it != classRegistry_.end()) {
                matches.push_back({qualifiedName, &it->second});
            }
        }

        // If multiple matches found, report ambiguity error
        if (matches.size() > 1) {
            std::string errorMsg = "Ambiguous type reference '" + className + "'. Could be:\n";
            for (const auto& match : matches) {
                errorMsg += "  - " + match.first + "\n";
            }
            errorMsg += "Use fully qualified name or remove conflicting #import statements.";
            errorReporter.reportError(
                Common::ErrorCode::TypeMismatch,
                errorMsg,
                Common::SourceLocation{}  // Will be improved when we have location context
            );
            return nullptr;
        } else if (matches.size() == 1) {
            return matches[0].second;
        }
    }

    // Try without namespace prefix (for fully qualified lookups)
    size_t lastColon = className.rfind("::");
    if (lastColon != std::string::npos) {
        std::string baseName = className.substr(lastColon + 2);
        it = classRegistry_.find(baseName);
        if (it != classRegistry_.end()) {
            return &it->second;
        }
    }

    // Try all registered classes in local registry - match by simple name if nothing else works
    for (auto& entry : classRegistry_) {
        // Extract simple name from qualified name
        std::string entrySimpleName = entry.first;
        size_t pos = entry.first.rfind("::");
        if (pos != std::string::npos) {
            entrySimpleName = entry.first.substr(pos + 2);
        }

        // Match simple names
        std::string searchSimpleName = className;
        pos = className.rfind("::");
        if (pos != std::string::npos) {
            searchSimpleName = className.substr(pos + 2);
        }

        if (entrySimpleName == searchSimpleName) {
            return &entry.second;
        }
    }

    // ✅ FIX: If not found locally, search in global registry shared across all analyzers
    if (context_) {
        auto* globalRegistry = context_->getCustomData<std::unordered_map<std::string, ClassInfo>>("globalClassRegistry");
        if (globalRegistry) {
            // Try exact match in global registry
            auto git = globalRegistry->find(className);
            if (git != globalRegistry->end()) {
                return &git->second;
            }

            // Try with current namespace prefix
            if (className.find("::") == std::string::npos && !currentNamespace.empty()) {
                std::string qualifiedName = currentNamespace + "::" + className;
                git = globalRegistry->find(qualifiedName);
                if (git != globalRegistry->end()) {
                    return &git->second;
                }
            }

            // Try each imported namespace in global registry - check for ambiguity
            if (className.find("::") == std::string::npos) {
                std::vector<std::pair<std::string, ClassInfo*>> matches;
                for (const auto& importedNs : importedNamespaces_) {
                    std::string qualifiedName = importedNs + "::" + className;
                    git = globalRegistry->find(qualifiedName);
                    if (git != globalRegistry->end()) {
                        matches.push_back({qualifiedName, &git->second});
                    }
                }

                // If multiple matches found, report ambiguity error
                if (matches.size() > 1) {
                    std::string errorMsg = "Ambiguous type reference '" + className + "'. Could be:\n";
                    for (const auto& match : matches) {
                        errorMsg += "  - " + match.first + "\n";
                    }
                    errorMsg += "Use fully qualified name or remove conflicting #import statements.";
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        errorMsg,
                        Common::SourceLocation{}
                    );
                    return nullptr;
                } else if (matches.size() == 1) {
                    return matches[0].second;
                }
            }

            // Try without namespace prefix
            if (lastColon != std::string::npos) {
                std::string baseName = className.substr(lastColon + 2);
                git = globalRegistry->find(baseName);
                if (git != globalRegistry->end()) {
                    return &git->second;
                }
            }

            // Try matching by simple name in global registry
            for (auto& entry : *globalRegistry) {
                std::string entrySimpleName = entry.first;
                size_t pos = entry.first.rfind("::");
                if (pos != std::string::npos) {
                    entrySimpleName = entry.first.substr(pos + 2);
                }

                std::string searchSimpleName = className;
                pos = className.rfind("::");
                if (pos != std::string::npos) {
                    searchSimpleName = className.substr(pos + 2);
                }

                if (entrySimpleName == searchSimpleName) {
                    return &entry.second;
                }
            }
        }
    }

    return nullptr;
}

SemanticAnalyzer::MethodInfo* SemanticAnalyzer::findMethod(const std::string& className, const std::string& methodName) {
    ClassInfo* classInfo = findClass(className);
    if (!classInfo) {
        return nullptr;
    }

    auto it = classInfo->methods.find(methodName);
    if (it != classInfo->methods.end()) {
        return &it->second;
    }

    return nullptr;
}

std::string SemanticAnalyzer::resolveMemberAccessChain(Parser::Expression* expr, bool& isClassReference) {
    isClassReference = false;

    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        // Check if identifier is a class
        if (findClass(ident->name)) {
            isClassReference = true;
            return ident->name;
        }
        // Check if identifier is a namespace
        if (validNamespaces_.find(ident->name) != validNamespaces_.end()) {
            return ident->name;
        }
        // Check intrinsic namespaces
        if (ident->name == "System" || ident->name == "Language" || ident->name == "IO" ||
            ident->name == "Test" || ident->name == "Syscall" || ident->name == "Mem" ||
            ident->name == "Console") {
            return ident->name;
        }
        return ident->name;
    }

    if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        bool objectIsClass = false;
        std::string objectName = resolveMemberAccessChain(memberExpr->object.get(), objectIsClass);
        if (objectName.empty()) return "";

        std::string qualified = objectName + "::" + memberExpr->member;

        // Check if qualified name is a class
        if (findClass(qualified)) {
            isClassReference = true;
            return qualified;
        }

        // Check if qualified name is a namespace
        if (validNamespaces_.find(qualified) != validNamespaces_.end()) {
            return qualified;
        }

        // If object was a namespace, this might be a class within it
        // Return the qualified name for further resolution
        return qualified;
    }

    return "";
}

bool SemanticAnalyzer::validateQualifiedIdentifier(const std::string& qualifiedName, const Common::SourceLocation& loc) {
    // Split by :: to get components, but don't split inside template arguments < >
    std::vector<std::string> components;
    size_t start = 0;
    int angleBracketDepth = 0;

    for (size_t i = 0; i < qualifiedName.length(); ++i) {
        if (qualifiedName[i] == '<') {
            angleBracketDepth++;
        } else if (qualifiedName[i] == '>') {
            angleBracketDepth--;
        } else if (angleBracketDepth == 0 && i + 1 < qualifiedName.length() &&
                   qualifiedName[i] == ':' && qualifiedName[i + 1] == ':') {
            // Found :: outside of template arguments
            components.push_back(qualifiedName.substr(start, i - start));
            start = i + 2;
            i++; // Skip the second ':'
        }
    }
    // Add the last component
    if (start < qualifiedName.length()) {
        components.push_back(qualifiedName.substr(start));
    }

    if (components.empty()) {
        return false;
    }

    // Check if the first component is a valid namespace or class
    if (components.size() == 1) {
        // Single identifier - check if it's a class, namespace, or variable
        if (classRegistry_.find(components[0]) != classRegistry_.end()) {
            return true;
        }
        if (validNamespaces_.find(components[0]) != validNamespaces_.end()) {
            return true;
        }
        // Check symbol table for variables/parameters
        Symbol* sym = symbolTable_->resolve(components[0]);
        return sym != nullptr;
    } else {
        // Multi-part identifier like System::Console or Language::Core::String
        // Build up the qualified name and check at each level
        std::string accumulated;
        for (size_t i = 0; i < components.size() - 1; ++i) {
            if (i > 0) accumulated += "::";
            accumulated += components[i];

            // Check if this is a valid namespace or class
            bool isIntrinsic = (accumulated == "System" || accumulated == "System::Console" ||
                                accumulated == "Syscall" ||
                                accumulated == "Mem" || accumulated == "Language::Core::Mem" ||
                                accumulated == "Language" || accumulated == "Language::Core" ||
                                accumulated == "Language::Reflection" || accumulated == "Language::Collections" ||
                                accumulated == "Language::System" || accumulated == "IO" ||
                                accumulated == "Language::IO" || accumulated == "Language::Concurrent" ||
                                accumulated == "Test" || accumulated == "Language::Test");

            if (!isIntrinsic &&
                validNamespaces_.find(accumulated) == validNamespaces_.end() &&
                classRegistry_.find(accumulated) == classRegistry_.end()) {
                // This component is not a known namespace or class
                errorReporter.reportError(
                    Common::ErrorCode::UndeclaredIdentifier,
                    "Undefined namespace or class '" + accumulated + "' in qualified name '" + qualifiedName + "'",
                    loc
                );
                return false;
            }
        }
        return true;
    }
}

std::string SemanticAnalyzer::buildQualifiedName(Parser::Expression* expr) {
    // Recursively build qualified name from expression tree
    // Only works for static calls (::), not instance calls (.)
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        DEBUG_OUT("DEBUG buildQualifiedName: IdentifierExpr = '" << identExpr->name << "'" << std::endl);
        return identExpr->name;
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        std::string member = memberExpr->member;
        DEBUG_OUT("DEBUG buildQualifiedName: MemberAccessExpr member = '" << member << "'" << std::endl);

        // Check if this is a static call (member starts with ::)
        if (member.length() >= 2 && member[0] == ':' && member[1] == ':') {
            // Remove :: prefix
            member = member.substr(2);

            // Recursively get the object name
            std::string objectName = buildQualifiedName(memberExpr->object.get());

            std::string result = objectName.empty() ? member : (objectName + "::" + member);
            DEBUG_OUT("DEBUG buildQualifiedName: returning '" << result << "'" << std::endl);
            return result;
        } else {
            // This is an instance call (dot access), not a static call
            DEBUG_OUT("DEBUG buildQualifiedName: instance call, returning empty" << std::endl);
            return "";
        }
    }
    DEBUG_OUT("DEBUG buildQualifiedName: unknown expression type, returning empty" << std::endl);
    return "";
}

std::string SemanticAnalyzer::extractClassName(const std::string& qualifiedName) {
    // Find the rightmost :: that's NOT inside template brackets
    int bracketDepth = 0;
    size_t lastColonPos = std::string::npos;

    for (size_t i = 0; i < qualifiedName.length() - 1; i++) {
        if (qualifiedName[i] == '<') {
            bracketDepth++;
        } else if (qualifiedName[i] == '>') {
            bracketDepth--;
        } else if (bracketDepth == 0 && qualifiedName[i] == ':' && qualifiedName[i+1] == ':') {
            lastColonPos = i;
        }
    }

    if (lastColonPos != std::string::npos) {
        return qualifiedName.substr(0, lastColonPos);
    }
    return "";
}

std::string SemanticAnalyzer::extractMethodName(const std::string& qualifiedName) {
    // Find the rightmost :: that's NOT inside template brackets
    int bracketDepth = 0;
    size_t lastColonPos = std::string::npos;

    for (size_t i = 0; i < qualifiedName.length() - 1; i++) {
        if (qualifiedName[i] == '<') {
            bracketDepth++;
        } else if (qualifiedName[i] == '>') {
            bracketDepth--;
        } else if (bracketDepth == 0 && qualifiedName[i] == ':' && qualifiedName[i+1] == ':') {
            lastColonPos = i;
        }
    }

    if (lastColonPos != std::string::npos) {
        return qualifiedName.substr(lastColonPos + 2);
    }
    return qualifiedName;  // No :: found, return the whole name
}

// Helper to get literal type directly (without relying on expressionTypes map)
static std::string getLiteralType(Parser::Expression* expr) {
    if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) return "Integer";
    if (dynamic_cast<Parser::StringLiteralExpr*>(expr)) return "String";
    if (dynamic_cast<Parser::BoolLiteralExpr*>(expr)) return "Bool";
    if (dynamic_cast<Parser::FloatLiteralExpr*>(expr)) return "Float";
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        // Could be a variable, but we don't know the type without context
        return UNKNOWN_TYPE;
    }
    return UNKNOWN_TYPE;
}

// Extract call expressions from an expression tree
void SemanticAnalyzer::extractCallsFromExpression(Parser::Expression* expr,
                                                   const std::set<std::string>& templateParams,
                                                   std::vector<TemplateBodyCallInfo>& calls) {
    if (!expr) return;

    // Check if this is a CallExpr
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        std::string fullName = buildQualifiedName(callExpr->callee.get());
        if (!fullName.empty()) {
            std::string className = extractClassName(fullName);
            std::string methodName = extractMethodName(fullName);

            // Only extract if the class name is a template parameter
            if (templateParams.find(className) != templateParams.end()) {
                TemplateBodyCallInfo callInfo;
                callInfo.className = className;
                callInfo.methodName = methodName;

                // Extract argument types (use literal type detection since expressionTypes isn't populated yet)
                for (auto& arg : callExpr->arguments) {
                    std::string argType = getLiteralType(arg.get());
                    if (argType == UNKNOWN_TYPE || argType == DEFERRED_TYPE) {
                        // Try the normal method in case it was already visited
                        argType = getExpressionType(arg.get());
                    }
                    callInfo.argumentTypes.push_back(argType);
                }

                calls.push_back(callInfo);
            }
        }

        // Also process arguments recursively
        for (auto& arg : callExpr->arguments) {
            extractCallsFromExpression(arg.get(), templateParams, calls);
        }
    }
    // Handle other expression types that might contain nested calls
    else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        extractCallsFromExpression(memberExpr->object.get(), templateParams, calls);
    }
    else if (auto* binExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        extractCallsFromExpression(binExpr->left.get(), templateParams, calls);
        extractCallsFromExpression(binExpr->right.get(), templateParams, calls);
    }
}

// Extract call expressions from a statement
void SemanticAnalyzer::extractCallsFromStatement(Parser::Statement* stmt,
                                                  const std::set<std::string>& templateParams,
                                                  std::vector<TemplateBodyCallInfo>& calls) {
    if (!stmt) return;

    if (auto* returnStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            extractCallsFromExpression(returnStmt->value.get(), templateParams, calls);
        }
    }
    else if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        if (runStmt->expression) {
            extractCallsFromExpression(runStmt->expression.get(), templateParams, calls);
        }
    }
    else if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        if (instStmt->initializer) {
            extractCallsFromExpression(instStmt->initializer.get(), templateParams, calls);
        }
    }
    else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        extractCallsFromExpression(ifStmt->condition.get(), templateParams, calls);
        for (auto& s : ifStmt->thenBranch) {
            extractCallsFromStatement(s.get(), templateParams, calls);
        }
        for (auto& s : ifStmt->elseBranch) {
            extractCallsFromStatement(s.get(), templateParams, calls);
        }
    }
    else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        extractCallsFromExpression(whileStmt->condition.get(), templateParams, calls);
        for (auto& s : whileStmt->body) {
            extractCallsFromStatement(s.get(), templateParams, calls);
        }
    }
    else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        if (forStmt->rangeStart) extractCallsFromExpression(forStmt->rangeStart.get(), templateParams, calls);
        if (forStmt->rangeEnd) extractCallsFromExpression(forStmt->rangeEnd.get(), templateParams, calls);
        for (auto& s : forStmt->body) {
            extractCallsFromStatement(s.get(), templateParams, calls);
        }
    }
}

// Main entry point for extracting calls from method body
std::vector<SemanticAnalyzer::TemplateBodyCallInfo> SemanticAnalyzer::extractCallsFromMethodBody(
    Parser::MethodDecl* method,
    const std::vector<Parser::TemplateParameter>& templateParams) {

    std::vector<TemplateBodyCallInfo> calls;
    if (!method) return calls;

    // Build set of template parameter names
    std::set<std::string> templateParamNames;
    for (const auto& param : templateParams) {
        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            templateParamNames.insert(param.name);
        }
    }

    // Walk all statements in the body
    for (auto& stmt : method->body) {
        extractCallsFromStatement(stmt.get(), templateParamNames, calls);
    }

    return calls;
}

void SemanticAnalyzer::recordMethodTemplateInstantiation(const std::string& className, const std::string& instantiatedClassName, const std::string& methodName, const std::vector<Parser::TemplateArgument>& args) {
    std::string methodKey = className + "::" + methodName;

    // Check if this template method exists
    auto it = templateMethods.find(methodKey);
    if (it == templateMethods.end()) {
        // Not a template method - might be a regular method or error caught elsewhere
        return;
    }

    // ✅ SAFE: Access TemplateMethodInfo struct (copied data)
    const TemplateMethodInfo& methodInfo = it->second;

    // Validate argument count using copied templateParams
    if (methodInfo.templateParams.size() != args.size()) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Method template '" + methodName + "' expects " +
            std::to_string(methodInfo.templateParams.size()) +
            " arguments but got " + std::to_string(args.size()),
            args.empty() ? Common::SourceLocation{} : args[0].location
        );
        return;
    }

    // Create instantiation record with QUALIFIED type arguments
    MethodTemplateInstantiation inst;
    inst.className = className;
    inst.instantiatedClassName = instantiatedClassName;
    inst.methodName = methodName;

    // Qualify all type arguments before storing
    for (const auto& arg : args) {
        Parser::TemplateArgument qualifiedArg = arg;
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            qualifiedArg.typeArg = resolveTypeArgToQualified(arg.typeArg);
        }
        inst.arguments.push_back(qualifiedArg);
    }

    // Evaluate constant expressions for non-type parameters
    // ✅ SAFE: Access copied templateParams from TemplateMethodInfo
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto& param = methodInfo.templateParams[i];

        // Wildcard can match any type parameter
        if (arg.kind == Parser::TemplateArgument::Kind::Wildcard) {
            if (param.kind != Parser::TemplateParameter::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Wildcard '?' can only be used for type parameters, not value parameters",
                    arg.location
                );
            }
            // Wildcard is valid, continue
            continue;
        }

        // Validate argument kind matches parameter kind
        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            if (arg.kind != Parser::TemplateArgument::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Method template parameter '" + param.name + "' expects a type, but got a value",
                    arg.location
                );
            } else {
                // Validate type constraints
                if (!validateConstraint(arg.typeArg, param.constraints, param.constraintsAreAnd)) {
                    // Build constraint list for error message
                    std::string constraintList;
                    std::string separator = param.constraintsAreAnd ? ", " : " | ";
                    for (size_t j = 0; j < param.constraints.size(); ++j) {
                        if (j > 0) constraintList += separator;
                        constraintList += param.constraints[j].toString();
                    }
                    std::string semanticsMsg = param.constraintsAreAnd
                        ? " (must satisfy ALL)" : " (must satisfy at least one)";
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        "Type '" + arg.typeArg + "' does not satisfy constraint '" +
                        constraintList + "'" + semanticsMsg + " for method template parameter '" + param.name + "'",
                        arg.location
                    );
                }
            }
        } else {  // Non-type parameter
            if (arg.kind != Parser::TemplateArgument::Kind::Value) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Method template parameter '" + param.name + "' expects a value, but got a type",
                    arg.location
                );
            } else {
                // Evaluate the constant expression
                int64_t value = evaluateConstantExpression(arg.valueArg.get());
                inst.evaluatedValues.push_back(value);
            }
        }
    }

    // Record the instantiation
    methodTemplateInstantiations.insert(inst);

    // Validate the method body with substituted types
    // Uses pre-extracted call info (no AST access), safe for all cases
    validateMethodTemplateBody(className, methodName, args,
        args.empty() ? Common::SourceLocation{} : args[0].location);
}

bool SemanticAnalyzer::isTemplateMethod(const std::string& className, const std::string& methodName) {
    std::string methodKey = className + "::" + methodName;
    return templateMethods.find(methodKey) != templateMethods.end();
}

void SemanticAnalyzer::recordLambdaTemplateInstantiation(const std::string& variableName, const std::vector<Parser::TemplateArgument>& args) {
    // Check if this template lambda exists
    auto it = templateLambdas_.find(variableName);
    if (it == templateLambdas_.end()) {
        // Not a template lambda - might be a regular lambda or error caught elsewhere
        return;
    }

    const TemplateLambdaInfo& lambdaInfo = it->second;

    // Validate argument count
    if (lambdaInfo.templateParams.size() != args.size()) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Lambda template '" + variableName + "' expects " +
            std::to_string(lambdaInfo.templateParams.size()) +
            " arguments but got " + std::to_string(args.size()),
            args.empty() ? Common::SourceLocation{} : args[0].location
        );
        return;
    }

    // Create instantiation record with QUALIFIED type arguments
    LambdaTemplateInstantiation inst;
    inst.variableName = variableName;

    // Qualify all type arguments before storing
    for (const auto& arg : args) {
        Parser::TemplateArgument qualifiedArg = arg;
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            qualifiedArg.typeArg = resolveTypeArgToQualified(arg.typeArg);
        }
        inst.arguments.push_back(qualifiedArg);
    }

    // Validate and evaluate template arguments
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto& param = lambdaInfo.templateParams[i];

        // Wildcard can match any type parameter
        if (arg.kind == Parser::TemplateArgument::Kind::Wildcard) {
            if (param.kind != Parser::TemplateParameter::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Wildcard '?' can only be used for type parameters, not value parameters",
                    arg.location
                );
            }
            continue;
        }

        // Validate argument kind matches parameter kind
        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            if (arg.kind != Parser::TemplateArgument::Kind::Type) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Lambda template parameter '" + param.name + "' expects a type, but got a value",
                    arg.location
                );
            } else {
                // Validate type constraints with type substitution for lambda template param
                std::unordered_map<std::string, std::string> lambdaSubs;
                lambdaSubs[param.name] = arg.typeArg;
                if (!validateConstraint(arg.typeArg, param.constraints, param.constraintsAreAnd, lambdaSubs)) {
                    std::string constraintList;
                    std::string separator = param.constraintsAreAnd ? ", " : " | ";
                    for (size_t j = 0; j < param.constraints.size(); ++j) {
                        if (j > 0) constraintList += separator;
                        constraintList += param.constraints[j].toString();
                    }
                    std::string semanticsMsg = param.constraintsAreAnd
                        ? " (must satisfy ALL)" : " (must satisfy at least one)";
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        "Type '" + arg.typeArg + "' does not satisfy constraint '" +
                        constraintList + "'" + semanticsMsg + " for lambda template parameter '" + param.name + "'",
                        arg.location
                    );
                }
            }
        } else {  // Non-type parameter
            if (arg.kind != Parser::TemplateArgument::Kind::Value) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Lambda template parameter '" + param.name + "' expects a value, but got a type",
                    arg.location
                );
            } else {
                // Evaluate the constant expression
                int64_t value = evaluateConstantExpression(arg.valueArg.get());
                inst.evaluatedValues.push_back(value);
            }
        }
    }

    // Record the instantiation
    lambdaTemplateInstantiations_.insert(inst);

    // Lambda template body validation is disabled for now as it can cause
    // memory access issues similar to method templates
    // TODO: Re-enable when AST lifetime management is improved
}

bool SemanticAnalyzer::isTemplateLambda(const std::string& variableName) {
    return templateLambdas_.find(variableName) != templateLambdas_.end();
}

void SemanticAnalyzer::validateMethodTemplateBody(
    const std::string& className,
    const std::string& methodName,
    const std::vector<Parser::TemplateArgument>& args,
    const Common::SourceLocation& callLocation) {

    // Early safety check: skip if class name contains template syntax
    // This indicates an instantiated template class like Box<Integer>
    if (className.find('<') != std::string::npos ||
        className.find('@') != std::string::npos) {
        return;
    }

    std::string methodKey = className + "::" + methodName;
    auto it = templateMethods.find(methodKey);
    if (it == templateMethods.end()) return;

    const TemplateMethodInfo& methodInfo = it->second;

    // Skip if no calls were extracted (empty body or no template-param calls)
    if (methodInfo.callsInBody.empty()) return;

    // Build type substitution map from template arguments
    std::unordered_map<std::string, std::string> typeMap;
    for (size_t i = 0; i < methodInfo.templateParams.size() && i < args.size(); ++i) {
        const auto& param = methodInfo.templateParams[i];
        const auto& arg = args[i];
        if (param.kind == Parser::TemplateParameter::Kind::Type &&
            arg.kind == Parser::TemplateArgument::Kind::Type) {
            typeMap[param.name] = arg.typeArg;
        }
    }

    // Validate each extracted call with substituted types
    // This uses ONLY the pre-extracted data, no AST access
    for (const auto& call : methodInfo.callsInBody) {
        // Substitute the template parameter with the actual type
        auto typeIt = typeMap.find(call.className);
        if (typeIt == typeMap.end()) continue;

        std::string substitutedClassName = typeIt->second;

        // Find the method in the substituted class
        MethodInfo* method = findMethod(substitutedClassName, call.methodName);
        if (!method) {
            errorReporter.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Template instantiation error: Method '" + call.methodName +
                "' not found in class '" + substitutedClassName + "'",
                callLocation
            );
            continue;
        }

        // Validate argument count
        if (call.argumentTypes.size() != method->parameters.size()) {
            errorReporter.reportError(
                Common::ErrorCode::InvalidMethodCall,
                "Template instantiation error: Method '" + substitutedClassName + "::" +
                call.methodName + "' expects " + std::to_string(method->parameters.size()) +
                " argument(s), but " + std::to_string(call.argumentTypes.size()) + " provided",
                callLocation
            );
            continue;
        }

        // Validate argument types
        for (size_t i = 0; i < call.argumentTypes.size(); ++i) {
            const std::string& argType = call.argumentTypes[i];
            const auto& [expectedType, expectedOwnership] = method->parameters[i];

            // Strip ownership from expected type for comparison
            std::string baseExpectedType = expectedType;
            if (!baseExpectedType.empty() &&
                (baseExpectedType.back() == '^' || baseExpectedType.back() == '&' ||
                 baseExpectedType.back() == '%')) {
                baseExpectedType.pop_back();
            }

            // Strict check for Integer -> NativeType<cstr> (not allowed in templates)
            bool isStrictlyIncompatible = false;
            if (argType == "Integer" &&
                (baseExpectedType.find("NativeType<cstr>") != std::string::npos ||
                 baseExpectedType.find("NativeType<\"cstr\">") != std::string::npos ||
                 baseExpectedType.find("NativeType<string_ptr>") != std::string::npos ||
                 baseExpectedType.find("NativeType<\"string_ptr\">") != std::string::npos)) {
                isStrictlyIncompatible = true;
            }

            if (isStrictlyIncompatible || !isCompatibleType(baseExpectedType, argType)) {
                errorReporter.reportError(
                    Common::ErrorCode::TypeMismatch,
                    "Template instantiation error: Cannot pass '" + argType +
                    "' to method '" + substitutedClassName + "::" + call.methodName +
                    "' which expects '" + expectedType + "'",
                    callLocation
                );
            }
        }
    }
}

void SemanticAnalyzer::validateLambdaTemplateBody(
    const std::string& variableName,
    const std::vector<Parser::TemplateArgument>& args,
    const Common::SourceLocation& callLocation) {

    auto it = templateLambdas_.find(variableName);
    if (it == templateLambdas_.end()) return;

    const TemplateLambdaInfo& lambdaInfo = it->second;
    Parser::LambdaExpr* templateLambda = lambdaInfo.astNode;

    // Skip validation for imported modules (astNode is null for imported templates)
    if (!templateLambda) return;

    // Skip validation for lambdas that return template types
    if (templateLambda->returnType) {
        const std::string& retTypeName = templateLambda->returnType->typeName;
        if (retTypeName.find('@') != std::string::npos ||
            retTypeName.find('<') != std::string::npos) {
            return;
        }
    }

    if (templateLambda->body.empty()) return;

    // Build type substitution map
    std::unordered_map<std::string, std::string> typeMap;
    for (size_t i = 0; i < lambdaInfo.templateParams.size() && i < args.size(); ++i) {
        const auto& param = lambdaInfo.templateParams[i];
        const auto& arg = args[i];
        if (param.kind == Parser::TemplateParameter::Kind::Type &&
            arg.kind == Parser::TemplateArgument::Kind::Type) {
            typeMap[param.name] = arg.typeArg;
        }
    }

    // Validate each statement in the body with substituted types
    for (auto& stmt : templateLambda->body) {
        validateStatementWithSubstitution(stmt.get(), typeMap, callLocation);
    }
}

void SemanticAnalyzer::validateStatementWithSubstitution(
    Parser::Statement* stmt,
    const std::unordered_map<std::string, std::string>& typeMap,
    const Common::SourceLocation& callLocation) {

    if (!stmt) return;

    // Handle ReturnStmt
    if (auto* returnStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            validateExpressionWithSubstitution(returnStmt->value.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle RunStmt
    if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        if (runStmt->expression) {
            validateExpressionWithSubstitution(runStmt->expression.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle InstantiateStmt
    if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        if (instStmt->initializer) {
            validateExpressionWithSubstitution(instStmt->initializer.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle IfStmt
    if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        if (ifStmt->condition) {
            validateExpressionWithSubstitution(ifStmt->condition.get(), typeMap, callLocation);
        }
        for (auto& s : ifStmt->thenBranch) {
            validateStatementWithSubstitution(s.get(), typeMap, callLocation);
        }
        for (auto& s : ifStmt->elseBranch) {
            validateStatementWithSubstitution(s.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle WhileStmt
    if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        if (whileStmt->condition) {
            validateExpressionWithSubstitution(whileStmt->condition.get(), typeMap, callLocation);
        }
        for (auto& s : whileStmt->body) {
            validateStatementWithSubstitution(s.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle ForStmt
    if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        if (forStmt->rangeStart) {
            validateExpressionWithSubstitution(forStmt->rangeStart.get(), typeMap, callLocation);
        }
        if (forStmt->rangeEnd) {
            validateExpressionWithSubstitution(forStmt->rangeEnd.get(), typeMap, callLocation);
        }
        for (auto& s : forStmt->body) {
            validateStatementWithSubstitution(s.get(), typeMap, callLocation);
        }
        return;
    }
}

void SemanticAnalyzer::validateExpressionWithSubstitution(
    Parser::Expression* expr,
    const std::unordered_map<std::string, std::string>& typeMap,
    const Common::SourceLocation& callLocation) {

    if (!expr) return;

    // Use try-catch to handle any errors gracefully
    // This is a best-effort validation - we'd rather skip than crash
    try {
        if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
            if (!callExpr->callee) {
                // Recursively validate arguments even if callee is null
                for (auto& arg : callExpr->arguments) {
                    if (arg) validateExpressionWithSubstitution(arg.get(), typeMap, callLocation);
                }
                return;
            }
            std::string fullName = buildQualifiedName(callExpr->callee.get());
        std::string className = extractClassName(fullName);
        std::string methodName = extractMethodName(fullName);

        // Skip if we couldn't parse the callee properly
        if (className.empty() || methodName.empty()) {
            // Still recursively validate arguments
            for (auto& arg : callExpr->arguments) {
                validateExpressionWithSubstitution(arg.get(), typeMap, callLocation);
            }
            return;
        }

        // Skip validation for parameterized types (e.g., Box@T, Maybe<T>)
        // These involve nested template resolution that's complex to validate here
        if (className.find('@') != std::string::npos ||
            className.find('<') != std::string::npos) {
            // Still validate arguments
            for (auto& arg : callExpr->arguments) {
                validateExpressionWithSubstitution(arg.get(), typeMap, callLocation);
            }
            return;
        }

        // Substitute template parameter with actual type
        auto typeIt = typeMap.find(className);
        if (typeIt != typeMap.end()) {
            std::string substitutedClassName = typeIt->second;

            // Now validate the substituted call
            MethodInfo* method = findMethod(substitutedClassName, methodName);
            if (!method) {
                errorReporter.reportError(
                    Common::ErrorCode::UndeclaredIdentifier,
                    "Method '" + methodName + "' not found in class '" + substitutedClassName +
                    "' (from template instantiation with " + className + " = " + substitutedClassName + ")",
                    callLocation
                );
                return;
            }

            // Validate argument count
            if (callExpr->arguments.size() != method->parameters.size()) {
                errorReporter.reportError(
                    Common::ErrorCode::InvalidMethodCall,
                    "Method '" + substitutedClassName + "::" + methodName + "' expects " +
                    std::to_string(method->parameters.size()) + " argument(s), but " +
                    std::to_string(callExpr->arguments.size()) + " provided (from template instantiation)",
                    callLocation
                );
            } else {
                // Validate argument types
                for (size_t i = 0; i < callExpr->arguments.size(); ++i) {
                    if (!callExpr->arguments[i]) continue;  // Safety check
                    std::string argType = getExpressionType(callExpr->arguments[i].get());
                    if (argType.empty()) continue;  // Skip if couldn't determine type
                    const auto& [expectedType, expectedOwnership] = method->parameters[i];

                    // Strip ownership from expected type
                    std::string baseExpectedType = expectedType;
                    if (!baseExpectedType.empty() &&
                        (baseExpectedType.back() == '^' || baseExpectedType.back() == '&' ||
                         baseExpectedType.back() == '%')) {
                        baseExpectedType.pop_back();
                    }

                    // For template body validation, be stricter:
                    // Don't allow Integer -> NativeType<cstr> (only valid for null pointer checks)
                    // Don't allow Integer -> NativeType<string_ptr>
                    bool isStrictlyIncompatible = false;
                    if (argType == "Integer" &&
                        (baseExpectedType.find("NativeType<cstr>") != std::string::npos ||
                         baseExpectedType.find("NativeType<\"cstr\">") != std::string::npos ||
                         baseExpectedType.find("NativeType<string_ptr>") != std::string::npos ||
                         baseExpectedType.find("NativeType<\"string_ptr\">") != std::string::npos)) {
                        isStrictlyIncompatible = true;
                    }

                    if (isStrictlyIncompatible || !isCompatibleType(baseExpectedType, argType)) {
                        errorReporter.reportError(
                            Common::ErrorCode::TypeMismatch,
                            "Template instantiation error: Cannot pass '" + argType +
                            "' to method '" + substitutedClassName + "::" + methodName +
                            "' which expects '" + expectedType + "'",
                            callLocation
                        );
                    }
                }
            }
        }

        // Recursively validate arguments
        for (auto& arg : callExpr->arguments) {
            validateExpressionWithSubstitution(arg.get(), typeMap, callLocation);
        }
        return;
    }

    // Handle BinaryExpr
    if (auto* binaryExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        validateExpressionWithSubstitution(binaryExpr->left.get(), typeMap, callLocation);
        validateExpressionWithSubstitution(binaryExpr->right.get(), typeMap, callLocation);
        return;
    }

    // Handle MemberAccessExpr
    if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        if (memberExpr->object) {
            validateExpressionWithSubstitution(memberExpr->object.get(), typeMap, callLocation);
        }
        return;
    }
    } catch (const std::exception&) {
        // Silently skip validation on error - better to let code gen handle it
        // than to crash during validation
    } catch (...) {
        // Catch any other exceptions
    }
}

bool SemanticAnalyzer::isTypeCompatible(const std::string& actualType, const std::string& constraintType) {
    // Exact match
    if (actualType == constraintType) {
        return true;
    }

    // Check if actualType is in the class registry
    auto* actualClass = findClass(actualType);
    if (!actualClass) {
        // Type not found in registry, assume it's compatible if names match
        return actualType == constraintType;
    }

    // Check inheritance - does actualType extend constraintType?
    // ✅ SAFE: Use copied baseClassName instead of astNode pointer
    if (!actualClass->baseClassName.empty()) {
        if (actualClass->baseClassName == constraintType) {
            return true;
        }
        // Recursive check up the inheritance chain
        return isTypeCompatible(actualClass->baseClassName, constraintType);
    }

    return false;
}

bool SemanticAnalyzer::validateConstraint(const std::string& typeName,
                                          const std::vector<Parser::ConstraintRef>& constraints,
                                          bool constraintsAreAnd,
                                          const std::unordered_map<std::string, std::string>& typeSubstitutions) {
    // Empty constraints means no restrictions (any type allowed)
    if (constraints.empty()) {
        return true;
    }

    if (constraintsAreAnd) {
        // AND semantics: type must satisfy ALL constraints
        for (const auto& constraint : constraints) {
            if (!validateSingleConstraint(typeName, constraint, typeSubstitutions, true)) {
                return false;
            }
        }
        return true;
    } else {
        // OR semantics (pipe): type must satisfy at least ONE constraint
        // First pass: check without reporting errors
        for (const auto& constraint : constraints) {
            if (validateSingleConstraint(typeName, constraint, typeSubstitutions, false)) {
                return true;  // Found one that works, no errors needed
            }
        }
        // None satisfied - no error reported here, caller will report
        return false;
    }
}

bool SemanticAnalyzer::validateSingleConstraint(const std::string& typeName,
                                                const Parser::ConstraintRef& constraint,
                                                const std::unordered_map<std::string, std::string>& providedSubstitutions,
                                                bool reportErrors) {
    // Resolve template arguments in constraint
    // e.g., Hashable<T> where T=String becomes Hashable<String>
    std::vector<std::string> resolvedArgs;
    for (const auto& arg : constraint.templateArgs) {
        auto it = providedSubstitutions.find(arg);
        if (it != providedSubstitutions.end()) {
            resolvedArgs.push_back(it->second);
        } else {
            resolvedArgs.push_back(arg);
        }
    }

    // Check if this is a constraint definition (has requirements)
    auto constraintIt = constraintRegistry_.find(constraint.name);
    if (constraintIt != constraintRegistry_.end()) {
        // Build type substitution map for parameterized constraints
        const ConstraintInfo& constraintInfo = constraintIt->second;
        std::unordered_map<std::string, std::string> typeSubstitutions = providedSubstitutions;

        // Map constraint's template parameters to resolved args
        for (size_t i = 0; i < constraintInfo.templateParams.size() && i < resolvedArgs.size(); ++i) {
            const std::string& paramName = constraintInfo.templateParams[i].name;
            const std::string& argValue = resolvedArgs[i];

            // The argument might refer to the type being validated
            if (argValue == typeName || typeSubstitutions.find(argValue) != typeSubstitutions.end()) {
                typeSubstitutions[paramName] = (typeSubstitutions.find(argValue) != typeSubstitutions.end())
                    ? typeSubstitutions.at(argValue) : argValue;
            } else {
                typeSubstitutions[paramName] = typeName;  // Default: substitute with type being validated
            }
        }

        // If no template args but constraint has params, default to typeName
        if (resolvedArgs.empty() && !constraintInfo.templateParams.empty()) {
            typeSubstitutions[constraintInfo.templateParams[0].name] = typeName;
        }

        // Validate requirements
        return validateConstraintRequirements(typeName, constraintInfo, Common::SourceLocation(), typeSubstitutions, reportErrors);
    } else {
        // Simple type constraint - check type compatibility
        return isTypeCompatible(typeName, constraint.name);
    }
}

// Constraint validation helpers
bool SemanticAnalyzer::hasMethod(const std::string& className,
                                 const std::string& methodName,
                                 Parser::TypeRef* returnType) {
    // Find the class in the registry
    ClassInfo* classInfo = findClass(className);
    if (!classInfo) {
        return false;
    }

    // Look for a method with the given name
    auto methodIt = classInfo->methods.find(methodName);
    if (methodIt == classInfo->methods.end()) {
        return false;
    }

    // If return type is specified, validate it matches
    if (returnType) {
        const MethodInfo& method = methodIt->second;
        std::string expectedReturnType = returnType->typeName;

        // Check if return types are compatible
        if (!isTypeCompatible(method.returnType, expectedReturnType)) {
            return false;
        }
    }

    // Method exists with compatible return type (or no return type check requested)
    return true;
}

bool SemanticAnalyzer::hasConstructor(const std::string& className,
                                     const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes) {
    // Find the class in the registry
    ClassInfo* classInfo = findClass(className);
    if (!classInfo) {
        return false;
    }

    // Look for a constructor with matching parameter types
    // Constructors are stored under the key "Constructor"
    auto methodIt = classInfo->methods.find("Constructor");
    if (methodIt == classInfo->methods.end()) {
        // No constructor found - check if it's a default constructor request
        return paramTypes.empty();  // Classes have implicit default constructor if no params requested
    }

    const MethodInfo& constructor = methodIt->second;

    // If we're checking for a parameterless constructor (None)
    if (paramTypes.empty()) {
        return constructor.parameters.empty();
    }

    // Check if parameter counts match
    if (constructor.parameters.size() != paramTypes.size()) {
        return false;
    }

    // Check if parameter types are compatible
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        const auto& expectedParam = paramTypes[i];
        const auto& actualParam = constructor.parameters[i];

        if (!isTypeCompatible(actualParam.first, expectedParam->typeName)) {
            return false;
        }
    }

    return true;
}

bool SemanticAnalyzer::evaluateTruthCondition(Parser::Expression* expr,
                                              const std::unordered_map<std::string, std::string>& typeSubstitutions) {
    // Evaluate compile-time truth conditions like TypeOf<T>() == TypeOf<Integer>()

    // Check if it's a binary expression (most truth conditions are comparisons)
    if (auto* binaryExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        if (binaryExpr->op == "==") {
            // Both sides should be TypeOf expressions
            auto* leftTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->left.get());
            auto* rightTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->right.get());

            if (leftTypeOf && rightTypeOf) {
                // Get the type names
                std::string leftType = leftTypeOf->type->typeName;
                std::string rightType = rightTypeOf->type->typeName;

                // Apply type substitutions (e.g., T -> Integer)
                if (typeSubstitutions.find(leftType) != typeSubstitutions.end()) {
                    leftType = typeSubstitutions.at(leftType);
                }
                if (typeSubstitutions.find(rightType) != typeSubstitutions.end()) {
                    rightType = typeSubstitutions.at(rightType);
                }

                // Check if types are equal
                return leftType == rightType;
            }
        } else if (binaryExpr->op == "!=") {
            // Handle inequality
            auto* leftTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->left.get());
            auto* rightTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->right.get());

            if (leftTypeOf && rightTypeOf) {
                std::string leftType = leftTypeOf->type->typeName;
                std::string rightType = rightTypeOf->type->typeName;

                if (typeSubstitutions.find(leftType) != typeSubstitutions.end()) {
                    leftType = typeSubstitutions.at(leftType);
                }
                if (typeSubstitutions.find(rightType) != typeSubstitutions.end()) {
                    rightType = typeSubstitutions.at(rightType);
                }

                return leftType != rightType;
            }
        }
    }

    // If we can't evaluate the expression, return false (constraint not satisfied)
    return false;
}

bool SemanticAnalyzer::validateConstraintRequirements(const std::string& typeName,
                                                      const ConstraintInfo& constraint,
                                                      const Common::SourceLocation& loc,
                                                      const std::unordered_map<std::string, std::string>& providedSubstitutions,
                                                      bool reportErrors) {
    // Use provided substitutions if available, otherwise build default ones
    std::unordered_map<std::string, std::string> typeSubstitutions = providedSubstitutions;

    // If no substitutions provided and constraint has template parameters, build defaults
    if (typeSubstitutions.empty() && !constraint.templateParams.empty()) {
        // Map the first template parameter to the actual type
        typeSubstitutions[constraint.templateParams[0].name] = typeName;
    }

    // Resolve the type name to its fully qualified name
    // Try to find the class to get its actual registered name
    std::string resolvedTypeName = typeName;
    ClassInfo* classInfo = findClass(typeName);
    if (classInfo) {
        // Use the fully qualified name from the registry
        // Search the registry for the key that matches this ClassInfo
        for (const auto& pair : classRegistry_) {
            if (&pair.second == classInfo) {
                resolvedTypeName = pair.first;
                break;
            }
        }
    }

    // Validate each requirement
    for (const auto* requirement : constraint.requirements) {
        if (requirement->kind == Parser::RequirementKind::Method) {
            // Substitute template parameters in the return type if needed
            Parser::TypeRef* returnTypeToCheck = requirement->methodReturnType.get();
            std::unique_ptr<Parser::TypeRef> substitutedReturnType;

            if (returnTypeToCheck) {
                std::string returnTypeName = returnTypeToCheck->typeName;

                // Check if the return type is a template parameter that needs substitution
                if (typeSubstitutions.find(returnTypeName) != typeSubstitutions.end()) {
                    // Create a substituted return type
                    std::string substitutedTypeName = typeSubstitutions.at(returnTypeName);
                    substitutedReturnType = std::make_unique<Parser::TypeRef>(
                        substitutedTypeName,
                        returnTypeToCheck->ownership,
                        returnTypeToCheck->location,
                        returnTypeToCheck->isTemplateParameter
                    );
                    returnTypeToCheck = substitutedReturnType.get();
                }
            }

            // Check if the type has the required method (use resolved name)
            if (!hasMethod(resolvedTypeName, requirement->methodName, returnTypeToCheck)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' does not have required method '" +
                        requirement->methodName + "'",
                        loc
                    );
                }
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::Constructor) {
            // Check if the type has the required constructor
            if (!hasConstructor(typeName, requirement->constructorParamTypes)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' does not have required constructor",
                        loc
                    );
                }
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::Truth) {
            // Evaluate the truth condition
            if (!evaluateTruthCondition(requirement->truthCondition.get(), typeSubstitutions)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' does not satisfy truth condition",
                        loc
                    );
                }
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::CompiletimeMethod) {
            // FC: Check if the type has the required method AND it's compile-time
            Parser::TypeRef* returnTypeToCheck = requirement->methodReturnType.get();
            std::unique_ptr<Parser::TypeRef> substitutedReturnType;
            if (returnTypeToCheck) {
                std::string returnTypeName = returnTypeToCheck->typeName;
                if (typeSubstitutions.find(returnTypeName) != typeSubstitutions.end()) {
                    std::string substitutedTypeName = typeSubstitutions.at(returnTypeName);
                    substitutedReturnType = std::make_unique<Parser::TypeRef>(
                        substitutedTypeName,
                        returnTypeToCheck->ownership,
                        returnTypeToCheck->location,
                        returnTypeToCheck->isTemplateParameter
                    );
                    returnTypeToCheck = substitutedReturnType.get();
                }
            }
            if (!hasMethod(resolvedTypeName, requirement->methodName, returnTypeToCheck)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' does not have required method '" +
                        requirement->methodName + "'",
                        loc
                    );
                }
                return false;
            }
            // Now check it's compile-time
            if (!isCompiletimeMethod(resolvedTypeName, requirement->methodName)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' has method '" + requirement->methodName +
                        "' but it is not compile-time. Add Compiletime to the method declaration.",
                        loc
                    );
                }
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::CompiletimeConstructor) {
            // CC: Check if the type has the required constructor AND it's compile-time
            if (!hasConstructor(typeName, requirement->constructorParamTypes)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' does not have required constructor",
                        loc
                    );
                }
                return false;
            }
            // Now check it's compile-time
            if (!hasCompiletimeConstructor(typeName, requirement->constructorParamTypes)) {
                if (reportErrors) {
                    errorReporter.reportError(
                        Common::ErrorCode::ConstraintViolation,
                        "Type '" + typeName + "' has required constructor but it is not compile-time. "
                        "Add Compiletime to the constructor declaration.",
                        loc
                    );
                }
                return false;
            }
        }
    }

    return true;
}

// Constraint-related visitor methods
void SemanticAnalyzer::visit(Parser::ConstraintDecl& node) {
    // Register the constraint in the registry
    ConstraintInfo info;
    info.name = node.name;
    info.templateParams = node.templateParams;
    info.paramBindings = node.paramBindings;
    info.astNode = &node;

    // Store raw pointers to requirements (owned by AST)
    for (const auto& req : node.requirements) {
        info.requirements.push_back(req.get());
    }

    // Check for duplicate constraint names
    if (constraintRegistry_.find(node.name) != constraintRegistry_.end()) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateSymbol,
            "Constraint '" + node.name + "' is already defined",
            node.location
        );
        return;
    }

    constraintRegistry_[node.name] = info;

    // Visit requirements to validate their syntax
    for (auto& req : node.requirements) {
        req->accept(*this);
    }
}

void SemanticAnalyzer::visit(Parser::AnnotateDecl& node) {
    // Visit the type
    if (node.type) {
        node.type->accept(*this);
    }
    // Visit the default value if present
    if (node.defaultValue) {
        node.defaultValue->accept(*this);
    }
}

void SemanticAnalyzer::visit(Parser::ProcessorDecl& node) {
    // Enable processor context - this allows ReflectionContext and CompilationContext types
    inProcessorContext_ = true;

    // Find and validate onAnnotate method
    bool hasOnAnnotate = false;
    for (auto& section : node.sections) {
        if (!section) continue;
        for (auto& decl : section->declarations) {
            if (!decl) continue;
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                if (method->name == "onAnnotate") {
                    hasOnAnnotate = true;
                    validateOnAnnotateSignature(*method);
                }
            }
        }
    }

    if (!hasOnAnnotate) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidMethodCall,
            "Processor must define 'onAnnotate' method with signature: "
            "Returns None Parameters (ReflectionContext&, CompilationContext&)",
            node.location
        );
    }

    // Visit access sections
    for (auto& section : node.sections) {
        if (section) {
            section->accept(*this);
        }
    }

    // Disable processor context
    inProcessorContext_ = false;
}

void SemanticAnalyzer::validateOnAnnotateSignature(Parser::MethodDecl& method) {
    // Must return None
    if (!method.returnType || method.returnType->typeName != "None") {
        std::string actualReturn = method.returnType ? method.returnType->typeName : "unknown";
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "onAnnotate must return None, but returns '" + actualReturn + "'",
            method.location
        );
    }

    // Must have exactly 2 parameters: ReflectionContext&, CompilationContext&
    if (method.parameters.size() != 2) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "onAnnotate must have exactly 2 parameters: (ReflectionContext&, CompilationContext&), "
            "but has " + std::to_string(method.parameters.size()) + " parameter(s)",
            method.location
        );
        return;
    }

    // Check first parameter: ReflectionContext&
    auto& param1 = method.parameters[0];
    if (!param1->type || param1->type->typeName != "ReflectionContext" ||
        param1->type->ownership != Parser::OwnershipType::Reference) {
        std::string actualType = param1->type ? param1->type->typeName : "unknown";
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "First parameter of onAnnotate must be 'ReflectionContext&', but is '" + actualType + "'",
            param1->location
        );
    }

    // Check second parameter: CompilationContext&
    auto& param2 = method.parameters[1];
    if (!param2->type || param2->type->typeName != "CompilationContext" ||
        param2->type->ownership != Parser::OwnershipType::Reference) {
        std::string actualType = param2->type ? param2->type->typeName : "unknown";
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Second parameter of onAnnotate must be 'CompilationContext&', but is '" + actualType + "'",
            param2->location
        );
    }
}

bool SemanticAnalyzer::isCompilerIntrinsicType(const std::string& typeName) const {
    return typeName == "ReflectionContext" || typeName == "CompilationContext";
}

void SemanticAnalyzer::visit(Parser::AnnotationDecl& node) {
    // Check for duplicate annotation definitions
    // If already registered (from cross-module sharing), skip re-registration silently
    // This allows annotations imported from other modules to be re-analyzed
    if (annotationRegistry_.find(node.name) != annotationRegistry_.end()) {
        // Only report error during Phase 2 validation within the same module
        // Cross-module annotations are pre-registered and should be skipped
        return;
    }

    // Register the annotation
    AnnotationInfo info;
    info.name = node.name;
    info.allowedTargets = node.allowedTargets;
    info.retainAtRuntime = node.retainAtRuntime;
    info.astNode = &node;

    // Process parameters
    for (auto& param : node.parameters) {
        param->accept(*this);

        AnnotationParamInfo paramInfo;
        paramInfo.name = param->name;
        if (param->type) {
            paramInfo.typeName = param->type->typeName;
            paramInfo.ownership = param->type->ownership;
        }
        paramInfo.hasDefault = (param->defaultValue != nullptr);
        info.parameters.push_back(paramInfo);
    }

    // Register annotation BEFORE visiting processor so getAnnotationArg() can look up parameter types
    annotationRegistry_[node.name] = info;

    // Visit processor if present
    if (node.processor) {
        // Set current annotation name so processor can look up parameter types
        currentAnnotationName_ = node.name;
        node.processor->accept(*this);
        currentAnnotationName_.clear();

        // Queue for auto-compilation (validation passed if we got here)
        PendingProcessorCompilation pending;
        pending.annotationName = node.name;
        pending.annotDecl = &node;
        pending.processorDecl = node.processor.get();
        // Copy imports from current file so processor can access imported modules
        pending.imports.assign(importedNamespaces_.begin(), importedNamespaces_.end());
        // Copy user-defined classes so processor can reference them
        pending.userClasses = localClasses_;
        pendingProcessorCompilations_.push_back(pending);
    }
}

void SemanticAnalyzer::visit(Parser::AnnotationUsage& node) {
    // Visit argument expressions first
    for (auto& arg : node.arguments) {
        if (arg.second) {
            arg.second->accept(*this);
        }
    }

    // Note: Full validation is done in validateAnnotationUsage() which is called
    // by the parent declaration visitor with the appropriate target kind
}

std::string SemanticAnalyzer::annotationTargetToString(Parser::AnnotationTarget target) {
    switch (target) {
        case Parser::AnnotationTarget::Properties: return "properties";
        case Parser::AnnotationTarget::Variables: return "variables";
        case Parser::AnnotationTarget::Classes: return "classes";
        case Parser::AnnotationTarget::Methods: return "methods";
        default: return "unknown";
    }
}

bool SemanticAnalyzer::isValidAnnotationTarget(const AnnotationInfo& annotation,
                                               Parser::AnnotationTarget target) {
    for (const auto& allowed : annotation.allowedTargets) {
        if (allowed == target) return true;
    }
    return false;
}

void SemanticAnalyzer::validateAnnotationUsage(Parser::AnnotationUsage& usage,
                                               Parser::AnnotationTarget targetKind,
                                               const std::string& targetName,
                                               const Common::SourceLocation& targetLoc,
                                               Parser::ASTNode* astNode) {
    // Skip validation during Phase 1 (registration phase)
    // Annotations from imported modules aren't available yet
    if (!enableValidation) return;

    // Look up the annotation definition
    auto it = annotationRegistry_.find(usage.annotationName);
    if (it == annotationRegistry_.end()) {
        errorReporter.reportError(
            Common::ErrorCode::UndefinedType,
            "Unknown annotation '@" + usage.annotationName + "'",
            usage.location
        );
        return;
    }

    const AnnotationInfo& annotation = it->second;

    // Check if the annotation is allowed on this target
    if (!isValidAnnotationTarget(annotation, targetKind)) {
        std::string allowedStr;
        for (size_t i = 0; i < annotation.allowedTargets.size(); ++i) {
            if (i > 0) allowedStr += ", ";
            allowedStr += annotationTargetToString(annotation.allowedTargets[i]);
        }
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Annotation '@" + usage.annotationName + "' cannot be applied to " +
            annotationTargetToString(targetKind) + ". Allowed targets: " + allowedStr,
            usage.location
        );
        return;
    }

    // Build a set of provided argument names
    std::set<std::string> providedArgs;
    for (const auto& arg : usage.arguments) {
        providedArgs.insert(arg.first);
    }

    // Check that all required parameters are provided
    bool hasErrors = false;
    for (const auto& param : annotation.parameters) {
        if (!param.hasDefault && providedArgs.find(param.name) == providedArgs.end()) {
            errorReporter.reportError(
                Common::ErrorCode::MissingArgument,
                "Missing required argument '" + param.name + "' for annotation '@" +
                usage.annotationName + "'",
                usage.location
            );
            hasErrors = true;
        }
    }

    // Check for unknown arguments
    std::set<std::string> knownParams;
    for (const auto& param : annotation.parameters) {
        knownParams.insert(param.name);
    }
    for (const auto& arg : usage.arguments) {
        if (knownParams.find(arg.first) == knownParams.end()) {
            errorReporter.reportError(
                Common::ErrorCode::UnknownArgument,
                "Unknown argument '" + arg.first + "' for annotation '@" +
                usage.annotationName + "'",
                usage.location
            );
            hasErrors = true;
        }
    }

    // If validation passed, add to pending annotations for processing
    if (!hasErrors) {
        // Convert Parser::AnnotationTarget to AnnotationProcessor::AnnotationTarget::Kind
        AnnotationProcessor::AnnotationTarget::Kind procTargetKind;
        switch (targetKind) {
            case Parser::AnnotationTarget::Classes:
                procTargetKind = AnnotationProcessor::AnnotationTarget::Kind::Class;
                break;
            case Parser::AnnotationTarget::Methods:
                procTargetKind = AnnotationProcessor::AnnotationTarget::Kind::Method;
                break;
            case Parser::AnnotationTarget::Properties:
                procTargetKind = AnnotationProcessor::AnnotationTarget::Kind::Property;
                break;
            case Parser::AnnotationTarget::Variables:
                procTargetKind = AnnotationProcessor::AnnotationTarget::Kind::Variable;
                break;
        }

        // Create the annotation target info
        AnnotationProcessor::AnnotationTarget target;
        target.kind = procTargetKind;
        target.name = targetName;
        target.className = currentClass;
        target.namespaceName = currentNamespace;
        target.location = targetLoc;
        target.astNode = astNode;

        // Extract type name for properties and variables
        if (targetKind == Parser::AnnotationTarget::Properties) {
            if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(astNode)) {
                if (propDecl->type) {
                    target.typeName = propDecl->type->typeName;
                }
            }
        } else if (targetKind == Parser::AnnotationTarget::Variables) {
            if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(astNode)) {
                if (instStmt->type) {
                    target.typeName = instStmt->type->typeName;
                }
            }
        }

        // Create the pending annotation
        AnnotationProcessor::PendingAnnotation pending;
        pending.annotationName = usage.annotationName;
        pending.usage = &usage;
        pending.target = target;

        // Copy arguments (raw pointers to expression AST nodes)
        for (const auto& arg : usage.arguments) {
            pending.arguments.push_back({arg.first, arg.second.get()});
        }

        // Add to the processor
        annotationProcessor_.addPendingAnnotation(pending);
    }
}

void SemanticAnalyzer::visit(Parser::RequireStmt& node) {
    // Validate requirement syntax (basic checks)
    if (node.kind == Parser::RequirementKind::Method) {
        // Validate method requirement has all necessary fields
        if (node.methodName.empty()) {
            errorReporter.reportError(
                Common::ErrorCode::InvalidSyntax,
                "Method requirement must specify method name",
                node.location
            );
        }
        if (!node.methodReturnType) {
            errorReporter.reportError(
                Common::ErrorCode::InvalidSyntax,
                "Method requirement must specify return type",
                node.location
            );
        }
    } else if (node.kind == Parser::RequirementKind::Constructor) {
        // Constructor requirements are always valid (can have any number of params)
    } else if (node.kind == Parser::RequirementKind::Truth) {
        // Validate truth condition exists
        if (!node.truthCondition) {
            errorReporter.reportError(
                Common::ErrorCode::InvalidSyntax,
                "Truth requirement must have a condition",
                node.location
            );
        }
    }
}

void SemanticAnalyzer::visit(Parser::TypeOfExpr& node) {
    // TypeOf expressions are evaluated at compile-time in truth conditions
    // Store type information for later evaluation
    if (node.type) {
        expressionTypes[&node] = node.type->typeName;
    }
}

void SemanticAnalyzer::visit(Parser::LambdaExpr& node) {
    // Save and add lambda template parameters so body analysis knows they're template types
    std::set<std::string> previousTemplateParams = templateTypeParameters;
    for (const auto& templateParam : node.templateParams) {
        if (templateParam.kind == Parser::TemplateParameter::Kind::Type) {
            templateTypeParameters.insert(templateParam.name);
        }
    }

    // Validate captures and check ownership semantics
    for (const auto& capture : node.captures) {
        // Check variable exists
        if (!symbolTable_->resolve(capture.varName)) {
            errorReporter.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Captured variable '" + capture.varName + "' not found in scope",
                node.location
            );
            continue;
        }

        // Check if variable was already moved before this capture
        checkVariableNotMoved(capture.varName, node.location);

        // Mark variable as moved if captured by owned (^)
        if (capture.mode == Parser::LambdaExpr::CaptureMode::Owned) {
            markVariableMoved(capture.varName, node.location);
        }
    }

    // Create new scope for lambda body
    symbolTable_->enterScope("Lambda");

    // Save the current moved variables state and reset for lambda body
    // (captured variables are valid inside the lambda, even if moved outside)
    std::set<std::string> savedMovedVariables = movedVariables_;
    movedVariables_.clear();

    // Add parameters to scope
    for (const auto& param : node.parameters) {
        param->accept(*this);
    }

    // Analyze body
    for (const auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Restore moved variables state (lambda body is isolated)
    movedVariables_ = savedMovedVariables;

    symbolTable_->exitScope();

    // Restore previous template parameters
    templateTypeParameters = previousTemplateParams;

    // Store lambda expression type as function type
    expressionTypes[&node] = "__function";
}

void SemanticAnalyzer::visit(Parser::FunctionTypeRef& node) {
    // Validate return type
    if (node.returnType) {
        node.returnType->accept(*this);
    }

    // Validate parameter types
    for (const auto& param : node.paramTypes) {
        param->accept(*this);
    }
}

// ============================================================================
// Move Tracking Implementation
// ============================================================================

void SemanticAnalyzer::markVariableMoved(const std::string& varName, const Common::SourceLocation& loc) {
    // Check if already moved (double move error)
    if (movedVariables_.find(varName) != movedVariables_.end()) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidOwnership,
            "Variable '" + varName + "' has already been moved and cannot be moved again",
            loc
        );
        return;
    }
    movedVariables_.insert(varName);
}

bool SemanticAnalyzer::isVariableMoved(const std::string& varName) const {
    return movedVariables_.find(varName) != movedVariables_.end();
}

void SemanticAnalyzer::checkVariableNotMoved(const std::string& varName, const Common::SourceLocation& loc) {
    if (isVariableMoved(varName)) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidOwnership,
            "Use of moved variable '" + varName + "'. Variable was moved and can no longer be used.",
            loc
        );
    }
}

void SemanticAnalyzer::resetMovedVariables() {
    movedVariables_.clear();
}

void SemanticAnalyzer::registerFunctionType(const std::string& varName, Parser::FunctionTypeRef* funcType) {
    if (!funcType) return;

    std::vector<Parser::OwnershipType> paramOwnerships;
    for (const auto& paramType : funcType->paramTypes) {
        paramOwnerships.push_back(paramType->ownership);
    }
    functionTypeParams_[varName] = std::move(paramOwnerships);
}

std::vector<Parser::OwnershipType>* SemanticAnalyzer::getFunctionTypeParams(const std::string& varName) {
    auto it = functionTypeParams_.find(varName);
    if (it != functionTypeParams_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============ Compile-time helpers ============
bool SemanticAnalyzer::isCompiletimeType(const std::string& typeName) const {
    // Built-in primitive types are compile-time capable.
    // Note: Integer, Bool, Float, Double, String are also marked as Compiletime in their
    // class definitions (Language/Core/*.XXML), but we check them here explicitly because
    // this function may be called before those classes are registered in the classRegistry
    // during semantic analysis.
    if (typeName == "Integer" || typeName == "Bool" || typeName == "Float" ||
        typeName == "Double" || typeName == "String" || typeName == "Void") {
        return true;
    }
    // Check if the class itself is marked as compile-time
    auto it = classRegistry_.find(typeName);
    if (it != classRegistry_.end()) {
        return it->second.isCompiletime;
    }
    // Check with namespace prefix
    std::string qualifiedName = currentNamespace.empty() ? typeName : currentNamespace + "::" + typeName;
    it = classRegistry_.find(qualifiedName);
    if (it != classRegistry_.end()) {
        return it->second.isCompiletime;
    }
    return false;
}
bool SemanticAnalyzer::isCompiletimeMethod(const std::string& className, const std::string& methodName) {
    ClassInfo* classInfo = findClass(className);
    if (!classInfo) {
        return false;
    }
    // If the class is compile-time, all its methods are compile-time
    if (classInfo->isCompiletime) {
        return true;
    }
    // Check the specific method
    auto methodIt = classInfo->methods.find(methodName);
    if (methodIt != classInfo->methods.end()) {
        return methodIt->second.isCompiletime;
    }
    return false;
}
bool SemanticAnalyzer::hasCompiletimeConstructor(const std::string& className,
                                                  const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes) {
    ClassInfo* classInfo = findClass(className);
    if (!classInfo) {
        return false;
    }
    // If the class is compile-time, check if it has a matching constructor that is also compile-time
    auto ctorIt = classInfo->methods.find("Constructor");
    if (ctorIt != classInfo->methods.end()) {
        const MethodInfo& ctorInfo = ctorIt->second;
        // Check parameter count
        if (ctorInfo.parameters.size() != paramTypes.size()) {
            return false;
        }
        // Check parameter types
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (ctorInfo.parameters[i].first != paramTypes[i]->typeName) {
                return false;
            }
        }
        // For compile-time instantiation, the constructor must be compile-time
        // (or the class must be compile-time, which makes all constructors compile-time)
        return ctorInfo.isCompiletime || classInfo->isCompiletime;
    }
    return false;
}} // namespace Semantic
} // namespace XXML
