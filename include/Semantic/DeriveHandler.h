#pragma once

#include "Parser/AST.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace XXML {
namespace Semantic {

class SemanticAnalyzer;

/**
 * @brief Result of a derive operation
 */
struct DeriveResult {
    // Generated methods to add to the class
    std::vector<std::unique_ptr<Parser::MethodDecl>> methods;

    // Generated properties to add to the class (rare, but supported)
    std::vector<std::unique_ptr<Parser::PropertyDecl>> properties;

    // Any errors encountered during generation
    std::vector<std::string> errors;

    bool hasErrors() const { return !errors.empty(); }
};

/**
 * @brief Base class for derive handlers
 *
 * Derive handlers are responsible for generating code based on compile-time
 * reflection of a class. Each handler implements a specific derive trait
 * like ToString, Eq, Hash, Json, etc.
 *
 * Example usage in XXML:
 *   [ Derive <ToString> ]
 *   [ Class <Person> Extends None
 *       [ Public <>
 *           Property <name> Types String^;
 *           Property <age> Types Integer^;
 *       ]
 *   ]
 *
 * The ToString derive handler will generate:
 *   Method <toString> Returns String^ Parameters () Do {
 *       Return String::Constructor("Person{name=").concat(name.toString())
 *           .concat(String::Constructor(", age=")).concat(age.toString())
 *           .concat(String::Constructor("}"));
 *   }
 */
class DeriveHandler {
public:
    virtual ~DeriveHandler() = default;

    /**
     * @brief Get the name of this derive trait (e.g., "ToString", "Eq", "Hash")
     */
    virtual std::string getDeriveName() const = 0;

    /**
     * @brief Generate methods/properties for a class
     *
     * @param classDecl The class declaration to generate for
     * @param analyzer The semantic analyzer (for type lookups, etc.)
     * @return DeriveResult containing generated declarations or errors
     */
    virtual DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) = 0;

    /**
     * @brief Check if this derive can be applied to a class
     *
     * For example, Eq derive might require that all fields are themselves Eq.
     *
     * @param classDecl The class to check
     * @param analyzer The semantic analyzer
     * @return Empty string if valid, error message if not
     */
    virtual std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) {
        return "";  // Default: always valid
    }

protected:
    /**
     * @brief Helper to create a type reference
     */
    std::unique_ptr<Parser::TypeRef> makeType(
        const std::string& typeName,
        Parser::OwnershipType ownership = Parser::OwnershipType::Owned);

    /**
     * @brief Helper to create a parameter declaration
     */
    std::unique_ptr<Parser::ParameterDecl> makeParam(
        const std::string& name,
        const std::string& typeName,
        Parser::OwnershipType ownership = Parser::OwnershipType::Owned);

    /**
     * @brief Helper to create an identifier expression
     */
    std::unique_ptr<Parser::IdentifierExpr> makeIdent(const std::string& name);

    /**
     * @brief Helper to create a member access expression (e.g., obj.member)
     */
    std::unique_ptr<Parser::MemberAccessExpr> makeMemberAccess(
        std::unique_ptr<Parser::Expression> object,
        const std::string& member);

    /**
     * @brief Helper to create a method call expression
     */
    std::unique_ptr<Parser::CallExpr> makeCall(
        std::unique_ptr<Parser::Expression> callee,
        std::vector<std::unique_ptr<Parser::Expression>> args = {});

    /**
     * @brief Helper to create a static call expression (Class::method())
     */
    std::unique_ptr<Parser::CallExpr> makeStaticCall(
        const std::string& className,
        const std::string& methodName,
        std::vector<std::unique_ptr<Parser::Expression>> args = {});

    /**
     * @brief Helper to create a return statement
     */
    std::unique_ptr<Parser::ReturnStmt> makeReturn(
        std::unique_ptr<Parser::Expression> value);

    /**
     * @brief Helper to create a string literal
     */
    std::unique_ptr<Parser::StringLiteralExpr> makeStringLiteral(const std::string& value);

    /**
     * @brief Helper to get all public properties from a class
     */
    std::vector<Parser::PropertyDecl*> getPublicProperties(Parser::ClassDecl* classDecl);

    /**
     * @brief Helper to get all properties from a class (any visibility)
     */
    std::vector<Parser::PropertyDecl*> getAllProperties(Parser::ClassDecl* classDecl);

    // ==================== Ownership Helpers ====================

    /**
     * @brief Get the ownership type of a property
     * @return The OwnershipType enum value
     */
    Parser::OwnershipType getPropertyOwnership(Parser::PropertyDecl* prop);

    /**
     * @brief Check if a property has owned (^) ownership
     */
    bool isPropertyOwned(Parser::PropertyDecl* prop);

    /**
     * @brief Check if a property has reference (&) ownership
     */
    bool isPropertyReference(Parser::PropertyDecl* prop);

    /**
     * @brief Check if a property has copy (%) ownership
     */
    bool isPropertyCopy(Parser::PropertyDecl* prop);

    /**
     * @brief Check if all public properties have a specific ownership type
     */
    bool allPropertiesHaveOwnership(
        Parser::ClassDecl* classDecl,
        Parser::OwnershipType ownership);

    /**
     * @brief Get ownership type as string (for error messages)
     */
    std::string ownershipToString(Parser::OwnershipType ownership);

    /**
     * @brief Get ownership symbol (^, &, %)
     */
    std::string ownershipToSymbol(Parser::OwnershipType ownership);

    /**
     * @brief Check if a property's type can be safely compared (for Eq derive)
     * Properties with reference ownership might need special handling
     */
    bool canCompareProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer);

    /**
     * @brief Check if a property's type can be safely hashed (for Hash derive)
     */
    bool canHashProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer);

    /**
     * @brief Check if a property's type can be safely stringified (for ToString derive)
     */
    bool canStringifyProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer);
};

} // namespace Semantic

// Forward declaration for in-language derive support
namespace Derive {
class InLanguageDeriveRegistry;
class InLanguageDeriveHandler;
} // namespace Derive

namespace Semantic {

/**
 * @brief Registry for derive handlers
 *
 * Manages all available derive handlers and processes derive annotations.
 * Can also delegate to InLanguageDeriveRegistry for user-defined derives.
 */
class DeriveRegistry {
public:
    DeriveRegistry();
    ~DeriveRegistry() = default;

    /**
     * @brief Register a derive handler
     */
    void registerHandler(std::unique_ptr<DeriveHandler> handler);

    /**
     * @brief Get a handler by name
     * @return The handler, or nullptr if not found
     */
    DeriveHandler* getHandler(const std::string& name);

    /**
     * @brief Process all derive annotations on a class
     *
     * @param classDecl The class with derive annotations
     * @param analyzer The semantic analyzer
     * @return True if all derives succeeded
     */
    bool processClassDerives(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer);

    /**
     * @brief Get list of all registered derive names
     */
    std::vector<std::string> getAvailableDerives() const;

    /**
     * @brief Set the in-language derive registry for user-defined derives
     * @param registry Pointer to InLanguageDeriveRegistry (not owned)
     */
    void setInLanguageRegistry(Derive::InLanguageDeriveRegistry* registry);

private:
    std::unordered_map<std::string, std::unique_ptr<DeriveHandler>> handlers_;
    Derive::InLanguageDeriveRegistry* inLanguageRegistry_ = nullptr;
};

} // namespace Semantic
} // namespace XXML
