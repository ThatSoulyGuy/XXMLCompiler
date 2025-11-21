#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace XXML {
namespace Backends {

/**
 * Registry for special methods (constructors, operators, etc.)
 */
class SpecialMethodRegistry {
public:
    // Check if a method is a constructor
    static bool isConstructor(const std::string& methodName);

    // Check if a method is a destructor
    static bool isDestructor(const std::string& methodName);

    // Check if a method is an operator
    static bool isOperator(const std::string& methodName);

    // Check if a method returns void
    static bool returnsVoid(const std::string& fullyQualifiedName);

    // Get expected parameter types for a known method
    static std::vector<std::string> getParameterTypes(const std::string& fullyQualifiedName);

    // Register a special method
    static void registerMethod(const std::string& name, bool returnsVoid, const std::vector<std::string>& paramTypes);

private:
    static std::unordered_set<std::string> voidMethods_;
    static std::unordered_map<std::string, std::vector<std::string>> methodSignatures_;
};

} // namespace Backends
} // namespace XXML
