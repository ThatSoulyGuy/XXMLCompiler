#include "Backends/NameMangler.h"
#include <sstream>
#include <algorithm>

namespace XXML {
namespace Backends {

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

std::string NameMangler::mangleClassName(const std::string& className) {
    return "%class." + sanitize(className);
}

std::pair<std::string, std::string> NameMangler::demangleMethod(const std::string& mangledName) {
    size_t pos = mangledName.find('_');
    if (pos != std::string::npos) {
        return {mangledName.substr(0, pos), mangledName.substr(pos + 1)};
    }
    return {"", mangledName};
}

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

} // namespace Backends
} // namespace XXML
