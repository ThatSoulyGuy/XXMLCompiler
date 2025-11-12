#include "../../include/Semantic/SemanticAnalyzer.h"
#include "../../include/Core/CompilationContext.h"
#include "../../include/Core/TypeRegistry.h"
#include "../../include/Core/FormatCompat.h"
#include <iostream>

namespace XXML {
namespace Semantic {

// ✅ REMOVED STATIC MEMBERS - now instance-based

// New constructor with CompilationContext
SemanticAnalyzer::SemanticAnalyzer(Core::CompilationContext& context, Common::ErrorReporter& reporter)
    : symbolTable_(&context.symbolTable()),
      context_(&context),
      errorReporter(reporter),
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
    // For now, exact match required
    // Future: implement inheritance checking
    return expected == actual || expected == "None" || actual == "None";
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

    symbolTable_->exitScope();
    currentNamespace = previousNamespace;
}

void SemanticAnalyzer::visit(Parser::ClassDecl& node) {
    std::string previousClass = currentClass;
    currentClass = node.name;

    // Record template class if it has template parameters
    if (!node.templateParams.empty()) {
        std::string qualifiedTemplateName = currentNamespace.empty() ? node.name : currentNamespace + "::" + node.name;
        templateClasses[qualifiedTemplateName] = &node;
        // Also register without namespace for convenience
        if (!currentNamespace.empty()) {
            templateClasses[node.name] = &node;
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
    classInfo.astNode = &node;

    // Collect methods and properties by processing sections
    for (auto& section : node.sections) {
        for (auto& decl : section->declarations) {
            if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Register template methods
                if (!methodDecl->templateParams.empty()) {
                    std::string methodKey = qualifiedClassName + "::" + methodDecl->name;
                    templateMethods[methodKey] = methodDecl;
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
            (*globalRegistry)[qualifiedClassName] = classInfo;
            if (!currentNamespace.empty()) {
                (*globalRegistry)[node.name] = classInfo;
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
    if (!typeSym && node.type->typeName.find("NativeType<") != 0) {
        // ✅ Phase 5: Skip validation if this is a template parameter in a template definition
        bool isTemplateParam = (inTemplateDefinition &&
                                templateTypeParameters.find(node.type->typeName) != templateTypeParameters.end());

        // Only error if it's not a NativeType, builtin type, or template parameter
        if (!isTemplateParam &&
            node.type->typeName != "Integer" &&
            node.type->typeName != "String" &&
            node.type->typeName != "Bool" &&
            node.type->typeName != "Float" &&
            node.type->typeName != "Double" &&
            node.type->typeName != "None") {
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

void SemanticAnalyzer::visit(Parser::MethodDecl& node) {
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

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    symbolTable_->exitScope();
}

// Statement visitors
void SemanticAnalyzer::visit(Parser::InstantiateStmt& node) {
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

    // ✅ Phase 5: Skip validation if this is a template parameter in a template definition
    bool isTemplateParam = (inTemplateDefinition &&
                            templateTypeParameters.find(node.type->typeName) != templateTypeParameters.end());

    // Don't error if type is qualified (contains ::), NativeType, template parameter, or a builtin type
    if (!typeSym && !isTemplateParam &&
        node.type->typeName.find("::") == std::string::npos &&
        node.type->typeName.find("NativeType<") != 0) {
        // Only error for simple unqualified types that aren't found
        if (node.type->typeName != "Integer" &&
            node.type->typeName != "String" &&
            node.type->typeName != "Bool" &&
            node.type->typeName != "Float" &&
            node.type->typeName != "Double" &&
            node.type->typeName != "None") {
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

    // Type check initializer (but be lenient with Unknown types from method calls)
    std::string initType = getExpressionType(node.initializer.get());
    if (initType != "Unknown" && !isCompatibleType(node.type->typeName, initType)) {
        // Only warn, don't error, for type mismatches with Unknown
        // This allows method calls and constructor calls to work
    }

    // Define the variable symbol
    auto symbol = std::make_unique<Symbol>(
        node.variableName,
        SymbolKind::LocalVariable,
        node.type->typeName,
        node.type->ownership,
        node.location
    );
    symbolTable_->define(node.variableName, std::move(symbol));
}

void SemanticAnalyzer::visit(Parser::AssignmentStmt& node) {
    // Check if variable exists
    Symbol* existing = symbolTable_->resolve(node.variableName);
    if (!existing) {
        errorReporter.reportError(
            Common::ErrorCode::UndeclaredIdentifier,
            "Variable '" + node.variableName + "' not declared",
            node.location
        );
        return;
    }

    // Analyze the value expression
    node.value->accept(*this);

    // Type check (but be lenient with Unknown types)
    std::string valueType = getExpressionType(node.value.get());
    if (valueType != "Unknown" && !isCompatibleType(existing->typeName, valueType)) {
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

    // Analyze range expressions
    node.rangeStart->accept(*this);
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
    expressionTypes[&node] = "Integer";
    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
}

void SemanticAnalyzer::visit(Parser::StringLiteralExpr& node) {
    expressionTypes[&node] = "String";
    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
}

void SemanticAnalyzer::visit(Parser::BoolLiteralExpr& node) {
    expressionTypes[&node] = "Bool";
    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
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
        if (validNamespaces_.find(node.name) != validNamespaces_.end() ||
            classRegistry_.find(node.name) != classRegistry_.end()) {
            // It's a valid namespace or class name, assume it's part of a qualified expression
            expressionTypes[&node] = "Unknown";
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        // Check if it's a known intrinsic (Syscall, System, Console, Mem, etc.)
        if (node.name == "Syscall" ||
            node.name == "System" || node.name == "Console" || node.name == "Mem") {
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

    expressionTypes[&node] = sym->typeName;
    expressionOwnerships[&node] = sym->ownership;
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
        std::cerr << "DEBUG: fullName = '" << fullName << "'" << std::endl;
    }

    // Check if this is a qualified static call like Class::method()
    if (!fullName.empty()) {
        // Use template-aware parsing to handle names like "List<Integer>::Constructor"
        className = extractClassName(fullName);
        methodName = extractMethodName(fullName);

        std::cerr << "DEBUG: className = '" << className << "', methodName = '" << methodName << "'" << std::endl;

        // Only treat it as a static call if we successfully extracted a class name
        if (!className.empty()) {
            // Check if this is a template instantiation (e.g., "Test::Box<Integer>")
            size_t angleBracketPos = className.find('<');
            if (angleBracketPos != std::string::npos) {
                // Extract template name and arguments
                std::string templateName = className.substr(0, angleBracketPos);
                std::cerr << "DEBUG: Found template instantiation, templateName = '" << templateName << "'" << std::endl;

                // Check if it's a template class
                bool isTemplate = isTemplateClass(templateName);
                std::cerr << "DEBUG: isTemplateClass('" << templateName << "') = " << isTemplate << std::endl;
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

                // Only exempt Syscall, Mem, Console, and template instantiations from validation
                bool isIntrinsic = (className == "Syscall" || className == "Mem" ||
                                    className == "Console" || className == "System::Console");

                if (!method && !isIntrinsic && !isTemplateInstantiation) {
                    errorReporter.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Method '" + methodName + "' not found in class '" + className + "'",
                        node.location
                    );
                    expressionTypes[&node] = "Unknown";
                    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                    return;
                }
            }

            if (method) {
                expressionTypes[&node] = method->returnType;
                expressionOwnerships[&node] = method->returnOwnership;
            } else {
                // Builtin type - assume valid
                expressionTypes[&node] = "Unknown";
                expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            }
            return;
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        // obj.method() call
        std::string objectType = getExpressionType(memberExpr->object.get());
        methodName = memberExpr->member;

        if (objectType != "Unknown") {
            MethodInfo* method = findMethod(objectType, methodName);

            // Only validate if validation is enabled
            if (enableValidation) {
                // Only exempt Syscall, Mem, and Console from validation (intrinsic functions / stubs)
                bool isIntrinsic = (objectType == "Syscall" || objectType == "Mem" ||
                                    objectType == "Console" || objectType == "System::Console");

                if (!method && !isIntrinsic) {
                    errorReporter.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Method '" + methodName + "' not found in class '" + objectType + "'",
                        node.location
                    );
                    expressionTypes[&node] = "Unknown";
                    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
                    return;
                }
            }

            if (method) {
                expressionTypes[&node] = method->returnType;
                expressionOwnerships[&node] = method->returnOwnership;
            } else {
                // Builtin type - assume valid
                expressionTypes[&node] = "Unknown";
                expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            }
            return;
        }
    }

    // Default fallback
    expressionTypes[&node] = "Unknown";
    expressionOwnerships[&node] = Parser::OwnershipType::Owned;
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

        // For arithmetic operators, both sides should be compatible
        if (leftType != rightType && leftType != "Unknown" && rightType != "Unknown") {
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
    // Check if this template class exists
    if (templateClasses.find(templateName) == templateClasses.end()) {
        // Not a template class - maybe it's a regular class or error will be caught elsewhere
        return;
    }

    auto* templateClass = templateClasses[templateName];

    // ✅ Phase 7: Improved error message for argument count mismatch
    // Validate argument count
    if (templateClass->templateParams.size() != args.size()) {
        // Build parameter list for better error message
        std::string paramList;
        for (size_t i = 0; i < templateClass->templateParams.size(); ++i) {
            if (i > 0) paramList += ", ";
            paramList += templateClass->templateParams[i].name;
        }

        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Template '" + templateName + "' expects " +
            std::to_string(templateClass->templateParams.size()) +
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
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto& param = templateClass->templateParams[i];

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
    return templateClasses.find(className) != templateClasses.end();
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
    // Split by :: to get components
    std::vector<std::string> components;
    size_t start = 0;
    size_t pos = 0;
    while ((pos = qualifiedName.find("::", start)) != std::string::npos) {
        components.push_back(qualifiedName.substr(start, pos - start));
        start = pos + 2;
    }
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
                                accumulated == "Mem" || accumulated == "Language::Core::Mem");

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
        std::cerr << "DEBUG buildQualifiedName: IdentifierExpr = '" << identExpr->name << "'" << std::endl;
        return identExpr->name;
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        std::string member = memberExpr->member;
        std::cerr << "DEBUG buildQualifiedName: MemberAccessExpr member = '" << member << "'" << std::endl;

        // Check if this is a static call (member starts with ::)
        if (member.length() >= 2 && member[0] == ':' && member[1] == ':') {
            // Remove :: prefix
            member = member.substr(2);

            // Recursively get the object name
            std::string objectName = buildQualifiedName(memberExpr->object.get());

            std::string result = objectName.empty() ? member : (objectName + "::" + member);
            std::cerr << "DEBUG buildQualifiedName: returning '" << result << "'" << std::endl;
            return result;
        } else {
            // This is an instance call (dot access), not a static call
            std::cerr << "DEBUG buildQualifiedName: instance call, returning empty" << std::endl;
            return "";
        }
    }
    std::cerr << "DEBUG buildQualifiedName: unknown expression type, returning empty" << std::endl;
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
    if (templateMethods.find(methodKey) == templateMethods.end()) {
        // Not a template method - might be a regular method or error caught elsewhere
        return;
    }

    auto* templateMethod = templateMethods[methodKey];

    // Validate argument count
    if (templateMethod->templateParams.size() != args.size()) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Method template '" + methodName + "' expects " +
            std::to_string(templateMethod->templateParams.size()) +
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
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto& param = templateMethod->templateParams[i];

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
    // For now, simple check: if actualClass has a base class, check it
    if (actualClass->astNode && !actualClass->astNode->baseClass.empty()) {
        if (actualClass->astNode->baseClass == constraintType) {
            return true;
        }
        // Recursive check up the inheritance chain
        return isTypeCompatible(actualClass->astNode->baseClass, constraintType);
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
        if (isTypeCompatible(typeName, constraint)) {
            return true;
        }
    }

    // Type doesn't satisfy any constraint
    return false;
}

} // namespace Semantic
} // namespace XXML
