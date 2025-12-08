#include "../../include/Semantic/DeriveHandler.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <iostream>
#include <set>

namespace XXML {
namespace Semantic {

// ============================================================================
// DeriveHandler Helper Methods
// ============================================================================

std::unique_ptr<Parser::TypeRef> DeriveHandler::makeType(
    const std::string& typeName,
    Parser::OwnershipType ownership) {

    return std::make_unique<Parser::TypeRef>(
        typeName,
        ownership,
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::ParameterDecl> DeriveHandler::makeParam(
    const std::string& name,
    const std::string& typeName,
    Parser::OwnershipType ownership) {

    return std::make_unique<Parser::ParameterDecl>(
        name,
        makeType(typeName, ownership),
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::IdentifierExpr> DeriveHandler::makeIdent(const std::string& name) {
    return std::make_unique<Parser::IdentifierExpr>(name, Common::SourceLocation{});
}

std::unique_ptr<Parser::MemberAccessExpr> DeriveHandler::makeMemberAccess(
    std::unique_ptr<Parser::Expression> object,
    const std::string& member) {

    return std::make_unique<Parser::MemberAccessExpr>(
        std::move(object),
        member,
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::CallExpr> DeriveHandler::makeCall(
    std::unique_ptr<Parser::Expression> callee,
    std::vector<std::unique_ptr<Parser::Expression>> args) {

    return std::make_unique<Parser::CallExpr>(
        std::move(callee),
        std::move(args),
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::CallExpr> DeriveHandler::makeStaticCall(
    const std::string& className,
    const std::string& methodName,
    std::vector<std::unique_ptr<Parser::Expression>> args) {

    // Create Class::method callee
    auto classIdent = makeIdent(className);
    auto memberAccess = std::make_unique<Parser::MemberAccessExpr>(
        std::move(classIdent),
        "::" + methodName,  // Parser convention for static calls
        Common::SourceLocation{}
    );

    return std::make_unique<Parser::CallExpr>(
        std::move(memberAccess),
        std::move(args),
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::ReturnStmt> DeriveHandler::makeReturn(
    std::unique_ptr<Parser::Expression> value) {

    return std::make_unique<Parser::ReturnStmt>(
        std::move(value),
        Common::SourceLocation{}
    );
}

std::unique_ptr<Parser::StringLiteralExpr> DeriveHandler::makeStringLiteral(const std::string& value) {
    return std::make_unique<Parser::StringLiteralExpr>(value, Common::SourceLocation{});
}

std::vector<Parser::PropertyDecl*> DeriveHandler::getPublicProperties(Parser::ClassDecl* classDecl) {
    std::vector<Parser::PropertyDecl*> result;

    for (const auto& section : classDecl->sections) {
        if (!section) continue;
        if (section->modifier != Parser::AccessModifier::Public) continue;

        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                result.push_back(prop);
            }
        }
    }

    return result;
}

std::vector<Parser::PropertyDecl*> DeriveHandler::getAllProperties(Parser::ClassDecl* classDecl) {
    std::vector<Parser::PropertyDecl*> result;

    for (const auto& section : classDecl->sections) {
        if (!section) continue;

        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                result.push_back(prop);
            }
        }
    }

    return result;
}

// ============================================================================
// DeriveRegistry Implementation
// ============================================================================

DeriveRegistry::DeriveRegistry() {
    // Built-in derives will be registered by SemanticAnalyzer
}

void DeriveRegistry::registerHandler(std::unique_ptr<DeriveHandler> handler) {
    std::string name = handler->getDeriveName();
    handlers_[name] = std::move(handler);
}

DeriveHandler* DeriveRegistry::getHandler(const std::string& name) {
    auto it = handlers_.find(name);
    if (it != handlers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool DeriveRegistry::processClassDerives(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Look for Derive annotations on the class
    for (const auto& annotation : classDecl->annotations) {
        if (!annotation) continue;

        // Check if this is a Derive annotation
        if (annotation->annotationName != "Derive") continue;

        // Extract the derive trait name from the 'trait' argument
        // Syntax: @Derive(trait = String::Constructor("ToString"))
        // Or: @Derive(trait = "ToString")
        std::string traitName;
        for (const auto& [argName, argExpr] : annotation->arguments) {
            if (argName == "trait") {
                // Try to extract string value from the expression
                if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(argExpr.get())) {
                    traitName = strLit->value;
                } else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(argExpr.get())) {
                    // String::Constructor("ToString") pattern
                    if (!callExpr->arguments.empty()) {
                        if (auto* innerStr = dynamic_cast<Parser::StringLiteralExpr*>(
                                callExpr->arguments[0].get())) {
                            traitName = innerStr->value;
                        }
                    }
                } else if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(argExpr.get())) {
                    // Just an identifier like: trait = ToString
                    traitName = identExpr->name;
                }
                break;
            }
        }

        if (traitName.empty()) {
            std::cerr << "[Derive] Error: Derive annotation requires a 'trait' argument, "
                      << "e.g., @Derive(trait = \"ToString\")\n";
            continue;
        }

        // Strip ownership markers if present
        if (!traitName.empty() &&
            (traitName.back() == '^' || traitName.back() == '&' || traitName.back() == '%')) {
            traitName.pop_back();
        }

        // Get the handler for this derive trait
        DeriveHandler* handler = getHandler(traitName);
        if (!handler) {
            std::cerr << "[Derive] Warning: Unknown derive trait '" << traitName
                      << "' for class '" << classDecl->name << "'\n";
            continue;
        }

        // Check if derive can be applied
        std::string canDeriveError = handler->canDerive(classDecl, analyzer);
        if (!canDeriveError.empty()) {
            std::cerr << "[Derive] Error: Cannot derive " << traitName
                      << " for class '" << classDecl->name << "': "
                      << canDeriveError << "\n";
            return false;
        }

        // Generate the derived code
        DeriveResult result = handler->generate(classDecl, analyzer);

        if (result.hasErrors()) {
            for (const auto& error : result.errors) {
                std::cerr << "[Derive] Error in " << traitName
                          << " for class '" << classDecl->name << "': "
                          << error << "\n";
            }
            return false;
        }

        // Add generated methods to the class
        // Find the public section or create one
        Parser::AccessSection* publicSection = nullptr;
        for (auto& section : classDecl->sections) {
            if (section && section->modifier == Parser::AccessModifier::Public) {
                publicSection = section.get();
                break;
            }
        }

        if (!publicSection) {
            // Create a new public section
            auto newSection = std::make_unique<Parser::AccessSection>(
                Parser::AccessModifier::Public, Common::SourceLocation{});
            classDecl->sections.push_back(std::move(newSection));
            publicSection = classDecl->sections.back().get();
        }

        // Move generated methods into the class
        for (auto& method : result.methods) {
            std::cout << "[Derive] Generated method '" << method->name
                      << "' for class '" << classDecl->name << "'\n";
            publicSection->declarations.push_back(std::move(method));
        }

        // Move generated properties into the class
        for (auto& prop : result.properties) {
            publicSection->declarations.push_back(std::move(prop));
        }
    }

    return true;
}

std::vector<std::string> DeriveRegistry::getAvailableDerives() const {
    std::vector<std::string> result;
    result.reserve(handlers_.size());
    for (const auto& [name, handler] : handlers_) {
        result.push_back(name);
    }
    return result;
}

// ============================================================================
// DeriveHandler Ownership Helper Methods
// ============================================================================

Parser::OwnershipType DeriveHandler::getPropertyOwnership(Parser::PropertyDecl* prop) {
    if (!prop || !prop->type) {
        return Parser::OwnershipType::None;
    }
    return prop->type->ownership;
}

bool DeriveHandler::isPropertyOwned(Parser::PropertyDecl* prop) {
    return getPropertyOwnership(prop) == Parser::OwnershipType::Owned;
}

bool DeriveHandler::isPropertyReference(Parser::PropertyDecl* prop) {
    return getPropertyOwnership(prop) == Parser::OwnershipType::Reference;
}

bool DeriveHandler::isPropertyCopy(Parser::PropertyDecl* prop) {
    return getPropertyOwnership(prop) == Parser::OwnershipType::Copy;
}

bool DeriveHandler::allPropertiesHaveOwnership(
    Parser::ClassDecl* classDecl,
    Parser::OwnershipType ownership) {

    auto properties = getPublicProperties(classDecl);
    for (auto* prop : properties) {
        if (getPropertyOwnership(prop) != ownership) {
            return false;
        }
    }
    return true;
}

std::string DeriveHandler::ownershipToString(Parser::OwnershipType ownership) {
    switch (ownership) {
        case Parser::OwnershipType::Owned:
            return "Owned(^)";
        case Parser::OwnershipType::Reference:
            return "Reference(&)";
        case Parser::OwnershipType::Copy:
            return "Copy(%)";
        case Parser::OwnershipType::None:
        default:
            return "None";
    }
}

std::string DeriveHandler::ownershipToSymbol(Parser::OwnershipType ownership) {
    switch (ownership) {
        case Parser::OwnershipType::Owned:
            return "^";
        case Parser::OwnershipType::Reference:
            return "&";
        case Parser::OwnershipType::Copy:
            return "%";
        case Parser::OwnershipType::None:
        default:
            return "";
    }
}

bool DeriveHandler::canCompareProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer) {
    if (!prop || !prop->type) {
        return false;
    }

    // Get the base type name without ownership marker
    std::string typeName = prop->type->typeName;
    if (!typeName.empty() &&
        (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
        typeName.pop_back();
    }

    // Built-in comparable types
    static const std::set<std::string> comparableTypes = {
        "Integer", "Language::Core::Integer",
        "Float", "Language::Core::Float",
        "Double", "Language::Core::Double",
        "Bool", "Language::Core::Bool",
        "String", "Language::Core::String",
        "Char", "Language::Core::Char"
    };

    if (comparableTypes.count(typeName) > 0) {
        return true;
    }

    // Check if the type has an equals method via class registry
    const auto& classRegistry = analyzer.getClassRegistry();
    auto it = classRegistry.find(typeName);
    if (it == classRegistry.end()) {
        it = classRegistry.find("Language::Core::" + typeName);
    }

    if (it != classRegistry.end()) {
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "equals") {
                return true;
            }
        }
    }

    return false;
}

bool DeriveHandler::canHashProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer) {
    if (!prop || !prop->type) {
        return false;
    }

    // Get the base type name without ownership marker
    std::string typeName = prop->type->typeName;
    if (!typeName.empty() &&
        (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
        typeName.pop_back();
    }

    // Built-in hashable types
    static const std::set<std::string> hashableTypes = {
        "Integer", "Language::Core::Integer",
        "String", "Language::Core::String"
    };

    if (hashableTypes.count(typeName) > 0) {
        return true;
    }

    // Check if the type has a hash method via class registry
    const auto& classRegistry = analyzer.getClassRegistry();
    auto it = classRegistry.find(typeName);
    if (it == classRegistry.end()) {
        it = classRegistry.find("Language::Core::" + typeName);
    }

    if (it != classRegistry.end()) {
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "hash") {
                return true;
            }
        }
    }

    return false;
}

bool DeriveHandler::canStringifyProperty(Parser::PropertyDecl* prop, SemanticAnalyzer& analyzer) {
    if (!prop || !prop->type) {
        return false;
    }

    // Get the base type name without ownership marker
    std::string typeName = prop->type->typeName;
    if (!typeName.empty() &&
        (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
        typeName.pop_back();
    }

    // Built-in stringifiable types
    static const std::set<std::string> stringifiableTypes = {
        "Integer", "Language::Core::Integer",
        "Float", "Language::Core::Float",
        "Double", "Language::Core::Double",
        "Bool", "Language::Core::Bool",
        "String", "Language::Core::String",
        "Char", "Language::Core::Char"
    };

    if (stringifiableTypes.count(typeName) > 0) {
        return true;
    }

    // Check if the type has a toString method via class registry
    const auto& classRegistry = analyzer.getClassRegistry();
    auto it = classRegistry.find(typeName);
    if (it == classRegistry.end()) {
        it = classRegistry.find("Language::Core::" + typeName);
    }

    if (it != classRegistry.end()) {
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "toString") {
                return true;
            }
        }
    }

    return false;
}

} // namespace Semantic
} // namespace XXML
