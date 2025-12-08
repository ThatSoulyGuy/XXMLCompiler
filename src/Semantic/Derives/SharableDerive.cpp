#include "../../../include/Semantic/Derives/SharableDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"
#include <set>

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult SharableDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Sharable is a marker trait - no methods to generate
    // The canDerive() check ensures the class meets requirements
    DeriveResult result;
    return result;
}

std::string SharableDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Get all properties (not just public - internal state matters for thread safety)
    auto properties = getAllProperties(classDecl);

    for (auto* prop : properties) {
        if (!prop->type) {
            return "Property '" + prop->name + "' has no type";
        }

        std::string typeName = prop->type->typeName;

        // Strip ownership markers from type name
        if (!typeName.empty() &&
            (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
            typeName.pop_back();
        }

        // Check if the type is Sharable using the analyzer's isSharable method
        std::set<std::string> visited;
        if (!analyzer.isSharable(typeName, visited)) {
            return "Property '" + prop->name + "' of type '" + typeName +
                   "' is not Sharable. All fields must be of Sharable types "
                   "for the class to be safely shared across threads.";
        }
    }

    return "";  // All checks passed
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
