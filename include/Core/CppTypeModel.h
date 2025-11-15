#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace XXML {
namespace Core {

/**
 * C++ Type Categories
 *
 * This enum captures the fundamental categories of C++ types,
 * which determines how they behave in expressions and how member
 * access should be generated.
 */
enum class CppTypeCategory {
    // Value types - stored directly, use . for member access
    Value,           // int, bool, char, structs by value

    // Reference types - aliases to other objects, use . for member access
    LValueRef,       // T&
    RValueRef,       // T&&
    ConstRef,        // const T&

    // Pointer types - addresses, use -> for member access
    RawPointer,      // T*
    ConstPointer,    // const T*

    // Smart pointer types - RAII wrappers, use -> for member access
    UniquePtr,       // std::unique_ptr<T>
    SharedPtr,       // std::shared_ptr<T>
    OwnedPtr,        // Language::Runtime::Owned<T> (XXML's smart pointer)

    // Special types
    Void,            // void (no value)
    Unknown          // Type not yet resolved
};

/**
 * C++ Type Information
 *
 * This class captures complete semantic information about a C++ type,
 * including its category, template arguments, const-ness, and behavioral
 * properties. This allows the code generator to make correct decisions
 * about syntax without hardcoded rules.
 */
class CppTypeInfo {
public:
    CppTypeInfo()
        : category_(CppTypeCategory::Unknown)
        , isConst_(false)
        , isVolatile_(false) {}

    explicit CppTypeInfo(const std::string& typeName);

    // ========================================================================
    // Type Identity
    // ========================================================================

    std::string getFullName() const { return fullTypeName_; }
    std::string getBaseName() const { return baseTypeName_; }
    CppTypeCategory getCategory() const { return category_; }

    // ========================================================================
    // Type Properties
    // ========================================================================

    bool isValue() const { return category_ == CppTypeCategory::Value; }
    bool isReference() const {
        return category_ == CppTypeCategory::LValueRef ||
               category_ == CppTypeCategory::RValueRef ||
               category_ == CppTypeCategory::ConstRef;
    }
    bool isPointer() const {
        return category_ == CppTypeCategory::RawPointer ||
               category_ == CppTypeCategory::ConstPointer;
    }
    bool isSmartPointer() const {
        return category_ == CppTypeCategory::UniquePtr ||
               category_ == CppTypeCategory::SharedPtr ||
               category_ == CppTypeCategory::OwnedPtr;
    }

    bool isConst() const { return isConst_; }
    bool isVolatile() const { return isVolatile_; }

    // ========================================================================
    // Template Information
    // ========================================================================

    bool isTemplate() const { return !templateArgs_.empty(); }
    const std::vector<std::string>& getTemplateArgs() const { return templateArgs_; }

    // For smart pointers, get the wrapped type
    std::string getWrappedType() const {
        if (isSmartPointer() && !templateArgs_.empty()) {
            return templateArgs_[0];
        }
        return "";
    }

    // ========================================================================
    // Code Generation Decisions
    // ========================================================================

    /**
     * Determines the correct member access operator for this type.
     *
     * Returns:
     *   "."  for value types and references
     *   "->" for pointers and smart pointers
     */
    std::string getMemberAccessOperator() const {
        if (isPointer() || isSmartPointer()) {
            return "->";
        }
        return ".";
    }

    /**
     * Determines if this type needs to be dereferenced to access the underlying value.
     *
     * For example:
     *   Owned<Integer> needs .get() to access Integer
     *   std::unique_ptr<T> needs .get() to access T*
     *   T& does not need dereferencing
     */
    bool needsExplicitUnwrap() const {
        return category_ == CppTypeCategory::OwnedPtr;
    }

    /**
     * Get the expression to unwrap/dereference this type.
     *
     * Returns:
     *   ".get()" for Owned<T>
     *   "*"      for raw pointers when dereferencing to value
     *   ""       for types that don't need unwrapping
     */
    std::string getUnwrapExpression() const {
        if (category_ == CppTypeCategory::OwnedPtr) {
            return ".get()";
        } else if (category_ == CppTypeCategory::UniquePtr ||
                   category_ == CppTypeCategory::SharedPtr) {
            return ".get()";  // Gets raw pointer
        }
        return "";
    }

