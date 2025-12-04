#include "../../include/Semantic/TypeCanonicalizer.h"
#include <iostream>
#include <functional>

namespace XXML {
namespace Semantic {

TypeCanonicalizer::TypeCanonicalizer(Common::ErrorReporter& errorReporter,
                                     const std::set<std::string>& validNamespaces)
    : errorReporter_(errorReporter),
      currentNamespace_(""),
      currentClass_(""),
      inCollectionPhase_(true) {
    // Copy in provided namespaces
    validNamespaces_ = validNamespaces;
    // Register built-in types
    registerBuiltinTypes();
}

TypeResolutionResult TypeCanonicalizer::run(Parser::Program& program) {
    result_ = TypeResolutionResult{};

    // Phase 1: Collect all type declarations
    inCollectionPhase_ = true;
    collectDeclarations(program);

    // Phase 2: Resolve all types
    inCollectionPhase_ = false;
    resolveAllTypes(program);

    // Validate results
    validateNoCircularDependencies();
    validateAllResolved();

    // Populate result
    result_.resolvedTypes = registeredTypes_;
    result_.validNamespaces = validNamespaces_;

    // Convert pending references to ForwardReference structs
    for (const auto& pending : pendingReferences_) {
        if (!isTypeResolved(pending.typeName)) {
            result_.unresolvedReferences.push_back({
                pending.typeName,
                pending.location,
                pending.referencedFrom
            });
        }
    }

    result_.success = result_.unresolvedReferences.empty() && !errorReporter_.hasErrors();
    return result_;
}

void TypeCanonicalizer::collectDeclarations(Parser::Program& program) {
    visitProgram(program);
}

void TypeCanonicalizer::resolveAllTypes(Parser::Program& program) {
    // Second pass: resolve forward references
    for (auto& [name, type] : registeredTypes_) {
        if (!type.isResolved()) {
            // Try to resolve
            auto resolved = resolveType(name, Common::SourceLocation{});
            if (resolved.isResolved()) {
                type = resolved;
            }
        }
    }
}

const QualifiedType* TypeCanonicalizer::getCanonicalType(const std::string& name) const {
    // Try exact match first
    auto it = registeredTypes_.find(name);
    if (it != registeredTypes_.end()) {
        return &it->second;
    }

    // Try with current namespace prefix
    if (!currentNamespace_.empty()) {
        std::string qualified = currentNamespace_ + "::" + name;
        it = registeredTypes_.find(qualified);
        if (it != registeredTypes_.end()) {
            return &it->second;
        }
    }

    // Try common prefixes
    std::string langCore = "Language::Core::" + name;
    it = registeredTypes_.find(langCore);
    if (it != registeredTypes_.end()) {
        return &it->second;
    }

    return nullptr;
}

bool TypeCanonicalizer::isTypeResolved(const std::string& name) const {
    const QualifiedType* type = getCanonicalType(name);
    return type != nullptr && type->isResolved();
}

bool TypeCanonicalizer::hasUnresolvedTypes() const {
    return !pendingResolution_.empty();
}

std::vector<std::string> TypeCanonicalizer::getUnresolvedTypeNames() const {
    return std::vector<std::string>(pendingResolution_.begin(), pendingResolution_.end());
}

//==============================================================================
// MANUAL AST TRAVERSAL METHODS
//==============================================================================

void TypeCanonicalizer::visitProgram(Parser::Program& node) {
    for (auto& decl : node.declarations) {
        // Dispatch based on declaration type
        if (auto* importDecl = dynamic_cast<Parser::ImportDecl*>(decl.get())) {
            visitImportDecl(*importDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            visitNamespaceDecl(*nsDecl);
        } else if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            visitClassDecl(*classDecl);
        } else if (auto* nativeStruct = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            visitNativeStructureDecl(*nativeStruct);
        } else if (auto* callbackType = dynamic_cast<Parser::CallbackTypeDecl*>(decl.get())) {
            visitCallbackTypeDecl(*callbackType);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            visitEnumerationDecl(*enumDecl);
        } else if (auto* constraintDecl = dynamic_cast<Parser::ConstraintDecl*>(decl.get())) {
            visitConstraintDecl(*constraintDecl);
        } else if (auto* annotationDecl = dynamic_cast<Parser::AnnotationDecl*>(decl.get())) {
            visitAnnotationDecl(*annotationDecl);
        } else if (auto* processorDecl = dynamic_cast<Parser::ProcessorDecl*>(decl.get())) {
            visitProcessorDecl(*processorDecl);
        }
    }
}

void TypeCanonicalizer::visitImportDecl(Parser::ImportDecl& node) {
    // Track imported namespaces
    validNamespaces_.insert(node.modulePath);
}

void TypeCanonicalizer::visitNamespaceDecl(Parser::NamespaceDecl& node) {
    std::string previousNamespace = currentNamespace_;

    if (currentNamespace_.empty()) {
        currentNamespace_ = node.name;
    } else {
        currentNamespace_ = currentNamespace_ + "::" + node.name;
    }

    validNamespaces_.insert(currentNamespace_);

    // Process declarations in this namespace
    for (auto& decl : node.declarations) {
        // Dispatch based on declaration type
        if (auto* importDecl = dynamic_cast<Parser::ImportDecl*>(decl.get())) {
            visitImportDecl(*importDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            visitNamespaceDecl(*nsDecl);
        } else if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            visitClassDecl(*classDecl);
        } else if (auto* nativeStruct = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            visitNativeStructureDecl(*nativeStruct);
        } else if (auto* callbackType = dynamic_cast<Parser::CallbackTypeDecl*>(decl.get())) {
            visitCallbackTypeDecl(*callbackType);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            visitEnumerationDecl(*enumDecl);
        } else if (auto* constraintDecl = dynamic_cast<Parser::ConstraintDecl*>(decl.get())) {
            visitConstraintDecl(*constraintDecl);
        } else if (auto* annotationDecl = dynamic_cast<Parser::AnnotationDecl*>(decl.get())) {
            visitAnnotationDecl(*annotationDecl);
        } else if (auto* processorDecl = dynamic_cast<Parser::ProcessorDecl*>(decl.get())) {
            visitProcessorDecl(*processorDecl);
        }
    }

    currentNamespace_ = previousNamespace;
}

void TypeCanonicalizer::visitClassDecl(Parser::ClassDecl& node) {
    std::string qualifiedName = getFullyQualifiedName(node.name);

    if (inCollectionPhase_) {
        // Register the class type
        QualifiedType classType;
        classType.qualifiedName = qualifiedName;
        classType.simpleName = node.name;
        classType.ownership = Parser::OwnershipType::None;
        classType.isTemplate = !node.templateParams.empty();
        classType.isPrimitive = false;
        classType.isNativeType = false;

        registerType(qualifiedName, classType);

        // Track template parameters as temporarily valid types
        std::set<std::string> previousTemplateParams = templateTypeParameters_;
        for (const auto& param : node.templateParams) {
            templateTypeParameters_.insert(param.name);
        }

        // Process class members
        std::string previousClass = currentClass_;
        currentClass_ = qualifiedName;

        for (auto& section : node.sections) {
            for (auto& member : section->declarations) {
                if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(member.get())) {
                    visitPropertyDecl(*prop);
                } else if (auto* method = dynamic_cast<Parser::MethodDecl*>(member.get())) {
                    visitMethodDecl(*method);
                }
                // Constructors, destructors don't introduce new types
            }
        }

        currentClass_ = previousClass;
        templateTypeParameters_ = previousTemplateParams;
    }
}

void TypeCanonicalizer::visitNativeStructureDecl(Parser::NativeStructureDecl& node) {
    std::string qualifiedName = getFullyQualifiedName(node.name);

    if (inCollectionPhase_) {
        QualifiedType structType;
        structType.qualifiedName = qualifiedName;
        structType.simpleName = node.name;
        structType.ownership = Parser::OwnershipType::None;
        structType.isTemplate = false;
        structType.isPrimitive = false;
        structType.isNativeType = true;

        registerType(qualifiedName, structType);
    }
}

void TypeCanonicalizer::visitCallbackTypeDecl(Parser::CallbackTypeDecl& node) {
    std::string qualifiedName = getFullyQualifiedName(node.name);

    if (inCollectionPhase_) {
        QualifiedType callbackType;
        callbackType.qualifiedName = qualifiedName;
        callbackType.simpleName = node.name;
        callbackType.ownership = Parser::OwnershipType::None;
        callbackType.isTemplate = false;
        callbackType.isPrimitive = false;
        callbackType.isNativeType = false;

        registerType(qualifiedName, callbackType);
    }
}

void TypeCanonicalizer::visitEnumerationDecl(Parser::EnumerationDecl& node) {
    std::string qualifiedName = getFullyQualifiedName(node.name);

    if (inCollectionPhase_) {
        QualifiedType enumType;
        enumType.qualifiedName = qualifiedName;
        enumType.simpleName = node.name;
        enumType.ownership = Parser::OwnershipType::None;
        enumType.isTemplate = false;
        enumType.isPrimitive = false;
        enumType.isNativeType = false;

        registerType(qualifiedName, enumType);
    }
}

void TypeCanonicalizer::visitConstraintDecl(Parser::ConstraintDecl& /* node */) {
    // Constraints don't introduce new types (they constrain existing ones)
}

void TypeCanonicalizer::visitAnnotationDecl(Parser::AnnotationDecl& node) {
    std::string qualifiedName = getFullyQualifiedName(node.name);

    if (inCollectionPhase_) {
        // Annotations are special types
        QualifiedType annotationType;
        annotationType.qualifiedName = qualifiedName;
        annotationType.simpleName = node.name;
        annotationType.ownership = Parser::OwnershipType::None;
        annotationType.isTemplate = false;
        annotationType.isPrimitive = false;
        annotationType.isNativeType = false;

        registerType(qualifiedName, annotationType);
    }
}

void TypeCanonicalizer::visitProcessorDecl(Parser::ProcessorDecl& /* node */) {
    // Processors don't introduce new types
}

void TypeCanonicalizer::visitPropertyDecl(Parser::PropertyDecl& node) {
    if (!inCollectionPhase_ && node.type) {
        // Validate ownership annotation
        validateOwnershipAnnotation(node.type.get(), "property '" + node.name + "'");

        // Record reference for resolution
        QualifiedType resolved = resolveTypeRef(node.type.get());
        if (!resolved.isResolved()) {
            PendingReference ref;
            ref.typeName = node.type->typeName;
            ref.referencedFrom = currentClass_;
            ref.location = node.location;
            pendingReferences_.push_back(ref);
        }
    }
}

void TypeCanonicalizer::visitMethodDecl(Parser::MethodDecl& node) {
    if (!inCollectionPhase_) {
        // Validate return type
        if (node.returnType) {
            QualifiedType resolved = resolveTypeRef(node.returnType.get());
            if (!resolved.isResolved() && !templateTypeParameters_.count(node.returnType->typeName)) {
                PendingReference ref;
                ref.typeName = node.returnType->typeName;
                ref.referencedFrom = currentClass_ + "::" + node.name;
                ref.location = node.location;
                pendingReferences_.push_back(ref);
            }
        }

        // Validate parameter types
        for (auto& param : node.parameters) {
            if (param->type) {
                validateOwnershipAnnotation(param->type.get(),
                    "parameter '" + param->name + "' of method '" + node.name + "'");

                QualifiedType resolved = resolveTypeRef(param->type.get());
                if (!resolved.isResolved() && !templateTypeParameters_.count(param->type->typeName)) {
                    PendingReference ref;
                    ref.typeName = param->type->typeName;
                    ref.referencedFrom = currentClass_ + "::" + node.name;
                    ref.location = param->location;
                    pendingReferences_.push_back(ref);
                }
            }
        }
    }
}

void TypeCanonicalizer::visitTypeRef(Parser::TypeRef* /* typeRef */) {
    // TypeRef resolution is handled in resolveTypeRef()
}

//==============================================================================
// HELPER METHODS
//==============================================================================

std::string TypeCanonicalizer::qualifyTypeName(const std::string& typeName) const {
    // If already qualified, return as-is
    if (typeName.find("::") != std::string::npos) {
        return typeName;
    }

    // If it's a primitive type, return with Language::Core prefix
    if (isPrimitiveType(typeName)) {
        return "Language::Core::" + typeName;
    }

    // If in a namespace, try to qualify with current namespace
    if (!currentNamespace_.empty()) {
        return currentNamespace_ + "::" + typeName;
    }

    return typeName;
}

std::string TypeCanonicalizer::getFullyQualifiedName(const std::string& name) const {
    if (currentNamespace_.empty()) {
        return name;
    }
    return currentNamespace_ + "::" + name;
}

bool TypeCanonicalizer::isPrimitiveType(const std::string& typeName) const {
    return typeName == "Integer" || typeName == "Float" || typeName == "Double" ||
           typeName == "Bool" || typeName == "String" || typeName == "Void" ||
           typeName == "None";
}

bool TypeCanonicalizer::isNativeType(const std::string& typeName) const {
    return typeName.find("NativeType<") == 0;
}

void TypeCanonicalizer::registerType(const std::string& qualifiedName, const QualifiedType& type) {
    registeredTypes_[qualifiedName] = type;

    // Also register by simple name if not already registered
    if (registeredTypes_.find(type.simpleName) == registeredTypes_.end()) {
        registeredTypes_[type.simpleName] = type;
    }
}

void TypeCanonicalizer::registerBuiltinTypes() {
    // Register primitive types
    for (const std::string& name : {"Integer", "Float", "Double", "Bool", "String"}) {
        QualifiedType primitive;
        primitive.qualifiedName = "Language::Core::" + name;
        primitive.simpleName = name;
        primitive.ownership = Parser::OwnershipType::None;
        primitive.isTemplate = false;
        primitive.isPrimitive = true;
        primitive.isNativeType = false;

        registerType(primitive.qualifiedName, primitive);
        registerType(name, primitive);  // Also register unqualified
    }

    // Register Void/None
    QualifiedType voidType;
    voidType.qualifiedName = "Void";
    voidType.simpleName = "Void";
    voidType.ownership = Parser::OwnershipType::None;
    voidType.isTemplate = false;
    voidType.isPrimitive = true;
    voidType.isNativeType = false;
    registerType("Void", voidType);

    QualifiedType noneType;
    noneType.qualifiedName = "None";
    noneType.simpleName = "None";
    noneType.ownership = Parser::OwnershipType::None;
    noneType.isTemplate = false;
    noneType.isPrimitive = true;
    noneType.isNativeType = false;
    registerType("None", noneType);

    // Register Language::Core namespace
    validNamespaces_.insert("Language");
    validNamespaces_.insert("Language::Core");
    validNamespaces_.insert("Language::Collections");
}

QualifiedType TypeCanonicalizer::resolveType(const std::string& typeName,
                                              const Common::SourceLocation& loc) {
    // Check if already resolved
    auto it = registeredTypes_.find(typeName);
    if (it != registeredTypes_.end() && it->second.isResolved()) {
        return it->second;
    }

    // Check if it's a template parameter
    if (templateTypeParameters_.count(typeName)) {
        QualifiedType templateParam;
        templateParam.qualifiedName = typeName;
        templateParam.simpleName = typeName;
        templateParam.ownership = Parser::OwnershipType::None;
        templateParam.isTemplate = true;
        templateParam.isPrimitive = false;
        templateParam.isNativeType = false;
        return templateParam;
    }

    // Try to qualify and resolve
    std::string qualified = qualifyTypeName(typeName);
    it = registeredTypes_.find(qualified);
    if (it != registeredTypes_.end()) {
        return it->second;
    }

    // Handle NativeType<X>
    if (isNativeType(typeName)) {
        QualifiedType nativeType;
        nativeType.qualifiedName = typeName;
        nativeType.simpleName = typeName;
        nativeType.ownership = Parser::OwnershipType::None;
        nativeType.isTemplate = false;
        nativeType.isPrimitive = false;
        nativeType.isNativeType = true;
        return nativeType;
    }

    // Not found - mark as pending
    pendingResolution_.insert(typeName);

    QualifiedType unresolved;
    unresolved.qualifiedName = "";
    unresolved.simpleName = typeName;
    return unresolved;
}

QualifiedType TypeCanonicalizer::resolveTypeRef(Parser::TypeRef* typeRef) {
    if (!typeRef) {
        QualifiedType empty;
        return empty;
    }

    QualifiedType resolved = resolveType(typeRef->typeName, typeRef->location);
    resolved.ownership = typeRef->ownership;

    // Resolve template arguments
    if (!typeRef->templateArgs.empty()) {
        resolveTemplateArguments(resolved, typeRef->templateArgs, typeRef->location);
    }

    return resolved;
}

void TypeCanonicalizer::resolveTemplateArguments(QualifiedType& type,
                                                  const std::vector<Parser::TemplateArgument>& args,
                                                  const Common::SourceLocation& loc) {
    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            QualifiedType argType = resolveType(arg.typeArg, loc);
            type.templateArgs.push_back(argType);
        }
        // Value arguments don't need type resolution
    }
}

