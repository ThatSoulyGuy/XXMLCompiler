#include "Backends/DestructorManager.h"
#include "Backends/NameMangler.h"
#include <sstream>

namespace XXML {
namespace Backends {

void DestructorManager::registerVariable(const std::string& name, const std::string& type,
                                        const std::string& llvmRegister) {
    if (needsDestruction(type)) {
        variables_.push_back({name, type, llvmRegister, true});
    }
}

std::vector<std::string> DestructorManager::generateDestructorCalls() const {
    std::vector<std::string> calls;
    
    // Generate destructor calls in reverse order (LIFO)
    for (auto it = variables_.rbegin(); it != variables_.rend(); ++it) {
        if (it->needsDestruction) {
            std::ostringstream call;

            // Call dispose method if it exists
            std::string destructorName = NameMangler::mangleMethod(it->type, "dispose");
            call << "call void @" << destructorName << "(ptr " << it->llvmRegister << ")";

            calls.push_back(call.str());
        }
    }

    return calls;
}

bool DestructorManager::needsDestruction(const std::string& typeName) {
    // Primitive types don't need destruction
    if (typeName == "Integer" || typeName == "Bool" || typeName == "Float" ||
        typeName == "Double" || typeName == "None") {
        return false;
    }

    // String and all class types need destruction
    return true;
}

void DestructorManager::clear() {
    variables_.clear();
}

} // namespace Backends
} // namespace XXML
