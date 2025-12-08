#include "../../../include/Semantic/Derives/ToStringDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult ToStringDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    DeriveResult result;

    // Get all public properties
    auto properties = getPublicProperties(classDecl);

    // Build the toString method body
    // Format: ClassName{prop1=value1, prop2=value2, ...}

    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    // Start with: String::Constructor("ClassName{")
    std::unique_ptr<Parser::Expression> currentExpr;

    if (properties.empty()) {
        // No properties: just return "ClassName{}"
        std::vector<std::unique_ptr<Parser::Expression>> args;
        args.push_back(makeStringLiteral(classDecl->name + "{}"));
        currentExpr = makeStaticCall("String", "Constructor", std::move(args));
    } else {
        // Build: String::Constructor("ClassName{prop1=")
        std::vector<std::unique_ptr<Parser::Expression>> startArgs;
        startArgs.push_back(makeStringLiteral(classDecl->name + "{" + properties[0]->name + "="));
        currentExpr = makeStaticCall("String", "Constructor", std::move(startArgs));

        // Chain: .append(prop1.toString())
        currentExpr = makeCall(
            makeMemberAccess(std::move(currentExpr), "append"),
            [&]() {
                std::vector<std::unique_ptr<Parser::Expression>> args;
                args.push_back(generatePropertyToString(properties[0], analyzer));
                return args;
            }()
        );

        // For each subsequent property: .append(", propN=").append(propN.toString())
        for (size_t i = 1; i < properties.size(); ++i) {
            // .append(", propN=")
            auto sepArgs = [&]() {
                std::vector<std::unique_ptr<Parser::Expression>> args;
                std::vector<std::unique_ptr<Parser::Expression>> strArgs;
                strArgs.push_back(makeStringLiteral(", " + properties[i]->name + "="));
                args.push_back(makeStaticCall("String", "Constructor", std::move(strArgs)));
                return args;
            }();

            currentExpr = makeCall(
                makeMemberAccess(std::move(currentExpr), "append"),
                std::move(sepArgs)
            );

            // .append(propN.toString())
            currentExpr = makeCall(
                makeMemberAccess(std::move(currentExpr), "append"),
                [&]() {
                    std::vector<std::unique_ptr<Parser::Expression>> args;
                    args.push_back(generatePropertyToString(properties[i], analyzer));
                    return args;
                }()
            );
        }

        // .append("}")
        auto endArgs = [&]() {
            std::vector<std::unique_ptr<Parser::Expression>> args;
            std::vector<std::unique_ptr<Parser::Expression>> strArgs;
            strArgs.push_back(makeStringLiteral("}"));
            args.push_back(makeStaticCall("String", "Constructor", std::move(strArgs)));
            return args;
        }();

        currentExpr = makeCall(
            makeMemberAccess(std::move(currentExpr), "append"),
            std::move(endArgs)
        );
    }

    // Create the return statement
    bodyStmts.push_back(makeReturn(std::move(currentExpr)));

    // Create the method declaration
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;  // No parameters

    auto method = std::make_unique<Parser::MethodDecl>(
        "toString",
        makeType("String", Parser::OwnershipType::Owned),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    result.methods.push_back(std::move(method));
    return result;
}

std::string ToStringDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Check that all public properties can be converted to string
    auto properties = getPublicProperties(classDecl);

    for (auto* prop : properties) {
        if (!prop->type) {
            return "Property '" + prop->name + "' has no type";
        }

        std::string typeName = prop->type->typeName;

        // Strip ownership markers
        if (!typeName.empty() &&
            (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
            typeName.pop_back();
        }

        // String is always ok
        if (typeName == "String" || typeName == "Language::Core::String") {
            continue;
        }

        // Check if the type has a toString method
        if (!hasToStringMethod(typeName, analyzer)) {
            return "Property '" + prop->name + "' of type '" + typeName +
                   "' does not have a toString() method";
        }
    }

    return "";  // All checks passed
}

std::unique_ptr<Parser::Expression> ToStringDeriveHandler::generatePropertyToString(
    Parser::PropertyDecl* prop,
    SemanticAnalyzer& analyzer) {

    std::string typeName = prop->type ? prop->type->typeName : "";

    // Strip ownership markers
    if (!typeName.empty() &&
        (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
        typeName.pop_back();
    }

    // For String type, just return the property
    if (typeName == "String" || typeName == "Language::Core::String") {
        return makeIdent(prop->name);
    }

    // For other types, call .toString()
    return makeCall(
        makeMemberAccess(makeIdent(prop->name), "toString"),
        {}  // No arguments
    );
}

bool ToStringDeriveHandler::hasToStringMethod(
    const std::string& typeName,
    SemanticAnalyzer& analyzer) {

    // Built-in types that have toString
    static const std::set<std::string> builtinWithToString = {
        "Integer", "Language::Core::Integer",
        "Float", "Language::Core::Float",
        "Double", "Language::Core::Double",
        "Bool", "Language::Core::Bool",
        "String", "Language::Core::String",
        "Char", "Language::Core::Char"
    };

    if (builtinWithToString.count(typeName) > 0) {
        return true;
    }

    // Check class registry for user-defined types
    const auto& classRegistry = analyzer.getClassRegistry();

    auto it = classRegistry.find(typeName);
    if (it == classRegistry.end()) {
        // Try with Language::Core:: prefix
        it = classRegistry.find("Language::Core::" + typeName);
    }

    if (it != classRegistry.end()) {
        // Check if the class has a toString method
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "toString") {
                return true;
            }
        }
    }

    // TODO: Check base class for toString method

    return false;
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
