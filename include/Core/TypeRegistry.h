#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <mutex>
#include "ITypeSystem.h"
#include "Concepts.h"

namespace XXML::Core {

/// Information about a native FFI type (int64, float, ptr, etc.)
struct NativeTypeInfo {
    std::string canonicalName;  // "int64"
    std::string llvmType;       // "i64"
    std::string cppType;        // "int64_t"
    int bitWidth;               // 64
    bool isSigned;
    bool isFloatingPoint;
    bool isPointer;
};

/// Concrete implementation of ITypeSystem using a registry pattern
/// Thread-safe type registry with runtime registration
class TypeRegistry : public ITypeSystem {
public:
    TypeRegistry();
    ~TypeRegistry() override = default;

    // Type registration
    void registerType(const TypeInfo& info) override;
    void registerBuiltinTypes() override;

    // Fluent API for registration
    TypeRegistry& withType(const TypeInfo& info) {
        registerType(info);
        return *this;
    }

    template<StringLike Name, StringLike CppType>
    TypeRegistry& withPrimitive(const Name& name, const CppType& cppType) {
        registerType(TypeInfo{
            name, cppType,
            TypeCategory::Primitive,
            OwnershipSemantics::Value,
            true
        });
        return *this;
    }

    template<StringLike Name, StringLike CppType>
    TypeRegistry& withClass(const Name& name, const CppType& cppType,
                           OwnershipSemantics ownership = OwnershipSemantics::Unique) {
        registerType(TypeInfo{
            name, cppType,
            TypeCategory::Class,
            ownership,
            false
        });
        return *this;
    }

    // Type queries
    bool isRegistered(std::string_view typeName) const override;
    const TypeInfo* getTypeInfo(std::string_view typeName) const override;
    TypeInfo* getTypeInfo(std::string_view typeName) override;

    // Type compatibility checks
    bool isCompatible(std::string_view from, std::string_view to) const override;
    bool canAssign(std::string_view from, std::string_view to) const override;
    bool requiresConversion(std::string_view from, std::string_view to) const override;

    // Type properties
    bool isPrimitive(std::string_view typeName) const override;
    bool isValueType(std::string_view typeName) const override;
    bool requiresSmartPointer(std::string_view typeName) const override;
    OwnershipSemantics getOwnershipSemantics(std::string_view typeName) const override;

    // Type conversion
    std::string getCppType(std::string_view xxmlType) const override;
    std::string getLLVMType(std::string_view xxmlType) const override;

    // Template support
    bool isTemplate(std::string_view typeName) const override;
    std::string instantiateTemplate(std::string_view templateName,
                                   const std::vector<std::string>& args) const override;

    // Operator support
    bool hasOperatorOverload(std::string_view typeName,
                            std::string_view op,
                            std::string_view rightType) const override;
    std::optional<TypeInfo::OperatorOverload>
        getOperatorOverload(std::string_view typeName,
                          std::string_view op,
                          std::string_view rightType) const override;

    // Utility
    std::vector<std::string> getAllRegisteredTypes() const override;
    void clear() override;

    // Statistics
    size_t size() const;
    size_t builtinCount() const;
    size_t userDefinedCount() const;

    // ========== NativeType Support ==========

    /// Look up native type info by name (e.g., "int32", "ptr", "float")
    const NativeTypeInfo* lookupNativeType(std::string_view name) const;

    /// Get LLVM IR type string for a native type (e.g., "int64" -> "i64")
    std::string getNativeTypeLLVM(std::string_view name) const;

    /// Get C++ type string for a native type (e.g., "int64" -> "int64_t")
    std::string getNativeTypeCpp(std::string_view name) const;

    /// Check if a native type is a signed integer
    bool isNativeTypeSigned(std::string_view name) const;

    /// Check if a native type is floating point
    bool isNativeTypeFloat(std::string_view name) const;

    /// Check if a native type is a pointer type
    bool isNativeTypePointer(std::string_view name) const;

    /// Get bit width of a native type (0 for pointer/void)
    int getNativeTypeBitWidth(std::string_view name) const;

    // ========== Primitive Type Utilities (Static) ==========

    /// Check if type is a primitive XXML type (Integer, Float, Bool, etc.)
    static bool isPrimitiveXXML(std::string_view typeName);

    /// Check if type is a numeric primitive (Integer, Float, Double)
    static bool isNumericPrimitive(std::string_view typeName);

    /// Get LLVM type for a primitive XXML type
    static std::string getPrimitiveLLVMType(std::string_view typeName);

    /// Get C++ type for a primitive XXML type
    static std::string getPrimitiveCppType(std::string_view typeName);

private:
    // Internal storage
    std::unordered_map<std::string, TypeInfo> types_;
    mutable std::mutex mutex_;  // Thread-safety

    // NativeType storage
    std::unordered_map<std::string, NativeTypeInfo> nativeTypes_;
    std::unordered_map<std::string, std::string> nativeAliases_;  // alias -> canonical

    // Helper methods
    std::string extractBaseType(std::string_view fullType) const;
    bool isGenericTemplateType(std::string_view typeName) const;

    // Built-in type registration helpers
    void registerCoreTypes();
    void registerSystemTypes();
    void registerCollectionTypes();
    void registerMathTypes();
    void registerNativeTypes();  // Register all NativeType<...> mappings
};

} // namespace XXML::Core
