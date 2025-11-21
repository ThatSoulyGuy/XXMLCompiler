#include "Backends/MethodResolver.h"
#include "Backends/NameMangler.h"
#include "Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Backends {

std::string MethodResolver::resolveMethod(const std::string& className, const std::string& methodName) const {
    return NameMangler::mangleMethod(className, methodName);
}

std::string MethodResolver::getReturnType(const std::string& className, const std::string& methodName) const {
    // Use semantic analyzer if available
    // For now, return default based on method name
    if (methodName == "Constructor") return className;
    if (methodName == "add" || methodName == "dispose") return "None";
    return "ptr";
}

std::vector<std::string> MethodResolver::getParameterTypes(const std::string& className,
                                                            const std::string& methodName) const {
    // This would ideally query the semantic analyzer
    // For now, return empty (will be inferred from call site)
    return {};
}

bool MethodResolver::methodExists(const std::string& className, const std::string& methodName) const {
    // This would query the semantic analyzer's symbol table
    return true; // Assume exists for now
}

} // namespace Backends
} // namespace XXML
