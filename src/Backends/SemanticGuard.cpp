#include "Backends/SemanticGuard.h"
#include "Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Backends {

std::string SemanticGuard::getMethodReturnType(const std::string& className,
                                                const std::string& methodName) const {
    if (!analyzer_) return "ptr";

    // Query the semantic analyzer for the return type
    // This would use analyzer_->getMethodReturnType() if it exists
    // For now, return a default
    return "ptr";
}

std::vector<std::string> SemanticGuard::getMethodParameterTypes(const std::string& className,
                                                                 const std::string& methodName) const {
    if (!analyzer_) return {};

    // Query the semantic analyzer for parameter types
    // This would use analyzer_->getMethodParameterTypes() if it exists
    return {};
}

bool SemanticGuard::typeExists(const std::string& typeName) const {
    if (!analyzer_) return true; // Assume exists if no analyzer

    // Check if type exists in semantic analyzer
    // This would use analyzer_->isTypeName() if it exists
    return true;
}

std::string SemanticGuard::getVariableType(const std::string& varName) const {
    if (!analyzer_) return "ptr";

    // Query variable type from semantic analyzer
    // This would use analyzer_->getVariableType() if it exists
    return "ptr";
}

} // namespace Backends
} // namespace XXML
