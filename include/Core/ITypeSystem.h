#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include "Concepts.h"

namespace XXML::Core {

// Forward declarations
class TypeInfo;
class CompilationContext;

/// Ownership semantics for types
enum class OwnershipSemantics {
    Value,          // Pass by value (primitives like int, bool)
    Unique,         // std::unique_ptr<T> - exclusive ownership
    Shared,         // std::shared_ptr<T> - shared ownership
    Reference,      // T& - reference (no ownership transfer)
    ConstReference  // const T& - const reference
};

/// Type category classification
enum class TypeCategory {
    Primitive,      // Built-in C++ types (int, bool, double)
    Class,          // User-defined classes
    Struct,         // User-defined structs
    Interface,      // Abstract interfaces
    Template,       // Template types
    Native,         // Native C++ types wrapped in NativeType<>
    Array,          // Array types
    Function        // Function types
};

/// Information about a single type
class TypeInfo {
public:
    std::string xxmlName;           // Name in XXML (e.g., "Integer")
    std::string cppType;            // C++ type (e.g., "int64_t")
    std::string llvmType;           // LLVM type (e.g., "i64") - optional
    TypeCategory category;
    OwnershipSemantics ownership;
    bool isBuiltin;                 // Is it a built-in stdlib type?
    bool isTemplate;                // Is it a template type?

    // Template information
    std::vector<std::string> templateParams;

    // Method information (can be extended)
    struct MethodInfo {
        std::string name;
        std::string returnType;
        std::vector<std::pair<std::string, std::string>> parameters; // (name, type)
        bool isStatic;
        bool isConst;
    };
    std::vector<MethodInfo> methods;

    // Constructor information
    struct ConstructorInfo {
        std::vector<std::string> parameterTypes;
        std::string cppInitializer;  // Custom C++ initialization code
    };
    std::vector<ConstructorInfo> constructors;

    // Operator overload information
    struct OperatorOverload {
        std::string op;              // Operator symbol (e.g., "+", "==")
        std::string rightType;       // Type of right operand
        std::string returnType;      // Return type
        std::string cppImplementation; // C++ code template
    };
    std::vector<OperatorOverload> operatorOverloads;

    TypeInfo() = default;
    TypeInfo(std::string_view xxml, std::string_view cpp,
             TypeCategory cat, OwnershipSemantics own, bool builtin = false)
        : xxmlName(xxml), cppType(cpp), category(cat),
          ownership(own), isBuiltin(builtin), isTemplate(false) {}

    // Helper methods
    bool requiresSmartPointer() const {
        return ownership == OwnershipSemantics::Unique ||
               ownership == OwnershipSemantics::Shared;
    }

    bool isValueType() const {
        return ownership == OwnershipSemantics::Value;
    }

    bool isReferenceType() const {
        return ownership == OwnershipSemantics::Reference ||
               ownership == OwnershipSemantics::ConstReference;
    }

    bool isPrimitive() const {
        return category == TypeCategory::Primitive;
    }

    std::string toDebugString() const;
};

/// Abstract interface for type system implementations
class ITypeSystem {
public:
    virtual ~ITypeSystem() = default;

    // Type registration
    virtual void registerType(const TypeInfo& info) = 0;
    virtual void registerBuiltinTypes() = 0;

    // Type queries
    virtual bool isRegistered(std::string_view typeName) const = 0;
    virtual const TypeInfo* getTypeInfo(std::string_view typeName) const = 0;
    virtual TypeInfo* getTypeInfo(std::string_view typeName) = 0;

    // Type compatibility checks
    virtual bool isCompatible(std::string_view from, std::string_view to) const = 0;
    virtual bool canAssign(std::string_view from, std::string_view to) const = 0;
    virtual bool requiresConversion(std::string_view from, std::string_view to) const = 0;

    // Type properties
    virtual bool isPrimitive(std::string_view typeName) const = 0;
    virtual bool isValueType(std::string_view typeName) const = 0;
    virtual bool requiresSmartPointer(std::string_view typeName) const = 0;
    virtual OwnershipSemantics getOwnershipSemantics(std::string_view typeName) const = 0;

    // Type conversion
    virtual std::string getCppType(std::string_view xxmlType) const = 0;
    virtual std::string getLLVMType(std::string_view xxmlType) const = 0;

    // Template support
    virtual bool isTemplate(std::string_view typeName) const = 0;
    virtual std::string instantiateTemplate(std::string_view templateName,
                                           const std::vector<std::string>& args) const = 0;

    // Operator support
    virtual bool hasOperatorOverload(std::string_view typeName,
                                    std::string_view op,
                                    std::string_view rightType) const = 0;
    virtual std::optional<TypeInfo::OperatorOverload>
        getOperatorOverload(std::string_view typeName,
                          std::string_view op,
                          std::string_view rightType) const = 0;

    // Utility
    virtual std::vector<std::string> getAllRegisteredTypes() const = 0;
    virtual void clear() = 0;
};

} // namespace XXML::Core
