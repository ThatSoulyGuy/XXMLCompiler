#pragma once

#include "Backends/Codegen/TemplateCodegen/ASTCloner.h"
#include "Parser/AST.h"
#include <string>
#include <set>
#include <unordered_map>
#include <memory>

namespace XXML {

namespace Semantic { class SemanticAnalyzer; }

namespace Backends {
namespace Codegen {

// Forward declarations
class CodegenContext;
class DeclCodegen;

/**
 * @brief Handles template instantiation (monomorphization) for classes and methods
 *
 * This class is responsible for:
 * 1. Processing template class instantiations (e.g., List<Integer>)
 * 2. Processing template method instantiations
 * 3. Cloning and substituting AST nodes using ASTCloner
 * 4. Delegating code generation to DeclCodegen
 */
class TemplateCodegen {
public:
    TemplateCodegen(CodegenContext& ctx, DeclCodegen& declCodegen);
    ~TemplateCodegen() = default;

    // Non-copyable
    TemplateCodegen(const TemplateCodegen&) = delete;
    TemplateCodegen& operator=(const TemplateCodegen&) = delete;

    /**
     * Generate all template class instantiations.
     * Reads from SemanticAnalyzer's template instantiation records.
     *
     * @param analyzer The semantic analyzer with template instantiation info
     */
    void generateClassTemplates(Semantic::SemanticAnalyzer& analyzer);

    /**
     * Generate all template method instantiations.
     * Reads from SemanticAnalyzer's method template instantiation records.
     *
     * @param analyzer The semantic analyzer with template instantiation info
     */
    void generateMethodTemplates(Semantic::SemanticAnalyzer& analyzer);

    /**
     * Check if a template instantiation has been generated.
     */
    bool isInstantiationGenerated(const std::string& mangledName) const;

    /**
     * Mark a template instantiation as generated.
     */
    void markInstantiationGenerated(const std::string& mangledName);

private:
    CodegenContext& ctx_;
    DeclCodegen& declCodegen_;
    std::unique_ptr<ASTCloner> cloner_;

    // Track generated instantiations to avoid duplicates
    std::set<std::string> generatedClassInstantiations_;
    std::set<std::string> generatedMethodInstantiations_;

    /**
     * Generate a mangled name for a template instantiation.
     * E.g., "List" + ["Integer"] -> "List_Integer"
     */
    std::string mangleClassName(const std::string& templateName,
                                const std::vector<Parser::TemplateArgument>& args,
                                const std::vector<int64_t>& evaluatedValues);

    /**
     * Generate a mangled name for a method template instantiation.
     * E.g., "convert" + ["Integer"] -> "convert_LT_Integer_GT_"
     */
    std::string mangleMethodName(const std::string& methodName,
                                 const std::vector<Parser::TemplateArgument>& args);

    /**
     * Build a type substitution map from template parameters to concrete types.
     */
    ASTCloner::TypeMap buildTypeMap(
        const std::vector<Parser::TemplateParameter>& params,
        const std::vector<Parser::TemplateArgument>& args,
        const std::vector<int64_t>& evaluatedValues);

    /**
     * Check if any argument is itself an unbound template parameter.
     */
    bool hasUnboundTemplateParams(
        const std::vector<Parser::TemplateArgument>& args,
        Semantic::SemanticAnalyzer& analyzer);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
