#include "../../../include/Semantic/Derives/EqDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult EqDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    DeriveResult result;

    // Get all public properties
    auto properties = getPublicProperties(classDecl);

    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    if (properties.empty()) {
        // No properties to compare - always equal
        std::vector<std::unique_ptr<Parser::Expression>> trueArgs;
        trueArgs.push_back(std::make_unique<Parser::BoolLiteralExpr>(true, Common::SourceLocation{}));
        auto returnTrue = makeStaticCall("Bool", "Constructor", std::move(trueArgs));
        bodyStmts.push_back(makeReturn(std::move(returnTrue)));
    } else {
        // For each property, generate:
        // Instantiate Bool^ As <_eq_N> = prop.equals(other.prop);
        // Instantiate Bool^ As <_neq_N> = _eq_N.not();
        // If (_neq_N) -> { Return Bool::Constructor(false); }
        int propIndex = 0;
        for (auto* prop : properties) {
            std::string eqVarName = "_eq_" + std::to_string(propIndex);
            std::string neqVarName = "_neq_" + std::to_string(propIndex);

            // Build: prop.equals(other.prop)
            auto propIdent = makeIdent(prop->name);
            auto otherPropAccess = makeMemberAccess(makeIdent("other"), prop->name);

            std::vector<std::unique_ptr<Parser::Expression>> equalsArgs;
            equalsArgs.push_back(std::move(otherPropAccess));

            auto equalsCall = makeCall(
                makeMemberAccess(std::move(propIdent), "equals"),
                std::move(equalsArgs)
            );

            // Instantiate Bool^ As <_eq_N> = prop.equals(other.prop);
            auto eqDecl = std::make_unique<Parser::InstantiateStmt>(
                makeType("Bool", Parser::OwnershipType::Owned),
                eqVarName,
                std::move(equalsCall),
                Common::SourceLocation{}
            );
            bodyStmts.push_back(std::move(eqDecl));

            // Build: _eq_N.not()
            auto notCall = makeCall(
                makeMemberAccess(makeIdent(eqVarName), "not"),
                {}
            );

            // Instantiate Bool^ As <_neq_N> = _eq_N.not();
            auto neqDecl = std::make_unique<Parser::InstantiateStmt>(
                makeType("Bool", Parser::OwnershipType::Owned),
                neqVarName,
                std::move(notCall),
                Common::SourceLocation{}
            );
            bodyStmts.push_back(std::move(neqDecl));

            // Build: Return Bool::Constructor(false)
            std::vector<std::unique_ptr<Parser::Expression>> falseArgs;
            falseArgs.push_back(std::make_unique<Parser::BoolLiteralExpr>(false, Common::SourceLocation{}));
            auto returnFalse = makeStaticCall("Bool", "Constructor", std::move(falseArgs));

            std::vector<std::unique_ptr<Parser::Statement>> ifBody;
            ifBody.push_back(makeReturn(std::move(returnFalse)));

            // Build the If statement with _neq_N
            auto ifStmt = std::make_unique<Parser::IfStmt>(
                makeIdent(neqVarName),
                std::move(ifBody),
                std::vector<std::unique_ptr<Parser::Statement>>{},  // No else branch
                Common::SourceLocation{}
            );

            bodyStmts.push_back(std::move(ifStmt));
            propIndex++;
        }

        // All properties matched - return true
        std::vector<std::unique_ptr<Parser::Expression>> trueArgs;
        trueArgs.push_back(std::make_unique<Parser::BoolLiteralExpr>(true, Common::SourceLocation{}));
        auto returnTrue = makeStaticCall("Bool", "Constructor", std::move(trueArgs));
        bodyStmts.push_back(makeReturn(std::move(returnTrue)));
    }

    // Create the method declaration
    // Method <equals> Returns Bool^ Parameters (Parameter <other> Types ClassName&)
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    params.push_back(makeParam("other", classDecl->name, Parser::OwnershipType::Reference));

    auto method = std::make_unique<Parser::MethodDecl>(
        "equals",
        makeType("Bool", Parser::OwnershipType::Owned),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    result.methods.push_back(std::move(method));
    return result;
}

std::string EqDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Check that all public properties have an equals method
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

        if (!hasEqualsMethod(typeName, analyzer)) {
            return "Property '" + prop->name + "' of type '" + typeName +
                   "' does not have an equals() method";
        }
    }

    return "";  // All checks passed
}

std::unique_ptr<Parser::Expression> EqDeriveHandler::generatePropertyEquals(
    Parser::PropertyDecl* prop,
    SemanticAnalyzer& analyzer) {

    // prop.equals(other.prop)
    auto propIdent = makeIdent(prop->name);
    auto otherProp = makeMemberAccess(makeIdent("other"), prop->name);

    std::vector<std::unique_ptr<Parser::Expression>> args;
    args.push_back(std::move(otherProp));

    return makeCall(
        makeMemberAccess(std::move(propIdent), "equals"),
        std::move(args)
    );
}

bool EqDeriveHandler::hasEqualsMethod(
    const std::string& typeName,
    SemanticAnalyzer& analyzer) {

    // Built-in types that have equals
    static const std::set<std::string> builtinWithEquals = {
        "Integer", "Language::Core::Integer",
        "Float", "Language::Core::Float",
        "Double", "Language::Core::Double",
        "Bool", "Language::Core::Bool",
        "String", "Language::Core::String",
        "Char", "Language::Core::Char"
    };

    if (builtinWithEquals.count(typeName) > 0) {
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
        // Check if the class has an equals method
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "equals") {
                return true;
            }
        }
    }

    return false;
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
