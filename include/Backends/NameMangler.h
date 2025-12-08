#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace XXML {
namespace Parser {
    struct TemplateArgument;  // Forward declaration
}
}

namespace XXML {
namespace Backends {

/**
 * Universal name mangler for LLVM IR generation.
 *
 * This class provides consistent name mangling across the entire compiler.
 * All name transformations for LLVM compatibility should go through this class.
 *
 * Mangling Conventions:
 * - Function names: ClassName_methodName (underscores for separators)
 * - IR struct names: Namespace.ClassName (dots for namespaces)
 * - Template instantiations: Template_Arg1_Arg2 (underscores for args)
 * - Template methods: method_LT_Arg_GT_ (encoded angle brackets)
 *
 * Character Mappings:
 * - :: (namespace) -> _ or . depending on context
 * - < > (template) -> _ or _LT_ _GT_ for methods
 * - , (separator) -> _ or _C_ for methods
 * - @ (alt template syntax) -> _
 * - spaces -> removed
 */
class NameMangler {
public:
    // ========================================================================
    // Basic Mangling Operations
    // ========================================================================

    /**
     * Mangle a method name: ClassName_methodName
     * @param className The class name (may include namespace)
     * @param methodName The method name
     * @return Mangled function name suitable for LLVM
     */
    static std::string mangleMethod(const std::string& className, const std::string& methodName);

    /**
     * Mangle a simple template instantiation: List_Integer
     * @param templateName Base template name
     * @param typeArgs Vector of type argument names
     * @return Mangled template name
     */
    static std::string mangleTemplate(const std::string& templateName, const std::vector<std::string>& typeArgs);

    /**
     * Mangle a class name for LLVM struct type: %class.ClassName
     * @param className The class name
     * @return Prefixed and sanitized class name
     */
    static std::string mangleStructType(const std::string& className);

    // ========================================================================
    // LLVM-Specific Mangling
    // ========================================================================

    /**
     * Mangle a name for LLVM identifiers.
     * Replaces special characters (::, <, >, ,, space, ") with underscores.
     * Removes consecutive underscores and trailing underscores.
     *
     * @param name The name to mangle
     * @return LLVM-compatible identifier
     *
     * Examples:
     *   "Language::Core::Integer" -> "Language_Core_Integer"
     *   "HashMap<String, Integer>" -> "HashMap_String_Integer"
     */
    static std::string mangleForLLVM(std::string_view name);

    /**
     * Sanitize a name for LLVM IR struct definitions.
     * Replaces :: with . (period) which is valid in LLVM struct names.
     *
     * @param name The name to sanitize
     * @return IR-compatible struct name
     *
     * Example: "Language::Collections::List" -> "Language.Collections.List"
     */
    static std::string sanitizeForIRStruct(std::string_view name);

    // ========================================================================
    // Template-Specific Mangling
    // ========================================================================

    /**
     * Normalize a template name for class registry lookup.
     * Converts template syntax to underscore format while preserving :: separators.
     *
     * @param name Template name with angle brackets or @ syntax
     * @return Normalized name for lookup
     *
     * Examples:
     *   "HashMap<Integer, String>" -> "HashMap_Integer_String"
     *   "HashMap@Integer, String" -> "HashMap_Integer_String"
     *   "Language::Collections::List<Integer>" -> "Language::Collections::List_Integer"
     */
    static std::string normalizeTemplateForLookup(std::string_view name);

    /**
     * Mangle a template class instantiation with full type information.
     * Uses TypeNormalizer::mangleForLLVM for each type argument.
     *
     * @param templateName Base template name (may include namespace)
     * @param args Vector of template arguments
     * @param evaluatedValues Evaluated values for non-type template parameters
     * @return Fully mangled template class name
     *
     * Example: "HashMap" + [Integer, String] -> "HashMap_Integer_String"
     */
    static std::string mangleTemplateClass(
        const std::string& templateName,
        const std::vector<Parser::TemplateArgument>& args,
        const std::vector<int64_t>& evaluatedValues = {});

    /**
     * Mangle a template method with encoded delimiters.
     * Uses _LT_ for <, _GT_ for >, _C_ for comma separators.
     *
     * @param methodName Base method name
     * @param args Vector of template arguments
     * @return Mangled method name
     *
     * Example: "convert" + [Integer] -> "convert_LT_Integer_GT_"
     */
    static std::string mangleTemplateMethod(
        const std::string& methodName,
        const std::vector<Parser::TemplateArgument>& args);

    // ========================================================================
    // Function Name Mangling (with special cases)
    // ========================================================================

    /**
     * Mangle a function name with special namespace handling.
     *
     * Special cases:
     * - Syscall namespace: methods become xxml_methodName
     * - Other namespaces: ClassName_methodName with LLVM mangling
     *
     * @param className Class or namespace name
     * @param methodName Method name
     * @return Mangled function name
     */
    static std::string mangleFunctionName(std::string_view className, std::string_view methodName);

    // ========================================================================
    // Demangling Operations
    // ========================================================================

    /**
     * Demangle a method name back to class and method components.
     * Splits on the first underscore.
     *
     * @param mangledName The mangled method name
     * @return Pair of (className, methodName), or ("", mangledName) if not mangled
     */
    static std::pair<std::string, std::string> demangleMethod(const std::string& mangledName);

    /**
     * Attempt to demangle an LLVM-mangled name.
     * This is best-effort and may not fully recover the original name.
     *
     * @param mangledName The LLVM-mangled name
     * @return Best-effort demangled name
     */
    static std::string demangleFromLLVM(std::string_view mangledName);

    // ========================================================================
    // Utility Methods
    // ========================================================================

    /**
     * Check if a name appears to be a mangled method (contains underscore).
     */
    static bool isMangledMethod(const std::string& name);

    /**
     * Basic sanitization: replaces :: with _, removes non-alphanumeric except _ and .
     *
     * @param name Name to sanitize
     * @return Sanitized identifier
     */
    static std::string sanitize(const std::string& name);

    /**
     * Sanitize only namespace separators in a type argument.
     * Replaces :: with _ but preserves other characters.
     *
     * @param typeArg Type argument to sanitize
     * @return Sanitized type argument
     */
    static std::string sanitizeTypeArg(std::string_view typeArg);
};

} // namespace Backends
} // namespace XXML