void TypeCanonicalizer::validateOwnershipAnnotation(Parser::TypeRef* typeRef,
                                                     const std::string& context) {
    if (!typeRef) return;

    // Template parameters don't require ownership
    if (typeRef->isTemplateParameter || templateTypeParameters_.count(typeRef->typeName)) {
        return;
    }

    // Check for missing ownership
    if (typeRef->ownership == Parser::OwnershipType::None) {
        reportMissingOwnership(typeRef->typeName, context, typeRef->location);
    }
}

void TypeCanonicalizer::validateNoCircularDependencies() {
    // Simple cycle detection using DFS
    // For each type, check if following its dependencies leads back to itself

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> inStack;

    std::function<bool(const std::string&, std::vector<std::string>&)> detectCycle;
    detectCycle = [&](const std::string& typeName, std::vector<std::string>& path) -> bool {
        if (inStack.count(typeName)) {
            // Found cycle
            path.push_back(typeName);
            return true;
        }

        if (visited.count(typeName)) {
            return false;
        }

        visited.insert(typeName);
        inStack.insert(typeName);
        path.push_back(typeName);

        // Check dependencies (template arguments)
        auto it = registeredTypes_.find(typeName);
        if (it != registeredTypes_.end()) {
            for (const auto& dep : it->second.templateArgs) {
                if (detectCycle(dep.qualifiedName, path)) {
                    return true;
                }
            }
        }

        path.pop_back();
        inStack.erase(typeName);
        return false;
    };

    for (const auto& [name, type] : registeredTypes_) {
        std::vector<std::string> path;
        if (detectCycle(name, path)) {
            reportCircularDependency(name, path);
        }
    }
}

