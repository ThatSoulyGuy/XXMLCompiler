#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include "PassResults.h"
#include "SemanticError.h"

namespace XXML {
namespace Semantic {

// Forward declarations
class SymbolTable;

//==============================================================================
// TYPE CANONICALIZER
//
// This pass ensures all types are fully resolved before any validation occurs.
// It performs two traversals:
//   1. Collection: Register all type declarations
//   2. Resolution: Resolve all forward references
//
// After this pass completes successfully:
//   - No "Unknown" types remain
//   - All forward references are resolved
//   - All type names are canonicalized (namespace-qualified)
//   - Ownership annotations are present on all non-template parameters
//==============================================================================

class TypeCanonicalizer {
public:
    TypeCanonicalizer(Common::ErrorReporter& errorReporter,
                      const std::set<std::string>& validNamespaces);

    // Main entry point - runs both passes
    TypeResolutionResult run(Parser::Program& program);

    // Two-pass resolution
    void collectDeclarations(Parser::Program& program);
    void resolveAllTypes(Parser::Program& program);

    // Query resolved types
    const QualifiedType* getCanonicalType(const std::string& name) const;
    bool isTypeResolved(const std::string& name) const;
    bool hasUnresolvedTypes() const;
    std::vector<std::string> getUnresolvedTypeNames() const;

    // Get the result (call after run())
    const TypeResolutionResult& result() const { return result_; }

private:
    Common::ErrorReporter& errorReporter_;
    TypeResolutionResult result_;

    // State during traversal
    std::string currentNamespace_;
    std::string currentClass_;
    bool inCollectionPhase_ = true;
    std::set<std::string> templateTypeParameters_;  // Currently in scope

    // Type registry
    std::unordered_map<std::string, QualifiedType> registeredTypes_;
    std::unordered_set<std::string> pendingResolution_;
    std::set<std::string> validNamespaces_;

    // Manual AST traversal methods (instead of visitor pattern)
    void visitProgram(Parser::Program& node);
    void visitImportDecl(Parser::ImportDecl& node);
    void visitNamespaceDecl(Parser::NamespaceDecl& node);
    void visitClassDecl(Parser::ClassDecl& node);
    void visitNativeStructureDecl(Parser::NativeStructureDecl& node);
    void visitCallbackTypeDecl(Parser::CallbackTypeDecl& node);
    void visitEnumerationDecl(Parser::EnumerationDecl& node);
    void visitConstraintDecl(Parser::ConstraintDecl& node);
    void visitAnnotationDecl(Parser::AnnotationDecl& node);
    void visitProcessorDecl(Parser::ProcessorDecl& node);
    void visitPropertyDecl(Parser::PropertyDecl& node);
    void visitMethodDecl(Parser::MethodDecl& node);
    void visitConstructorDecl(Parser::ConstructorDecl& node);
    void visitTypeRef(Parser::TypeRef* typeRef);

    // Forward reference tracking
    struct PendingReference {
        std::string typeName;
        std::string referencedFrom;
        Common::SourceLocation location;
    };
    std::vector<PendingReference> pendingReferences_;

    // Circular dependency detection
    std::unordered_set<std::string> resolutionStack_;

    // Helper methods
    std::string qualifyTypeName(const std::string& typeName) const;
    std::string getFullyQualifiedName(const std::string& name) const;
    bool isPrimitiveType(const std::string& typeName) const;
    bool isNativeType(const std::string& typeName) const;

    // Type registration
    void registerType(const std::string& qualifiedName,
                     const QualifiedType& type);
    void registerBuiltinTypes();

    // Type resolution
    QualifiedType resolveType(const std::string& typeName,
                              const Common::SourceLocation& loc);
    QualifiedType resolveTypeRef(Parser::TypeRef* typeRef);
    void resolveTemplateArguments(QualifiedType& type,
                                  const std::vector<Parser::TemplateArgument>& args,
                                  const Common::SourceLocation& loc);

    // Validation
    void validateOwnershipAnnotation(Parser::TypeRef* typeRef,
                                     const std::string& context);
    void validateNoCircularDependencies();
    void validateAllResolved();

    // Error reporting
    void reportUnresolvedType(const std::string& typeName,
                              const Common::SourceLocation& loc);
    void reportCircularDependency(const std::string& typeName,
                                  const std::vector<std::string>& cycle);
    void reportMissingOwnership(const std::string& typeName,
                               const std::string& context,
                               const Common::SourceLocation& loc);
};

//==============================================================================
// BUILTIN TYPES
//==============================================================================

// List of primitive types that don't require resolution
inline const std::vector<std::string>& getBuiltinTypes() {
    static const std::vector<std::string> types = {
        "Integer", "Float", "Double", "Bool", "String",
        "Void", "None", "Unknown"
    };
    return types;
}

// List of qualified primitive types (with namespace)
inline const std::vector<std::string>& getQualifiedBuiltinTypes() {
    static const std::vector<std::string> types = {
        "Language::Core::Integer",
        "Language::Core::Float",
        "Language::Core::Double",
        "Language::Core::Bool",
        "Language::Core::String"
    };
    return types;
}

} // namespace Semantic
} // namespace XXML
