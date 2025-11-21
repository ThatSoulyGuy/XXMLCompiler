#pragma once

#include <string>
#include <vector>

namespace XXML {

namespace Semantic {
class SemanticAnalyzer;
}

namespace Backends {

/**
 * Guards semantic operations and provides safe access to semantic analyzer
 */
class SemanticGuard {
public:
    explicit SemanticGuard(Semantic::SemanticAnalyzer* analyzer) : analyzer_(analyzer) {}

    // Query method return type safely
    std::string getMethodReturnType(const std::string& className, const std::string& methodName) const;

    // Query method parameter types safely
    std::vector<std::string> getMethodParameterTypes(const std::string& className, const std::string& methodName) const;

    // Check if a type exists
    bool typeExists(const std::string& typeName) const;

    // Get variable type
    std::string getVariableType(const std::string& varName) const;

private:
    Semantic::SemanticAnalyzer* analyzer_;
};

} // namespace Backends
} // namespace XXML
