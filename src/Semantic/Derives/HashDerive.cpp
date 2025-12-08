#include "../../../include/Semantic/Derives/HashDerive.h"
#include "../../../include/Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult HashDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    DeriveResult result;

    // Get all public properties
    auto properties = getPublicProperties(classDecl);

    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    // Instantiate Integer^ As <result> = Integer::Constructor(17);
    auto initValue = makeStaticCall("Integer", "Constructor", [&]() {
        std::vector<std::unique_ptr<Parser::Expression>> args;
        args.push_back(std::make_unique<Parser::IntegerLiteralExpr>(17, Common::SourceLocation{}));
        return args;
    }());

    auto resultDecl = std::make_unique<Parser::InstantiateStmt>(
        makeType("Integer", Parser::OwnershipType::Owned),
        "result",
        std::move(initValue),
        Common::SourceLocation{}
    );
    bodyStmts.push_back(std::move(resultDecl));

    // For each property: Set result = result.multiply(Integer::Constructor(31)).add(Integer::Constructor(prop.hash()));
    for (auto* prop : properties) {
        // Build: prop.hash() - returns NativeType<"int64">
        auto propHash = makeCall(
            makeMemberAccess(makeIdent(prop->name), "hash"),
            {}
        );

        // Wrap in Integer::Constructor()
        std::vector<std::unique_ptr<Parser::Expression>> intArgs;
        intArgs.push_back(std::move(propHash));
        auto propHashInt = makeStaticCall("Integer", "Constructor", std::move(intArgs));

        // Build: Integer::Constructor(31)
        std::vector<std::unique_ptr<Parser::Expression>> mul31Args;
        mul31Args.push_back(std::make_unique<Parser::IntegerLiteralExpr>(31, Common::SourceLocation{}));
        auto int31 = makeStaticCall("Integer", "Constructor", std::move(mul31Args));

        // Build: result.multiply(Integer::Constructor(31))
        std::vector<std::unique_ptr<Parser::Expression>> mulArgs;
        mulArgs.push_back(std::move(int31));
        auto mulResult = makeCall(
            makeMemberAccess(makeIdent("result"), "multiply"),
            std::move(mulArgs)
        );

        // Build: .add(propHashInt)
        std::vector<std::unique_ptr<Parser::Expression>> addArgs;
        addArgs.push_back(std::move(propHashInt));
        auto finalResult = makeCall(
            makeMemberAccess(std::move(mulResult), "add"),
            std::move(addArgs)
        );

        // Build: Set result = ...
        auto setStmt = std::make_unique<Parser::AssignmentStmt>(
            makeIdent("result"),
            std::move(finalResult),
            Common::SourceLocation{}
        );
        bodyStmts.push_back(std::move(setStmt));
    }

    // Return result.toInt64();
    auto returnExpr = makeCall(
        makeMemberAccess(makeIdent("result"), "toInt64"),
        {}
    );
    bodyStmts.push_back(makeReturn(std::move(returnExpr)));

    // Create the method declaration
    // Method <hash> Returns NativeType<"int64">^ Parameters ()
    // We need to create NativeType<"int64"> with string value argument
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;  // No parameters

    // Create NativeType<"int64"> TypeRef with string template argument
    auto strLiteral = std::make_unique<Parser::StringLiteralExpr>("int64", Common::SourceLocation{});
    std::vector<Parser::TemplateArgument> templateArgs;
    templateArgs.emplace_back(std::move(strLiteral), "int64", Common::SourceLocation{});

    auto returnType = std::make_unique<Parser::TypeRef>(
        "NativeType",
        std::move(templateArgs),
        Parser::OwnershipType::Owned,
        Common::SourceLocation{}
    );

    auto method = std::make_unique<Parser::MethodDecl>(
        "hash",
        std::move(returnType),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    result.methods.push_back(std::move(method));
    return result;
}

std::string HashDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Check that all public properties have a hash method
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

        if (!hasHashMethod(typeName, analyzer)) {
            return "Property '" + prop->name + "' of type '" + typeName +
                   "' does not have a hash() method";
        }
    }

    return "";  // All checks passed
}

bool HashDeriveHandler::hasHashMethod(
    const std::string& typeName,
    SemanticAnalyzer& analyzer) {

    // Built-in types that have hash
    static const std::set<std::string> builtinWithHash = {
        "Integer", "Language::Core::Integer",
        "String", "Language::Core::String"
    };

    if (builtinWithHash.count(typeName) > 0) {
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
        // Check if the class has a hash method
        for (const auto& [methodName, methodInfo] : it->second.methods) {
            if (methodName == "hash") {
                return true;
            }
        }
    }

    return false;
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
