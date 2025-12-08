#include "../../../include/Semantic/Derives/JSONDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult JSONDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    DeriveResult result;

    // Generate toJson() method
    result.methods.push_back(generateToJson(classDecl, analyzer));

    // Generate fromJson() static method
    result.methods.push_back(generateFromJson(classDecl, analyzer));

    return result;
}

std::string JSONDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

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

        if (!canSerializeType(typeName, analyzer)) {
            return "Property '" + prop->name + "' of type '" + typeName +
                   "' cannot be serialized to JSON";
        }
    }

    return "";  // All checks passed
}

std::unique_ptr<Parser::MethodDecl> JSONDeriveHandler::generateToJson(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    auto properties = getPublicProperties(classDecl);
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    // Instantiate JSONObject^ As <json> = JSONObject::Constructor();
    auto jsonObjArgs = std::vector<std::unique_ptr<Parser::Expression>>();
    auto jsonObjCreate = makeStaticCall("JSONObject", "Constructor", std::move(jsonObjArgs));

    auto jsonInstantiate = std::make_unique<Parser::InstantiateStmt>(
        makeType("JSONObject", Parser::OwnershipType::Owned),
        "json",
        std::move(jsonObjCreate),
        Common::SourceLocation{}
    );
    bodyStmts.push_back(std::move(jsonInstantiate));

    // For each property: Run json.putXxx(String::Constructor("propName"), propValue);
    for (auto* prop : properties) {
        std::string typeName = prop->type ? prop->type->typeName : "";

        // Strip ownership markers
        if (!typeName.empty() &&
            (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
            typeName.pop_back();
        }

        // Build the key argument: String::Constructor("propName")
        std::vector<std::unique_ptr<Parser::Expression>> keyStrArgs;
        keyStrArgs.push_back(makeStringLiteral(prop->name));
        auto keyArg = makeStaticCall("String", "Constructor", std::move(keyStrArgs));

        // Build the value expression
        std::unique_ptr<Parser::Expression> valueExpr;

        // Get the appropriate put method and value
        std::string putMethod = getJsonPutMethod(typeName);

        if (putMethod == "putObject") {
            // Nested object: call toJson() on property
            valueExpr = makeCall(
                makeMemberAccess(makeIdent(prop->name), "toJson"),
                {}
            );
        } else {
            // Primitive: just use the property
            valueExpr = makeIdent(prop->name);
        }

        // Build: json.putXxx(key, value)
        std::vector<std::unique_ptr<Parser::Expression>> putArgs;
        putArgs.push_back(std::move(keyArg));
        putArgs.push_back(std::move(valueExpr));

        auto putCall = makeCall(
            makeMemberAccess(makeIdent("json"), putMethod),
            std::move(putArgs)
        );

        auto runStmt = std::make_unique<Parser::RunStmt>(
            std::move(putCall),
            Common::SourceLocation{}
        );
        bodyStmts.push_back(std::move(runStmt));
    }

    // Return json;
    bodyStmts.push_back(makeReturn(makeIdent("json")));

    // Create method declaration
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;

    auto method = std::make_unique<Parser::MethodDecl>(
        "toJson",
        makeType("JSONObject", Parser::OwnershipType::Owned),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    return method;
}

std::unique_ptr<Parser::MethodDecl> JSONDeriveHandler::generateFromJson(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    auto properties = getPublicProperties(classDecl);
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    // Instantiate ClassName^ As <obj> = ClassName::Constructor();
    auto ctorArgs = std::vector<std::unique_ptr<Parser::Expression>>();
    auto ctorCall = makeStaticCall(classDecl->name, "Constructor", std::move(ctorArgs));

    auto objInstantiate = std::make_unique<Parser::InstantiateStmt>(
        makeType(classDecl->name, Parser::OwnershipType::Owned),
        "obj",
        std::move(ctorCall),
        Common::SourceLocation{}
    );
    bodyStmts.push_back(std::move(objInstantiate));

    // For each property: Set obj.propName = json.getXxx(String::Constructor("propName"));
    for (auto* prop : properties) {
        std::string typeName = prop->type ? prop->type->typeName : "";

        // Strip ownership markers
        if (!typeName.empty() &&
            (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
            typeName.pop_back();
        }

        // Build the key argument: String::Constructor("propName")
        std::vector<std::unique_ptr<Parser::Expression>> keyStrArgs;
        keyStrArgs.push_back(makeStringLiteral(prop->name));
        auto keyArg = makeStaticCall("String", "Constructor", std::move(keyStrArgs));

        // Get the appropriate get method
        std::string getMethod = getJsonGetMethod(typeName);

        // Build: json.getXxx(key)
        std::vector<std::unique_ptr<Parser::Expression>> getArgs;
        getArgs.push_back(std::move(keyArg));

        std::unique_ptr<Parser::Expression> valueExpr;

        if (getMethod == "getObject") {
            // Nested object: call ClassName::fromJson(json.getObject(...))
            auto getObjCall = makeCall(
                makeMemberAccess(makeIdent("json"), "getObject"),
                std::move(getArgs)
            );

            std::vector<std::unique_ptr<Parser::Expression>> fromJsonArgs;
            fromJsonArgs.push_back(std::move(getObjCall));
            valueExpr = makeStaticCall(typeName, "fromJson", std::move(fromJsonArgs));
        } else {
            // Primitive: json.getXxx(key)
            valueExpr = makeCall(
                makeMemberAccess(makeIdent("json"), getMethod),
                std::move(getArgs)
            );
        }

        // Build: Set obj.propName = valueExpr;
        auto target = makeMemberAccess(makeIdent("obj"), prop->name);
        auto assignStmt = std::make_unique<Parser::AssignmentStmt>(
            std::move(target),
            std::move(valueExpr),
            Common::SourceLocation{}
        );
        bodyStmts.push_back(std::move(assignStmt));
    }

    // Return obj;
    bodyStmts.push_back(makeReturn(makeIdent("obj")));

    // Create parameter: Parameter <json> Types JSONObject&
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    params.push_back(std::make_unique<Parser::ParameterDecl>(
        "json",
        makeType("JSONObject", Parser::OwnershipType::Reference),
        Common::SourceLocation{}
    ));

    auto method = std::make_unique<Parser::MethodDecl>(
        "fromJson",
        makeType(classDecl->name, Parser::OwnershipType::Owned),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    return method;
}

bool JSONDeriveHandler::canSerializeType(const std::string& typeName, SemanticAnalyzer& analyzer) {
    // Built-in JSON-serializable types
    static const std::set<std::string> builtinSerializable = {
        "Integer", "Language::Core::Integer",
        "Float", "Language::Core::Float",
        "Double", "Language::Core::Double",
        "Bool", "Language::Core::Bool",
        "String", "Language::Core::String"
    };

    if (builtinSerializable.count(typeName) > 0) {
        return true;
    }

    // Check if the class has @Derive(trait = "JSON") or has toJson/fromJson methods
    const auto& classRegistry = analyzer.getClassRegistry();

    auto it = classRegistry.find(typeName);
    if (it != classRegistry.end()) {
        bool hasToJson = false;
        bool hasFromJson = false;

        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "toJson") hasToJson = true;
            if (methodName == "fromJson") hasFromJson = true;
        }

        return hasToJson && hasFromJson;
    }

    return false;
}

std::string JSONDeriveHandler::getJsonPutMethod(const std::string& typeName) {
    if (typeName == "Integer" || typeName == "Language::Core::Integer") {
        return "putInteger";
    }
    if (typeName == "Float" || typeName == "Language::Core::Float" ||
        typeName == "Double" || typeName == "Language::Core::Double") {
        return "putNumber";
    }
    if (typeName == "Bool" || typeName == "Language::Core::Bool") {
        return "putBool";
    }
    if (typeName == "String" || typeName == "Language::Core::String") {
        return "putString";
    }
    // Default: nested object
    return "putObject";
}

std::string JSONDeriveHandler::getJsonGetMethod(const std::string& typeName) {
    if (typeName == "Integer" || typeName == "Language::Core::Integer") {
        return "getInteger";
    }
    if (typeName == "Float" || typeName == "Language::Core::Float" ||
        typeName == "Double" || typeName == "Language::Core::Double") {
        return "getNumber";
    }
    if (typeName == "Bool" || typeName == "Language::Core::Bool") {
        return "getBool";
    }
    if (typeName == "String" || typeName == "Language::Core::String") {
        return "getString";
    }
    // Default: nested object
    return "getObject";
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
