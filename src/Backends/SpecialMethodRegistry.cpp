#include "Backends/SpecialMethodRegistry.h"

namespace XXML {
namespace Backends {

std::unordered_set<std::string> SpecialMethodRegistry::voidMethods_ = {
    "List_add",
    "List_Integer_add",
    "Console_print",
    "Console_printLine",
    "Console_printInt",
    "Console_printBool",
    "String_destroy",
};

std::unordered_map<std::string, std::vector<std::string>> SpecialMethodRegistry::methodSignatures_;

bool SpecialMethodRegistry::isConstructor(const std::string& methodName) {
    return methodName == "Constructor" || methodName.find("_Constructor") != std::string::npos;
}

bool SpecialMethodRegistry::isDestructor(const std::string& methodName) {
    return methodName == "Destructor" || methodName == "dispose" ||
           methodName.find("_Destructor") != std::string::npos ||
           methodName.find("_dispose") != std::string::npos;
}

bool SpecialMethodRegistry::isOperator(const std::string& methodName) {
    return methodName == "add" || methodName == "sub" || methodName == "mul" || methodName == "div" ||
           methodName == "eq" || methodName == "ne" || methodName == "lt" || methodName == "le" ||
           methodName == "gt" || methodName == "ge" || methodName == "and" || methodName == "or" ||
           methodName == "not";
}

bool SpecialMethodRegistry::returnsVoid(const std::string& fullyQualifiedName) {
    return voidMethods_.find(fullyQualifiedName) != voidMethods_.end();
}

std::vector<std::string> SpecialMethodRegistry::getParameterTypes(const std::string& fullyQualifiedName) {
    auto it = methodSignatures_.find(fullyQualifiedName);
    if (it != methodSignatures_.end()) {
        return it->second;
    }
    return {};
}

void SpecialMethodRegistry::registerMethod(const std::string& name, bool returnsVoid,
                                           const std::vector<std::string>& paramTypes) {
    if (returnsVoid) {
        voidMethods_.insert(name);
    }
    methodSignatures_[name] = paramTypes;
}

} // namespace Backends
} // namespace XXML
