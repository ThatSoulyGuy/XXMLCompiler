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
      enableValidation(true) {
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
      enableValidation(true) {
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

    // Note: If the module isn't found in the registry, importAllFrom will print a warning
    // This is acceptable since not all imports may be resolved at semantic analysis time
}

void SemanticAnalyzer::visit(Parser::NamespaceDecl& node) {
    std::string previousNamespace = currentNamespace;
    currentNamespace = node.name;

    // Register namespace as valid
    validNamespaces_.insert(node.name);

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
        templateClasses[node.name] = &node;
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

    // Process access sections (normal visitor pattern)
    for (auto& section : node.sections) {
        section->accept(*this);
    }

    if (symbolTable_) {
        symbolTable_->exitScope();
    }
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
        // Only error if it's not a NativeType or builtin type
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
    if (existing && existing->kind == SymbolKind::Method) {
        errorReporter.reportError(
            Common::ErrorCode::DuplicateDeclaration,
            "Method '" + node.name + "' already declared",
            node.location
        );
        return;
    }

    // Define the method symbol
    auto symbol = std::make_unique<Symbol>(
        node.name,
        SymbolKind::Method,
        node.returnType->typeName,
        node.returnType->ownership,
        node.location
    );
    symbolTable_->define(node.name, std::move(symbol));

    // Enter method scope
    symbolTable_->enterScope(node.name);

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
    // Don't error if type is qualified (contains ::), NativeType, or a builtin type
    if (!typeSym &&
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

    // Analyze initializer
    node.initializer->accept(*this);

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
        // Don't error on qualified names or constructor calls
        if (node.name.find("Constructor") != std::string::npos ||
            node.name.find("::") != std::string::npos) {
            expressionTypes[&node] = "Unknown";
            expressionOwnerships[&node] = Parser::OwnershipType::Owned;
            return;
        }

        errorReporter.reportError(
            Common::ErrorCode::UndeclaredIdentifier,
            "Undeclared identifier '" + node.name + "'",
            node.location
        );
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

    // Check if this is a qualified call like Class::method() or obj.method()
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.callee.get())) {
        std::string fullName = identExpr->name;

        // Check if it contains :: (static method call)
        size_t colonPos = fullName.rfind("::");
        if (colonPos != std::string::npos) {
            className = fullName.substr(0, colonPos);
            methodName = fullName.substr(colonPos + 2);

            // Validate the qualified identifier
            validateQualifiedIdentifier(className, node.location);

            // Look up the method
            MethodInfo* method = findMethod(className, methodName);

            // Only validate if validation is enabled
            if (enableValidation) {
                // Only exempt Syscall and StringArray from validation (intrinsic functions / stubs)
                bool isIntrinsic = (className == "Syscall" || className == "StringArray");

                if (!method && !isIntrinsic) {
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
                // Only exempt Syscall and StringArray from validation (intrinsic functions / stubs)
                bool isIntrinsic = (objectType == "Syscall" || objectType == "StringArray");

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

void SemanticAnalyzer::recordTemplateInstantiation(const std::string& templateName, const std::vector<std::string>& args) {
    // Check if this template class exists
    if (templateClasses.find(templateName) == templateClasses.end()) {
        // Not a template class - maybe it's a regular class or error will be caught elsewhere
        return;
    }

    // Add to instantiations set
    templateInstantiations.insert({templateName, args});

    // TODO: Validate constraints
    auto* templateClass = templateClasses[templateName];
    if (templateClass->templateParams.size() != args.size()) {
        errorReporter.reportError(
            Common::ErrorCode::TypeMismatch,
            "Template '" + templateName + "' expects " +
            std::to_string(templateClass->templateParams.size()) +
            " arguments but got " + std::to_string(args.size()),
            Common::SourceLocation{} // We'd need to pass location from TypeRef
        );
    }
}

bool SemanticAnalyzer::isTemplateClass(const std::string& className) {
    return templateClasses.find(className) != templateClasses.end();
}

SemanticAnalyzer::ClassInfo* SemanticAnalyzer::findClass(const std::string& className) {
    // Try exact match first
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

    // Try without namespace prefix (for fully qualified lookups)
    size_t lastColon = className.rfind("::");
    if (lastColon != std::string::npos) {
        std::string baseName = className.substr(lastColon + 2);
        it = classRegistry_.find(baseName);
        if (it != classRegistry_.end()) {
            return &it->second;
        }
    }

    // Try all registered classes - match by simple name if nothing else works
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
            if (validNamespaces_.find(accumulated) == validNamespaces_.end() &&
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

} // namespace Semantic
} // namespace XXML
