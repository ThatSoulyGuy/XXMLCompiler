#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace XXML {
namespace Backends {

/**
 * Manages destructor calls for proper resource cleanup
 */
class DestructorManager {
public:
    struct Variable {
        std::string name;
        std::string type;
        std::string llvmRegister;
        bool needsDestruction;
    };

    DestructorManager() = default;

    // Register a variable that needs destruction
    void registerVariable(const std::string& name, const std::string& type,
                         const std::string& llvmRegister);

    // Generate destructor calls for all registered variables
    std::vector<std::string> generateDestructorCalls() const;

    // Check if a type needs destruction
    static bool needsDestruction(const std::string& typeName);

    // Clear all registered variables
    void clear();

private:
    std::vector<Variable> variables_;
};

} // namespace Backends
} // namespace XXML
