#pragma once

#include <string>
#include <vector>

namespace XXML {

namespace Semantic {
class SemanticAnalyzer;
}

namespace Backends {

/**
 * Resolves method calls to their LLVM IR representations
 */
class MethodResolver {
public:
    explicit MethodResolver(Semantic::SemanticAnalyzer* analyzer) : analyzer_(analyzer) {}

    // Resolve a method call to a fully qualified name
    std::string resolveMethod(const std::string& className, const std::string& methodName) const;

    // Get return type of a method
    std::string getReturnType(const std::string& className, const std::string& methodName) const;

    // Get parameter types of a method
    std::vector<std::string> getParameterTypes(const std::string& className, const std::string& methodName) const;

    // Check if method exists
    bool methodExists(const std::string& className, const std::string& methodName) const;

private:
    Semantic::SemanticAnalyzer* analyzer_;
};

} // namespace Backends
} // namespace XXML
