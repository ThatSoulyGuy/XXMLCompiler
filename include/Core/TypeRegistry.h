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

private:
    // Internal storage
    std::unordered_map<std::string, TypeInfo> types_;
    mutable std::mutex mutex_;  // Thread-safety

    // Helper methods
    std::string extractBaseType(std::string_view fullType) const;
    bool isGenericTemplateType(std::string_view typeName) const;

    // Built-in type registration helpers
    void registerCoreTypes();
    void registerSystemTypes();
    void registerCollectionTypes();
    void registerMathTypes();
};

} // namespace XXML::Core
