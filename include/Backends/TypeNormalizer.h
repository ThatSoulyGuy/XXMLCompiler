#pragma once

#include <string>
#include <vector>

namespace XXML {
namespace Backends {

/**
 * Normalizes type names for consistent handling
 */
class TypeNormalizer {
public:
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
};

} // namespace Backends
} // namespace XXML
