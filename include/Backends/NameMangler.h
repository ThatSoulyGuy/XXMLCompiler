#pragma once

#include <string>
#include <vector>

namespace XXML {
namespace Backends {

/**
 * Handles name mangling for LLVM IR generation
 */
class NameMangler {
public:
    // Mangle a method name: ClassName_methodName
    static std::string mangleMethod(const std::string& className, const std::string& methodName);

    // Mangle a template instantiation: List_Integer, HashMap_String_Integer
    static std::string mangleTemplate(const std::string& templateName, const std::vector<std::string>& typeArgs);

    // Mangle a class name for struct type: %class.ClassName
    static std::string mangleClassName(const std::string& className);

    // Demangle a method name back to class and method
    static std::pair<std::string, std::string> demangleMethod(const std::string& mangledName);

    // Check if a name is a mangled method
    static bool isMangledMethod(const std::string& name);

    // Sanitize identifier (remove invalid characters)
    static std::string sanitize(const std::string& name);
};

} // namespace Backends
} // namespace XXML
