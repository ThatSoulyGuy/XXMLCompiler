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
    std::string result;
    result.reserve(name.length());

    for (char c : name) {
        if (std::isalnum(c) || c == '_' || c == '.') {
            result += c;
        } else if (c == ':') {
            result += '_';
        }
    }

    return result;
}

} // namespace Backends
} // namespace XXML
