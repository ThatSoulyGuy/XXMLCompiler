#include "Backends/TypeNormalizer.h"
#include <algorithm>
#include <sstream>

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

} // namespace Backends
} // namespace XXML
