// JSONDerive.h - JSON Serialization Derive Handler
// Generates toJson() and fromJson() methods for classes

#ifndef XXML_SEMANTIC_DERIVES_JSON_DERIVE_H
#define XXML_SEMANTIC_DERIVES_JSON_DERIVE_H

#include "Semantic/DeriveHandler.h"

namespace XXML::Semantic::Derives {

class JSONDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "JSON"; }

    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

private:
    // Generate toJson() method that returns JSONObject^
    std::unique_ptr<Parser::MethodDecl> generateToJson(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer);

    // Generate fromJson() static method that returns ClassName^
    std::unique_ptr<Parser::MethodDecl> generateFromJson(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer);

    // Check if a type can be serialized to JSON
    bool canSerializeType(const std::string& typeName, SemanticAnalyzer& analyzer);

    // Get the JSON put method name for a type
    std::string getJsonPutMethod(const std::string& typeName);

    // Get the JSON get method name for a type
    std::string getJsonGetMethod(const std::string& typeName);
};

} // namespace XXML::Semantic::Derives

#endif // XXML_SEMANTIC_DERIVES_JSON_DERIVE_H