    /**
     * Checks if this type can implicitly convert to another type.
     *
     * Examples:
     *   Owned<T> -> T&        (yes, via operator T&())
     *   T        -> const T&  (yes, reference binding)
     *   int      -> double    (yes, numeric conversion)
     */
    bool canImplicitlyConvertTo(const CppTypeInfo& target) const;

    /**
     * Checks if this type is callable (function, lambda, functor).
     */
    bool isCallable() const { return isCallable_; }

    /**
     * For callable types, get return type.
     */
    std::string getReturnType() const { return returnType_; }

    // ========================================================================
    // Builders (Fluent API)
    // ========================================================================

    CppTypeInfo& setCategory(CppTypeCategory cat) { category_ = cat; return *this; }
    CppTypeInfo& setConst(bool isConst) { isConst_ = isConst; return *this; }
    CppTypeInfo& setTemplateArgs(const std::vector<std::string>& args) {
        templateArgs_ = args;
        return *this;
    }
    CppTypeInfo& setFullName(const std::string& name) { fullTypeName_ = name; return *this; }
    CppTypeInfo& setBaseName(const std::string& name) { baseTypeName_ = name; return *this; }

private:
    std::string fullTypeName_;     // e.g., "Language::Runtime::Owned<Language::Core::Integer>"
    std::string baseTypeName_;     // e.g., "Owned"
    CppTypeCategory category_;
    bool isConst_;
    bool isVolatile_;

    // Template information
    std::vector<std::string> templateArgs_;

    // Callable information
    bool isCallable_;
    std::string returnType_;
    std::vector<std::string> parameterTypes_;
};

/**
 * C++ Type Analyzer
 *
 * This class provides semantic analysis of C++ types. It parses C++ type
 * strings and constructs CppTypeInfo with full semantic understanding.
 *
 * This is the "brain" that gives the code generator deep understanding
 * of C++ types.
 */
class CppTypeAnalyzer {
public:
    CppTypeAnalyzer();

    /**
     * Analyze a C++ type string and return semantic information.
     *
     * Examples:
     *   "Language::Runtime::Owned<Language::Core::Integer>"
     *     -> OwnedPtr, template args = ["Language::Core::Integer"]
     *
     *   "const Language::Core::Integer&"
     *     -> ConstRef, base type = "Language::Core::Integer"
     *
     *   "int*"
     *     -> RawPointer, base type = "int"
     */
    CppTypeInfo analyze(const std::string& cppTypeName);

    /**
     * Register a custom type with known properties.
     *
     * This allows the system to know about XXML runtime types:
     *   - Language::Core::Integer is a value type
     *   - Language::Core::String is a value type
     *   - Language::Runtime::Owned<T> is a smart pointer
     */
    void registerType(const std::string& typeName, CppTypeCategory category);

    /**
     * Register that a type has methods.
     *
     * This allows method resolution during code generation.
     */
    void registerMethod(const std::string& typeName,
                       const std::string& methodName,
                       const std::string& returnType);

    /**
     * Check if a type has a specific method.
     */
    bool hasMethod(const std::string& typeName, const std::string& methodName) const;

    /**
     * Get the return type of a method.
     */
    std::string getMethodReturnType(const std::string& typeName,
                                   const std::string& methodName) const;

private:
    // Parse template arguments from type string
    std::vector<std::string> parseTemplateArgs(const std::string& typeStr, size_t& pos);

    // Extract base type name (without qualifiers or template args)
    std::string extractBaseName(const std::string& typeStr);

    // Check if a type name is a known smart pointer
    bool isSmartPointerType(const std::string& baseName) const;

    // Check if a type is a known value type
    bool isValueType(const std::string& typeName) const;

    // Registered type information
    std::unordered_map<std::string, CppTypeCategory> registeredTypes_;

    // Method registry: typeName -> (methodName -> returnType)
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> methodRegistry_;
};

} // namespace Core
} // namespace XXML
