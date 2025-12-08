#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace XXML {
namespace Backends {

/**
 * Normalizes type names for consistent handling.
 * Provides utilities for ownership markers, type qualifiers, name mangling, and NativeType parsing.
 *
 * NOTE: Name mangling functions delegate to NameMangler for consistency.
 * New code should use NameMangler directly for mangling operations.
 */
class TypeNormalizer {
public:
    // ========== Original Template Methods ==========

    // Normalize a type name (remove whitespace, standardize format)
    static std::string normalize(const std::string& typeName);

    // Extract base type from template (e.g., "List<Integer>" -> "List")
    static std::string getBaseType(const std::string& typeName);

    // Check if type is a template instantiation
    static bool isTemplate(const std::string& typeName);

    // Extract template arguments (e.g., "List<Integer>" -> ["Integer"])
    static std::vector<std::string> getTemplateArgs(const std::string& typeName);

    // Reconstruct template type from base and args
    static std::string makeTemplate(const std::string& base, const std::vector<std::string>& args);

    // ========== Ownership Marker Utilities ==========

    /// Check if type has an ownership marker (^, &, or %)
    static bool hasOwnershipMarker(std::string_view typeName);

    /// Get the ownership marker character ('^', '&', '%', or '\0' if none)
    static char getOwnershipMarker(std::string_view typeName);

    /// Strip ownership marker from type name (e.g., "Integer^" -> "Integer")
    static std::string stripOwnershipMarker(std::string_view typeName);

    /// Add ownership marker to type name (e.g., "Integer" + '^' -> "Integer^")
    static std::string addOwnershipMarker(std::string_view typeName, char marker);

    // ========== Type Qualifier Utilities ==========

    /// Check if type has namespace qualifiers (e.g., "Language::Core::Integer")
    static bool hasQualifier(std::string_view typeName);

    /// Strip all namespace qualifiers (e.g., "Language::Core::Integer" -> "Integer")
    static std::string stripQualifiers(std::string_view typeName);

    /// Get the qualifier portion (e.g., "Language::Core::Integer" -> "Language::Core")
    static std::string getQualifier(std::string_view typeName);

    // ========== Name Mangling for LLVM ==========
    // NOTE: These delegate to NameMangler. Use NameMangler directly for new code.

    /// Mangle a name for LLVM (replaces ::, <, >, comma, space with underscores)
    /// @deprecated Use NameMangler::mangleForLLVM instead
    static std::string mangleForLLVM(std::string_view name);

    /// Attempt to demangle an LLVM-mangled name (basic reversal)
    /// @deprecated Use NameMangler::demangleFromLLVM instead
    static std::string demangleFromLLVM(std::string_view mangledName);

    // ========== NativeType Format Utilities ==========

    /// Check if type is a NativeType (any format: NativeType<...>, NativeType_...)
    static bool isNativeType(std::string_view typeName);

    /// Check if type is a mangled NativeType (NativeType_int64)
    static bool isMangledNativeType(std::string_view typeName);

    /// Extract the inner type name from NativeType<"int64"> or NativeType<int64> or NativeType_int64
    static std::string extractNativeTypeName(std::string_view fullType);
};

} // namespace Backends
} // namespace XXML
