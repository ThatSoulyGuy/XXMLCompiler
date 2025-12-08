#pragma once

#include "Parser/AST.h"
#include <memory>
#include <unordered_map>
#include <set>
#include <string>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Represents a nested template instantiation discovered during AST cloning
 */
struct NestedTemplateInstantiation {
    std::string templateName;
    std::vector<Parser::TemplateArgument> arguments;

    bool operator<(const NestedTemplateInstantiation& other) const {
        if (templateName != other.templateName) return templateName < other.templateName;
        if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (arguments[i].typeArg != other.arguments[i].typeArg) {
                return arguments[i].typeArg < other.arguments[i].typeArg;
            }
        }
        return false;
    }
};

/**
 * @brief Utility class for cloning and substituting AST nodes during template instantiation
 *
 * This class provides methods to deep-clone AST nodes while applying type substitutions.
 * It's used during template monomorphization to create concrete instantiations of
 * template classes, methods, and lambdas.
 */
class ASTCloner {
public:
    using TypeMap = std::unordered_map<std::string, std::string>;

    ASTCloner() = default;
    ~ASTCloner() = default;

    // Non-copyable
    ASTCloner(const ASTCloner&) = delete;
    ASTCloner& operator=(const ASTCloner&) = delete;

    // === Class Cloning ===

    /**
     * Clone a class declaration with type substitution and a new mangled name
     * @param original The original template class
     * @param newName The mangled name for the instantiation (e.g., "List_Integer")
     * @param typeMap Map from template parameter names to concrete types
     * @return A new ClassDecl with all types substituted
     */
    std::unique_ptr<Parser::ClassDecl> cloneClass(
        Parser::ClassDecl* original,
        const std::string& newName,
        const TypeMap& typeMap);

    // === Method Cloning ===

    /**
     * Clone a method declaration with type substitution
     * @param original The original method
     * @param typeMap Map from template parameter names to concrete types
     * @return A new MethodDecl with all types substituted
     */
    std::unique_ptr<Parser::MethodDecl> cloneMethod(
        const Parser::MethodDecl* original,
        const TypeMap& typeMap);

    /**
     * Clone a method declaration with type substitution and a new mangled name
     * @param original The original template method
     * @param newName The mangled name for the instantiation
     * @param typeMap Map from template parameter names to concrete types
     * @return A new MethodDecl with all types substituted
     */
    std::unique_ptr<Parser::MethodDecl> cloneMethodWithName(
        Parser::MethodDecl* original,
        const std::string& newName,
        const TypeMap& typeMap);

    // === Type Cloning ===

    /**
     * Clone a type reference with substitution
     * @param original The original type reference
     * @param typeMap Map from template parameter names to concrete types
     * @return A new TypeRef with all types substituted
     */
    std::unique_ptr<Parser::TypeRef> cloneType(
        const Parser::TypeRef* original,
        const TypeMap& typeMap);

    // === Property Cloning ===

    /**
     * Clone a property declaration with type substitution
     */
    std::unique_ptr<Parser::PropertyDecl> cloneProperty(
        const Parser::PropertyDecl* original,
        const TypeMap& typeMap);

    // === Statement Cloning ===

    /**
     * Clone a statement with type substitution (recursive)
     */
    std::unique_ptr<Parser::Statement> cloneStmt(
        const Parser::Statement* original,
        const TypeMap& typeMap);

    // === Expression Cloning ===

    /**
     * Clone an expression with type substitution (recursive)
     * Also handles __typename intrinsic substitution
     */
    std::unique_ptr<Parser::Expression> cloneExpr(
        const Parser::Expression* original,
        const TypeMap& typeMap);

    /**
     * Clone a lambda expression with type substitution
     */
    std::unique_ptr<Parser::LambdaExpr> cloneLambda(
        const Parser::LambdaExpr* original,
        const TypeMap& typeMap);

    // === Nested Template Instantiation Tracking ===

    /**
     * Get all nested template instantiations discovered during cloning
     * These are types like ListIterator<Integer> found in return types, parameters, etc.
     */
    const std::set<NestedTemplateInstantiation>& getNestedInstantiations() const {
        return nestedInstantiations_;
    }

    /**
     * Clear the nested instantiations list (call before cloning a new class)
     */
    void clearNestedInstantiations() {
        nestedInstantiations_.clear();
    }

private:
    /**
     * Record a nested template instantiation discovered during type cloning
     */
    void recordNestedInstantiation(const std::string& typeName,
                                    const std::vector<Parser::TemplateArgument>& args);

    std::set<NestedTemplateInstantiation> nestedInstantiations_;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
