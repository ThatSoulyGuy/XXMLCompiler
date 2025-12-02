#include "Backends/TypeNormalizer.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace XXML {
namespace Backends {

std::string TypeNormalizer::normalize(const std::string& typeName) {
    std::string result = typeName;

    // Remove whitespace
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());

    return result;
}

std::string TypeNormalizer::getBaseType(const std::string& typeName) {
    std::string normalized = normalize(typeName);
    size_t pos = normalized.find('<');
    if (pos != std::string::npos) {
        return normalized.substr(0, pos);
    }
    return normalized;
}

bool TypeNormalizer::isTemplate(const std::string& typeName) {
    std::string normalized = normalize(typeName);
    return normalized.find('<') != std::string::npos &&
           normalized.find('>') != std::string::npos;
}

std::vector<std::string> TypeNormalizer::getTemplateArgs(const std::string& typeName) {
    std::vector<std::string> args;
    std::string normalized = normalize(typeName);

    size_t start = normalized.find('<');
    size_t end = normalized.rfind('>');

    if (start == std::string::npos || end == std::string::npos || start >= end) {
        return args;
    }

    std::string argsStr = normalized.substr(start + 1, end - start - 1);

    // Split by comma, handling nested templates
    int depth = 0;
    size_t lastPos = 0;

    for (size_t i = 0; i < argsStr.length(); ++i) {
        if (argsStr[i] == '<') depth++;
        else if (argsStr[i] == '>') depth--;
        else if (argsStr[i] == ',' && depth == 0) {
            args.push_back(argsStr.substr(lastPos, i - lastPos));
            lastPos = i + 1;
        }
    }

    if (lastPos < argsStr.length()) {
        args.push_back(argsStr.substr(lastPos));
    }

    // Trim each argument
    for (auto& arg : args) {
        arg.erase(std::remove_if(arg.begin(), arg.end(), ::isspace), arg.end());
    }

    return args;
}

std::string TypeNormalizer::makeTemplate(const std::string& base, const std::vector<std::string>& args) {
    if (args.empty()) {
        return base;
    }

    std::ostringstream result;
    result << base << "<";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result << ",";
        result << args[i];
    }
    result << ">";

    return result.str();
}

// ========== Ownership Marker Utilities ==========

bool TypeNormalizer::hasOwnershipMarker(std::string_view typeName) {
    if (typeName.empty()) return false;
    char last = typeName.back();
    return last == '^' || last == '&' || last == '%';
}

char TypeNormalizer::getOwnershipMarker(std::string_view typeName) {
    if (typeName.empty()) return '\0';
    char last = typeName.back();
    if (last == '^' || last == '&' || last == '%') {
        return last;
    }
    return '\0';
}

std::string TypeNormalizer::stripOwnershipMarker(std::string_view typeName) {
    if (hasOwnershipMarker(typeName)) {
        return std::string(typeName.substr(0, typeName.size() - 1));
    }
    return std::string(typeName);
}

std::string TypeNormalizer::addOwnershipMarker(std::string_view typeName, char marker) {
    if (marker == '\0') {
        return std::string(typeName);
    }
    return std::string(typeName) + marker;
}

// ========== Type Qualifier Utilities ==========

bool TypeNormalizer::hasQualifier(std::string_view typeName) {
    return typeName.find("::") != std::string_view::npos;
}

std::string TypeNormalizer::stripQualifiers(std::string_view typeName) {
    auto pos = typeName.rfind("::");
    if (pos != std::string_view::npos) {
        return std::string(typeName.substr(pos + 2));
    }
    return std::string(typeName);
}

std::string TypeNormalizer::getQualifier(std::string_view typeName) {
    auto pos = typeName.rfind("::");
    if (pos != std::string_view::npos) {
        return std::string(typeName.substr(0, pos));
    }
    return "";
}

// ========== Name Mangling for LLVM ==========

std::string TypeNormalizer::mangleForLLVM(std::string_view name) {
    std::string result(name);

    // Replace special characters with underscores
    for (char& c : result) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ' || c == '"') {
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

std::string TypeNormalizer::demangleFromLLVM(std::string_view mangledName) {
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

// ========== NativeType Format Utilities ==========

bool TypeNormalizer::isNativeType(std::string_view typeName) {
    // Strip ownership marker first
    std::string_view cleaned = typeName;
    if (!cleaned.empty() && (cleaned.back() == '^' || cleaned.back() == '&' || cleaned.back() == '%')) {
        cleaned = cleaned.substr(0, cleaned.size() - 1);
    }

    return cleaned.find("NativeType") == 0;
}

bool TypeNormalizer::isMangledNativeType(std::string_view typeName) {
    // Strip ownership marker first
    std::string_view cleaned = typeName;
    if (!cleaned.empty() && (cleaned.back() == '^' || cleaned.back() == '&' || cleaned.back() == '%')) {
        cleaned = cleaned.substr(0, cleaned.size() - 1);
    }

    return cleaned.find("NativeType_") == 0;
}

std::string TypeNormalizer::extractNativeTypeName(std::string_view fullType) {
    // Strip ownership marker first
    std::string_view type = fullType;
    if (!type.empty() && (type.back() == '^' || type.back() == '&' || type.back() == '%')) {
        type = type.substr(0, type.size() - 1);
    }

    // Handle NativeType_suffix (mangled form)
    if (type.find("NativeType_") == 0) {
        return std::string(type.substr(11));  // len("NativeType_") = 11
    }

    // Handle NativeType<"..."> (quoted form)
    auto startQuote = type.find('"');
    auto endQuote = type.rfind('"');
    if (startQuote != std::string_view::npos && endQuote != std::string_view::npos && endQuote > startQuote) {
        return std::string(type.substr(startQuote + 1, endQuote - startQuote - 1));
    }

    // Handle NativeType<...> (unquoted form)
    auto angleStart = type.find('<');
    auto angleEnd = type.rfind('>');
    if (angleStart != std::string_view::npos && angleEnd != std::string_view::npos && angleEnd > angleStart) {
        return std::string(type.substr(angleStart + 1, angleEnd - angleStart - 1));
    }

    // Return as-is if no pattern matched
    return std::string(type);
}

} // namespace Backends
} // namespace XXML
