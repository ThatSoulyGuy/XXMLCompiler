#pragma once

#include "Parser/AST.h"
#include "CppTypeModel.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace XXML {
namespace Core {

/**
 * Resolved type information for an expression or variable.
 * This struct captures all information needed for code generation
 * without requiring additional type analysis during emission.
 *
 * It combines XXML type information with C++ semantic understanding.
 */
struct ResolvedType {
    std::string xxmlTypeName;      // XXML type name (e.g., "Integer", "MyClass")
    std::string cppTypeName;       // Fully qualified C++ type (e.g., "Language::Core::Integer")
    Parser::OwnershipType ownership;  // Owned (^), Reference (&), Copy (%), None
    CppTypeInfo cppTypeInfo;       // Deep C++ semantic information
    bool isBuiltin;                // Whether this is a builtin/core type
    bool isPrimitive;              // Whether this is a primitive value type (Integer, Bool, etc.)

    ResolvedType()
        : ownership(Parser::OwnershipType::None)
        , isBuiltin(false)
        , isPrimitive(false) {}

    // Whether this becomes a smart pointer in C++ (Owned<T>)
    bool isSmartPointer() const {
        return cppTypeInfo.isSmartPointer();
    }

    // Whether member access on this type should use -> operator
    bool needsDereference() const {
        return cppTypeInfo.getMemberAccessOperator() == "->";
    }

    // Whether this type is wrapped in Owned<T>
    bool isOwnedWrapper() const {
        return cppTypeInfo.getCategory() == CppTypeCategory::OwnedPtr;
    }

    // Get the appropriate member access operator (. or ->)
    std::string getMemberOperator() const {
        return cppTypeInfo.getMemberAccessOperator();
    }

    // Get the expression to unwrap Owned<T> -> T
    std::string getUnwrapExpression() const {
        return cppTypeInfo.getUnwrapExpression();
    }

    // Get the wrapped type for smart pointers (e.g., "Integer" from "Owned<Integer>")
    std::string getWrappedType() const {
        return cppTypeInfo.getWrappedType();
    }
};

/**
 * TypeContext stores all resolved type information for expressions and variables.
 * It is populated by SemanticAnalyzer during type checking and queried by
 * CodeGenerator during code emission.
 *
 * This class provides the single source of truth for type information,
 * eliminating the need for ad-hoc type analysis during code generation.
 */
class TypeContext {
public:
    TypeContext();

    // ========================================================================
    // C++ Type Analysis
    // ========================================================================

    /**
     * Get the C++ type analyzer for semantic understanding of C++ types.
     */
    CppTypeAnalyzer& getCppAnalyzer() { return cppAnalyzer_; }
    const CppTypeAnalyzer& getCppAnalyzer() const { return cppAnalyzer_; }

    // ========================================================================
    // Mutation API (used by SemanticAnalyzer)
    // ========================================================================

    /**
     * Register type information for an expression node.
     * Called during semantic analysis for each expression.
     */
    void setExpressionType(Parser::Expression* expr, const ResolvedType& type);

    /**
     * Register type information for a variable by name.
     * Called when variables are declared (Instantiate statements).
     */
    void setVariableType(const std::string& varName, const ResolvedType& type);

    /**
     * Clear all type information.
     * Called between compilation units or when starting a new scope.
     */
    void clear();

    // ========================================================================
    // Query API (used by CodeGenerator)
    // ========================================================================

    /**
     * Get resolved type for an expression.
     * Returns nullptr if expression type is not registered.
     */
    const ResolvedType* getExpressionType(Parser::Expression* expr) const;

    /**
     * Get resolved type for a variable by name.
     * Returns nullptr if variable type is not registered.
     */
    const ResolvedType* getVariableType(const std::string& varName) const;

    /**
     * Check if an expression results in a smart pointer (Owned<T>).
     * Convenience method that checks if expression type isSmartPointer.
     */
    bool expressionIsSmartPtr(Parser::Expression* expr) const;

    /**
     * Check if a variable is a smart pointer (Owned<T>).
     * Convenience method that checks if variable type isSmartPointer.
     */
    bool variableIsSmartPtr(const std::string& varName) const;

    /**
     * Get the ownership type for a variable.
     * Returns OwnershipType::None if not found.
     */
    Parser::OwnershipType getVariableOwnership(const std::string& varName) const;

    /**
     * Get the XXML type name for a variable.
     * Returns empty string if not found.
     */
    std::string getVariableTypeName(const std::string& varName) const;

    /**
     * Get the C++ type name for a variable.
     * Returns empty string if not found.
     */
    std::string getVariableCppType(const std::string& varName) const;

private:
    // C++ type analyzer for semantic understanding
    CppTypeAnalyzer cppAnalyzer_;

    // Expression -> ResolvedType mapping
    std::unordered_map<Parser::Expression*, ResolvedType> expressionTypes_;

    // Variable name -> ResolvedType mapping
    std::unordered_map<std::string, ResolvedType> variableTypes_;
};

} // namespace Core
} // namespace XXML