void TypeCanonicalizer::validateAllResolved() {
    for (const auto& pending : pendingReferences_) {
        if (!isTypeResolved(pending.typeName) &&
            !templateTypeParameters_.count(pending.typeName)) {
            reportUnresolvedType(pending.typeName, pending.location);
        }
    }
}

//==============================================================================
// ERROR REPORTING
//==============================================================================

void TypeCanonicalizer::reportUnresolvedType(const std::string& typeName,
                                              const Common::SourceLocation& loc) {
    errorReporter_.reportError(
        Common::ErrorCode::UndefinedType,
        "Unresolved type '" + typeName + "'",
        loc
    );
}

void TypeCanonicalizer::reportCircularDependency(const std::string& typeName,
                                                  const std::vector<std::string>& cycle) {
    std::string cycleStr;
    for (size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) cycleStr += " -> ";
        cycleStr += cycle[i];
    }

    errorReporter_.reportError(
        Common::ErrorCode::TypeMismatch,  // TODO: Add specific error code
        "Circular type dependency detected: " + cycleStr,
        Common::SourceLocation{}
    );
}

void TypeCanonicalizer::reportMissingOwnership(const std::string& typeName,
                                                const std::string& context,
                                                const Common::SourceLocation& loc) {
    errorReporter_.reportError(
        Common::ErrorCode::InvalidOwnership,
        "Type '" + typeName + "' in " + context +
        " is missing ownership annotation (^, &, or %)",
        loc
    );
}

} // namespace Semantic
} // namespace XXML
