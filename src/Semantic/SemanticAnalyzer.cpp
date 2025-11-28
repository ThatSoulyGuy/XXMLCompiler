#include "../../include/Semantic/SemanticAnalyzer.h"
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

std::string SemanticAnalyzer::getExpressionType(Parser::Expression* expr) {
    auto it = expressionTypes.find(expr);
    if (it != expressionTypes.end()) {
        return it->second;
    }
    return "Unknown";
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
                    methodInfo.templateParams = methodDecl->templateParams;  // Copy params
                    methodInfo.astNode = methodDecl;  // Only valid for same-module access
                    templateMethods[methodKey] = methodInfo;
                }

                MethodInfo methodInfo;
                methodInfo.returnType = methodDecl->returnType->typeName;
                methodInfo.returnOwnership = methodDecl->returnType->ownership;
                methodInfo.isConstructor = (methodDecl->name == "Constructor");

                for (auto& param : methodDecl->parameters) {
                    methodInfo.parameters.push_back({param->type->typeName, param->type->ownership});
                }

                classInfo.methods[methodDecl->name] = methodInfo;
            } else if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                classInfo.properties[propDecl->name] = {propDecl->type->typeName, propDecl->type->ownership};
            } else if (auto* ctorDecl = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                MethodInfo methodInfo;
                methodInfo.returnType = node.name;
                methodInfo.returnOwnership = Parser::OwnershipType::Owned;
                methodInfo.isConstructor = true;

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

    if (initType != "Unknown" && !isCompatibleType(fullTypeName, initType)) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Cannot assign value of type '" + initType + "' to variable of type '" + fullTypeName + "'. " +
            "Template types with different parameters are incompatible (e.g., Vector2<Float> != Vector2<Integer>).",
            node.location
        );
        return;
    }

    // Define the variable symbol
    auto symbol = std::make_unique<Symbol>(
        node.variableName,
        SymbolKind::LocalVariable,
        fullTypeName,  // Use full type name with template args
        node.type->ownership,
        node.location
    );
    symbolTable_->define(node.variableName, std::move(symbol));

    // Register variable type in TypeContext for code generation
    registerVariableType(node.variableName, fullTypeName, node.type->ownership);

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

    if (targetType != "Unknown" && valueType != "Unknown" && !isCompatibleType(targetType, valueType)) {
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

    // Check that condition is Bool
    std::string condType = getExpressionType(node.condition.get());
    if (condType != "Bool" && condType != "Unknown") {
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

    // Check that condition is Bool
    std::string condType = getExpressionType(node.condition.get());
    if (condType != "Bool" && condType != "Unknown") {
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
        expressionTypes[&node] = "Unknown";
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
        // This is a qualified name like System::Print or String::Constructor
        // For now, assume it's valid (imports would handle this)
        expressionTypes[&node] = "Unknown";
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

        if (validNamespaces_.find(node.name) != validNamespaces_.end() || isValidClass) {
            // It's a valid namespace or class name, assume it's part of a qualified expression
            expressionTypes[&node] = "Unknown";
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Check if it's a known intrinsic (Syscall, System, Console, Mem, Language, IO, Test, __typename, etc.)
        if (node.name == "Syscall" ||
            node.name == "System" || node.name == "Console" || node.name == "Mem" ||
            node.name == "Language" || node.name == "IO" || node.name == "Test" ||
            node.name == "__typename") {
            expressionTypes[&node] = "Unknown";
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Don't error on qualified names, constructor calls, or template instantiations
        if (node.name.find("Constructor") != std::string::npos ||
            node.name.find("::") != std::string::npos ||
            node.name.find("<") != std::string::npos) {  // Template instantiation like List<Integer>
            expressionTypes[&node] = "Unknown";
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
        expressionTypes[&node] = "Unknown";
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

    // For now, assume member access is valid
    // Full implementation would look up the member in the class definition
    expressionTypes[&node] = "Unknown"; // Should be looked up
    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
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
                    // Parse template arguments from the string
                    size_t endBracket = className.rfind('>');
                    if (endBracket != std::string::npos) {
                        std::string argsStr = className.substr(angleBracketPos + 1, endBracket - angleBracketPos - 1);

                        // Simple parsing: split by comma
                        std::vector<Parser::TemplateArgument> args;
                        size_t start = 0;
                        size_t comma;
                        while ((comma = argsStr.find(',', start)) != std::string::npos) {
                            std::string arg = argsStr.substr(start, comma - start);
                            // Trim whitespace
                            size_t first = arg.find_first_not_of(" \t");
                            size_t last = arg.find_last_not_of(" \t");
                            if (first != std::string::npos && last != std::string::npos) {
                                arg = arg.substr(first, last - first + 1);
                            }
                            args.emplace_back(arg, node.location);
                            start = comma + 1;
                        }
                        // Add last argument
                        std::string arg = argsStr.substr(start);
                        size_t first = arg.find_first_not_of(" \t");
                        size_t last = arg.find_last_not_of(" \t");
                        if (first != std::string::npos && last != std::string::npos) {
                            arg = arg.substr(first, last - first + 1);
                        }
                        args.emplace_back(arg, node.location);

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
                    registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
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

                            // Skip type checking if argument type is Unknown (template-dependent code)
                            if (argType == "Unknown") {
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

                if (isTemplateInstantiation && isConstructorCall) {
                    // Template instantiation constructor - register the full type name
                    registerExpressionType(&node, className, Parser::OwnershipType::Owned);
                } else {
                    // Builtin type - assume valid
                    registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
                }
            }
            return;
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        // obj.method() call
        std::string objectType = getExpressionType(memberExpr->object.get());
        methodName = memberExpr->member;

        // Special handling for ReflectionContext.getTargetValue() - returns the annotated element's type
        if (inProcessorContext_ && objectType == "ReflectionContext" && methodName == "getTargetValue") {
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
        if (inProcessorContext_ && objectType == "ReflectionContext") {
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
                registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
                return;
            }

            // Legacy type-specific methods (for backwards compatibility and C API usage)
            if (methodName == "getAnnotationArgNameAt") {
                registerExpressionType(&node, "String", Parser::OwnershipType::Owned);
                return;
            }
        }

        // Special handling for CompilationContext intrinsic methods
        if (inProcessorContext_ && objectType == "CompilationContext") {
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

        if (objectType != "Unknown") {
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

                        // Simple parsing: split by comma (assuming single argument for now)
                        std::vector<std::string> args;
                        args.push_back(argsStr); // For now, handle single template arg

                        // Build substitution map (T -> Integer, etc.)
                        for (size_t i = 0; i < templateInfo->templateParams.size() && i < args.size(); ++i) {
                            templateSubstitutions[templateInfo->templateParams[i].name] = args[i];
                        }
                    }
                }
            }

            MethodInfo* method = findMethod(baseType, methodName);

            // Only validate if validation is enabled
            if (enableValidation) {
                // Only exempt Syscall, Mem, Console, Language namespaces, function types from validation (intrinsic functions / stubs)
                // Also exempt __DynamicValue (used for processor target values with unknown types at compile time)
                bool isIntrinsic = (objectType == "Syscall" || objectType == "Mem" ||
                                    objectType == "Console" || objectType == "System::Console" ||
                                    objectType == "__function" || baseType == "__function" ||
                                    objectType == "__DynamicValue" || baseType == "__DynamicValue" ||
                                    objectType.find("Language::") == 0 || baseType.find("Language::") == 0);
                bool isTemplateInstantiation = !templateSubstitutions.empty();
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
                    registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
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
                            for (const auto& [param, arg] : templateSubstitutions) {
                                if (substitutedExpectedType == param) {
                                    substitutedExpectedType = arg;
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

                            // Skip type checking if argument type is Unknown (template-dependent code)
                            if (argType == "Unknown") {
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
                for (const auto& [param, arg] : templateSubstitutions) {
                    // Simple substitution: replace exact matches
                    if (returnType == param) {
                        returnType = arg;
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

                // Builtin type - assume valid
                registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
            }
            return;
        }
    }

    // Default fallback
    registerExpressionType(&node, "Unknown", Parser::OwnershipType::Owned);
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
        if (!typesCompatible && leftType != "Unknown" && rightType != "Unknown") {
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

    // Create instantiation record
    TemplateInstantiation inst;
    inst.templateName = templateName;
    inst.arguments = args;

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
                if (!validateConstraint(arg.typeArg, param.constraints)) {
                    // Build constraint list for error message
                    std::string constraintList;
                    for (size_t j = 0; j < param.constraints.size(); ++j) {
                        if (j > 0) constraintList += " | ";
                        constraintList += param.constraints[j];
                    }
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        "Type '" + arg.typeArg + "' does not satisfy constraint for template parameter '" + param.name + "'.\n"
                        "  Required: " + constraintList + "\n"
                        "  Provided: " + arg.typeArg + "\n"
                        "  Constraint uses union semantics - type must match at least one option.",
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

    // Try each imported namespace if not qualified
    if (className.find("::") == std::string::npos) {
        for (const auto& importedNs : importedNamespaces_) {
            std::string qualifiedName = importedNs + "::" + className;
            it = classRegistry_.find(qualifiedName);
            if (it != classRegistry_.end()) {
                return &it->second;
            }
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

            // Try each imported namespace in global registry
            if (className.find("::") == std::string::npos) {
                for (const auto& importedNs : importedNamespaces_) {
                    std::string qualifiedName = importedNs + "::" + className;
                    git = globalRegistry->find(qualifiedName);
                    if (git != globalRegistry->end()) {
                        return &git->second;
                    }
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

void SemanticAnalyzer::recordMethodTemplateInstantiation(const std::string& className, const std::string& methodName, const std::vector<Parser::TemplateArgument>& args) {
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

    // Create instantiation record
    MethodTemplateInstantiation inst;
    inst.className = className;
    inst.methodName = methodName;
    inst.arguments = args;

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
                if (!validateConstraint(arg.typeArg, param.constraints)) {
                    // Build constraint list for error message
                    std::string constraintList;
                    for (size_t j = 0; j < param.constraints.size(); ++j) {
                        if (j > 0) constraintList += " | ";
                        constraintList += param.constraints[j];
                    }
                    errorReporter.reportError(
                        Common::ErrorCode::TypeMismatch,
                        "Type '" + arg.typeArg + "' does not satisfy constraint '" +
                        constraintList + "' for method template parameter '" + param.name + "'",
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
}

bool SemanticAnalyzer::isTemplateMethod(const std::string& className, const std::string& methodName) {
    std::string methodKey = className + "::" + methodName;
    return templateMethods.find(methodKey) != templateMethods.end();
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

bool SemanticAnalyzer::validateConstraint(const std::string& typeName, const std::vector<std::string>& constraints) {
    // Empty constraints means no restrictions (any type allowed)
    if (constraints.empty()) {
        return true;
    }

    // Check if the type satisfies at least one constraint (union semantics: Type1 | Type2 | ...)
    for (const auto& constraint : constraints) {
        // Parse constraint to extract base name and template arguments
        // e.g., "MyConstraint<T>" -> baseName="MyConstraint", args=["T"]
        std::string baseName = constraint;
        std::vector<std::string> templateArgs;
        std::unordered_map<std::string, std::string> typeSubstitutions;

        size_t angleBracketPos = constraint.find('<');
        if (angleBracketPos != std::string::npos) {
            // This is a parameterized constraint
            baseName = constraint.substr(0, angleBracketPos);

            // Extract template arguments
            size_t endBracket = constraint.rfind('>');
            if (endBracket != std::string::npos) {
                std::string argsStr = constraint.substr(angleBracketPos + 1, endBracket - angleBracketPos - 1);

                // Simple parsing: split by comma (handle single arg for now)
                templateArgs.push_back(argsStr);
            }
        }

        // First, check if this is a constraint definition (has requirements)
        auto constraintIt = constraintRegistry_.find(baseName);
        if (constraintIt != constraintRegistry_.end()) {
            // Build type substitution map for parameterized constraints
            const ConstraintInfo& constraintInfo = constraintIt->second;

            // Map constraint's template parameters to actual type arguments
            for (size_t i = 0; i < constraintInfo.templateParams.size() && i < templateArgs.size(); ++i) {
                const std::string& paramName = constraintInfo.templateParams[i].name;
                const std::string& argValue = templateArgs[i];

                // The argument might be a reference to the type being validated
                // e.g., MyConstraint<T> where T should be substituted with the actual type
                if (argValue == "T" || argValue == paramName) {
                    // Simple case: substitute with the type being validated
                    typeSubstitutions[paramName] = typeName;
                } else {
                    typeSubstitutions[paramName] = argValue;
                }
            }

            // This is a full constraint with requirements - validate them
            if (validateConstraintRequirements(typeName, constraintInfo, Common::SourceLocation(), typeSubstitutions)) {
                return true;  // Constraint satisfied
            }
            // Continue checking other constraints
        } else {
            // This is a simple type constraint (old behavior)
            if (isTypeCompatible(typeName, constraint)) {
                return true;
            }
        }
    }

    // Type doesn't satisfy any constraint
    return false;
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
    // Constructor name is the class name
    auto methodIt = classInfo->methods.find(className);
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
                                                      const std::unordered_map<std::string, std::string>& providedSubstitutions) {
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
                errorReporter.reportError(
                    Common::ErrorCode::ConstraintViolation,
                    "Type '" + typeName + "' does not have required method '" +
                    requirement->methodName + "'",
                    loc
                );
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::Constructor) {
            // Check if the type has the required constructor
            if (!hasConstructor(typeName, requirement->constructorParamTypes)) {
                errorReporter.reportError(
                    Common::ErrorCode::ConstraintViolation,
                    "Type '" + typeName + "' does not have required constructor",
                    loc
                );
                return false;
            }
        } else if (requirement->kind == Parser::RequirementKind::Truth) {
            // Evaluate the truth condition
            if (!evaluateTruthCondition(requirement->truthCondition.get(), typeSubstitutions)) {
                errorReporter.reportError(
                    Common::ErrorCode::ConstraintViolation,
                    "Type '" + typeName + "' does not satisfy truth condition",
                    loc
                );
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
    if (annotationRegistry_.find(node.name) != annotationRegistry_.end()) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateSymbol,
            "Annotation '" + node.name + "' is already defined",
            node.location
        );
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

} // namespace Semantic
} // namespace XXML
