#include "Backends/NameMangler.h"
#include "Parser/AST.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace XXML {
namespace Backends {

// ============================================================================
// Basic Mangling Operations
// ============================================================================

std::string NameMangler::mangleMethod(const std::string& className, const std::string& methodName) {
    return sanitize(className) + "_" + sanitize(methodName);
}

std::string NameMangler::mangleTemplate(const std::string& templateName, const std::vector<std::string>& typeArgs) {
    std::ostringstream mangled;
    mangled << sanitize(templateName);
    for (const auto& arg : typeArgs) {
        mangled << "_" << sanitize(arg);
    }
    return mangled.str();
}

std::string NameMangler::mangleStructType(const std::string& className) {
    return "%class." + sanitize(className);
}

// ============================================================================
// LLVM-Specific Mangling
// ============================================================================

std::string NameMangler::mangleForLLVM(std::string_view name) {
    std::string result(name);

    // Replace special characters with underscores
    for (char& c : result) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ' || c == '"' || c == '@') {
            c = '_';
        }
    }

    // Remove consecutive underscores
    auto newEnd = std::unique(result.begin(), result.end(),
        [](char a, char b) { return a == '_' && b == '_'; });
    result.erase(newEnd, result.end());

    // Remove trailing underscore if present
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return result;
}

std::string NameMangler::sanitizeForIRStruct(std::string_view name) {
    std::string result(name);
    size_t pos = 0;
    while ((pos = result.find("::", pos)) != std::string::npos) {
        result.replace(pos, 2, ".");
        pos += 1;
    }
    return result;
}

// ============================================================================
// Template-Specific Mangling
// ============================================================================

std::string NameMangler::normalizeTemplateForLookup(std::string_view name) {
    std::string result;
    result.reserve(name.size());

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c == '<' || c == '>' || c == '@' || c == ',') {
            // Template syntax characters become underscores
            // But skip if this would create consecutive underscores
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
        } else if (c == ' ') {
            // Skip spaces (e.g., "Integer, String" -> "Integer_String")
        } else if (c == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            // Preserve :: namespace separators
            result += "::";
            ++i;  // Skip the second colon
        } else {
            result += c;
        }
    }

    // Remove trailing underscore if present
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return result;
}

std::string NameMangler::mangleTemplateClass(
    const std::string& templateName,
    const std::vector<Parser::TemplateArgument>& args,
    const std::vector<int64_t>& evaluatedValues) {

    std::string mangledName = templateName;
    size_t valueIndex = 0;

    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Replace namespace separators with underscores
            std::string cleanType = mangleForLLVM(arg.typeArg);
            mangledName += "_" + cleanType;
        } else {
            // Use evaluated value for non-type parameters
            if (valueIndex < evaluatedValues.size()) {
                mangledName += "_" + std::to_string(evaluatedValues[valueIndex]);
                valueIndex++;
            }
        }
    }

    return mangledName;
}

std::string NameMangler::mangleTemplateMethod(
    const std::string& methodName,
    const std::vector<Parser::TemplateArgument>& args) {

    std::string mangledName = methodName + "_LT_";
    bool first = true;

    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            if (!first) mangledName += "_C_";  // Comma separator
            first = false;

            // Sanitize the type argument
            std::string cleanType = sanitizeTypeArg(arg.typeArg);
            mangledName += cleanType;
        }
    }

    mangledName += "_GT_";
    return mangledName;
}

// ============================================================================
// Function Name Mangling (with special cases)
// ============================================================================

std::string NameMangler::mangleFunctionName(std::string_view className, std::string_view methodName) {
    // Special handling for Syscall namespace - translate to xxml_ prefixed runtime functions
    if (className == "Syscall") {
        return "xxml_" + std::string(methodName);
    }

    std::string combined(className);
    combined += "_";
    combined += methodName;

    // Use mangleForLLVM for consistent mangling
    return mangleForLLVM(combined);
}

// ============================================================================
// Demangling Operations
// ============================================================================

std::pair<std::string, std::string> NameMangler::demangleMethod(const std::string& mangledName) {
    size_t pos = mangledName.find('_');
    if (pos != std::string::npos) {
        return {mangledName.substr(0, pos), mangledName.substr(pos + 1)};
    }
    return {"", mangledName};
}

std::string NameMangler::demangleFromLLVM(std::string_view mangledName) {
    // Basic demangling - this is a best-effort reversal
    // In practice, full demangling would require more context
    std::string result(mangledName);

    // Replace underscores with :: for common patterns
    // This is a simplified version - real demangling is complex
    size_t pos = 0;
    while ((pos = result.find('_', pos)) != std::string::npos) {
        // Check if this looks like a namespace separator
        // (between two identifiers, not at start/end or adjacent to numbers)
        if (pos > 0 && pos < result.length() - 1) {
            char before = result[pos - 1];
            char after = result[pos + 1];
            if (std::isalpha(before) && std::isalpha(after)) {
                // This could be a namespace separator, but we can't be sure
                // For safety, we'll just return as-is
            }
        }
        pos++;
    }

    return result;
}

// ============================================================================
// Utility Methods
// ============================================================================

bool NameMangler::isMangledMethod(const std::string& name) {
    return name.find('_') != std::string::npos;
}

std::string NameMangler::sanitize(const std::string& name) {
    std::string result = name;

    // First replace :: with single _ (for namespace separators)
    size_t pos = 0;
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "_");
    }

    // Then sanitize remaining characters
    std::string sanitized;
    sanitized.reserve(result.length());
    for (char c : result) {
        if (std::isalnum(c) || c == '_' || c == '.') {
            sanitized += c;
        } else if (c == ':') {
            sanitized += '_';  // Handle any remaining single colons
        }
    }

    return sanitized;
}

std::string NameMangler::sanitizeTypeArg(std::string_view typeArg) {
    std::string result(typeArg);

    // Replace :: with _
    size_t pos = 0;
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "_");
    }

    return result;
}

} // namespace Backends
} // namespace XXML
