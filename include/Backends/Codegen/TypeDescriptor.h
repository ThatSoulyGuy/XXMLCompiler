#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace XXML::Backends::Codegen {

/**
 * @brief Runtime type descriptor for type-erased operations
 *
 * This structure contains metadata about a type used as a template argument,
 * allowing shared base implementations to operate on any type.
 *
 * For XXML, most types are reference types (8-byte pointers), so the
 * descriptor primarily helps with:
 * - Determining if type-specific operations are needed
 * - Finding equals/hash functions for containers
 */
struct TypeDescriptor {
    std::string typeName;        // Qualified type name
    size_t size = 8;             // Size in bytes (default: pointer size)
    size_t alignment = 8;        // Alignment in bytes
    bool isValueType = false;    // True for Structure types
    bool isTrivial = true;       // True if memcpy-safe

    // Function names for type-specific operations (empty if not available)
    std::string equalsFn;        // e.g., "Integer_equals"
    std::string hashFn;          // e.g., "Integer_hashCode"
    std::string toStringFn;      // e.g., "Integer_toString"
};

/**
 * @brief Manages type descriptors for template deduplication
 *
 * Generates and caches TypeDescriptor values for each type used
 * as a template argument.
 */
class TypeDescriptorManager {
public:
    TypeDescriptorManager() = default;

    /**
     * Get or create a TypeDescriptor for the given type.
     * @param typeName The qualified type name (e.g., "Integer", "String")
     * @param size Size of the type in bytes (0 for default pointer size)
     * @param isValueType Whether this is a value type (Structure)
     * @return Reference to the TypeDescriptor for this type
     */
    const TypeDescriptor& getOrCreateDescriptor(
        const std::string& typeName,
        size_t size = 8,
        bool isValueType = false);

    /**
     * Check if a type has a descriptor.
     */
    bool hasDescriptor(const std::string& typeName) const;

    /**
     * Get descriptor by name (returns nullptr if not found).
     */
    const TypeDescriptor* getDescriptor(const std::string& typeName) const;

    /**
     * Register a type-specific function for a type.
     * @param typeName The type name
     * @param functionKind "equals", "hash", or "toString"
     * @param functionName The mangled function name
     */
    void registerFunction(
        const std::string& typeName,
        const std::string& functionKind,
        const std::string& functionName);

private:
    std::unordered_map<std::string, TypeDescriptor> descriptors_;

    // Normalize type name for lookup
    std::string normalizeTypeName(const std::string& typeName) const;
};

} // namespace XXML::Backends::Codegen
