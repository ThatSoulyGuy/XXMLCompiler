#include "../../../include/Semantic/Derives/SendableDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"
#include <set>

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult SendableDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Sendable is a marker trait - no methods to generate
    // The canDerive() check ensures the class meets requirements
    DeriveResult result;
    return result;
}

std::string SendableDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Get all properties (not just public - internal state matters for thread safety)
    auto properties = getAllProperties(classDecl);

    for (auto* prop : properties) {
        if (!prop->type) {
            return "Property '" + prop->name + "' has no type";
        }

        // Check ownership
        auto ownership = getPropertyOwnership(prop);

        // Reference fields are NOT Sendable - they could become dangling
        if (ownership == Parser::OwnershipType::Reference) {
            return "Property '" + prop->name + "' has reference (&) ownership. "
                   "Reference fields cannot be Sendable because they may become "
                   "dangling when moved across thread boundaries.";
        }

        // For owned fields, check if the type is Sendable
        if (ownership == Parser::OwnershipType::Owned) {
            std::string typeName = prop->type->typeName;

            // Strip ownership markers from type name
            if (!typeName.empty() &&
                (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
                typeName.pop_back();
            }

            // Check if the type is Sendable using the analyzer's isSendable method
            std::set<std::string> visited;
            if (!analyzer.isSendable(typeName, visited)) {
                return "Property '" + prop->name + "' of type '" + typeName +
                       "' is not Sendable. All owned fields must be of Sendable types.";
            }
        }

        // Copy (%) fields are implicitly Sendable - values are copied
    }

    return "";  // All checks passed
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
