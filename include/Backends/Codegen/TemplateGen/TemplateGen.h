#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Parser/AST.h"
#include "Semantic/SemanticAnalyzer.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates code for template instantiations
 *
 * Responsible for:
 * - Processing template instantiation requests from semantic analyzer
 * - Cloning template class AST with type substitutions
 * - Generating mangled names for template instantiations
 * - Coordinating with DeclCodegen to generate instantiated classes
 */
class TemplateGen {
public:
    explicit TemplateGen(CodegenContext& ctx);
    ~TemplateGen() = default;

    /// Set the semantic analyzer to get template instantiation info
    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) {
        semanticAnalyzer_ = analyzer;
    }

    /// Generate all pending template instantiations
    void generateAll();

    /// Generate a specific template instantiation
    void generateInstantiation(const std::string& templateName,
                               const std::vector<Parser::TemplateArgument>& args);

    /// Check if a template instantiation has already been generated
    bool isGenerated(const std::string& mangledName) const {
        return generatedInstantiations_.count(mangledName) > 0;
    }

    /// Mark an instantiation as generated
    void markGenerated(const std::string& mangledName) {
        generatedInstantiations_.insert(mangledName);
    }

    /// Generate mangled name for a template instantiation
    static std::string mangleTemplateName(const std::string& templateName,
                                          const std::vector<Parser::TemplateArgument>& args,
                                          const std::vector<int64_t>& evaluatedValues = {});

    /// Clone a class declaration with type substitutions
    std::unique_ptr<Parser::ClassDecl> cloneAndSubstitute(
        Parser::ClassDecl* templateClass,
        const std::string& newName,
        const std::unordered_map<std::string, std::string>& typeMap);

private:
    CodegenContext& ctx_;
    Semantic::SemanticAnalyzer* semanticAnalyzer_ = nullptr;
    std::unordered_set<std::string> generatedInstantiations_;

    /// Clone a type reference with substitutions
    std::unique_ptr<Parser::TypeRef> cloneTypeRef(
        Parser::TypeRef* typeRef,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Clone an expression with substitutions
    std::unique_ptr<Parser::Expression> cloneExpression(
        Parser::Expression* expr,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Clone a statement with substitutions
    std::unique_ptr<Parser::Statement> cloneStatement(
        Parser::Statement* stmt,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Clone a method declaration with substitutions
    std::unique_ptr<Parser::MethodDecl> cloneMethod(
        Parser::MethodDecl* method,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Clone a constructor declaration with substitutions
    std::unique_ptr<Parser::ConstructorDecl> cloneConstructor(
        Parser::ConstructorDecl* ctor,
        const std::string& newClassName,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Clone a property declaration with substitutions
    std::unique_ptr<Parser::PropertyDecl> cloneProperty(
        Parser::PropertyDecl* prop,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Substitute type parameter in a type name
    std::string substituteType(const std::string& typeName,
                               const std::unordered_map<std::string, std::string>& typeMap);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
