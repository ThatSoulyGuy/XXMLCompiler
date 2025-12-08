#pragma once

#include "Backends/Codegen/TemplateCodegen/ASTCloner.h"
#include "Parser/AST.h"
#include <string>
#include <set>
#include <unordered_map>
#include <memory>
#include <vector>

namespace XXML {

namespace Semantic { class SemanticAnalyzer; }

namespace Backends {
namespace Codegen {

// Forward declarations
class CodegenContext;
class DeclCodegen;

/**
 * @brief Analysis result for a template method
 *
 * Determines how a method uses type parameters, which affects
 * whether it can be shared across instantiations.
 */
struct TemplateMethodAnalysis {
    std::string methodName;

    // Method doesn't use T at all (e.g., size(), isEmpty())
    bool isTypeIndependent = false;

    // Method only needs sizeof(T) at runtime (most operations)
    bool usesOnlySize = false;

    // Method needs type-specific operations (comparison, hashing, etc.)
    bool usesComparison = false;
    bool usesHashing = false;
    bool usesToString = false;

    // Method returns a type dependent on T (e.g., get() returns T^)
    bool hasTypeDependentReturn = false;

    // Method has parameters of type T (e.g., add() takes T^)
    bool hasTypeDependentParams = false;

    // Method references other templates with T (e.g., ListIterator<T>)
    bool usesNestedTemplates = false;

    // Can this method use a shared base implementation?
    bool canShareImplementation() const {
        return isTypeIndependent || (usesOnlySize && !usesComparison &&
               !usesHashing && !usesToString && !usesNestedTemplates);
    }
};

/**
 * @brief Analysis results for an entire template class
 */
struct TemplateClassAnalysis {
    std::string templateName;
    std::vector<std::string> typeParams;
    std::vector<TemplateMethodAnalysis> methods;

    // Does this class have any methods that can share implementation?
    bool hasShareableMethods() const {
        for (const auto& m : methods) {
            if (m.canShareImplementation()) return true;
        }
        return false;
    }
};

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

    /**
     * Pre-register method return types from a cloned class.
     * Called during Phase 1 before code generation to ensure all return types are known.
     */
    void preRegisterMethodReturnTypes(Parser::ClassDecl* classDecl, const std::string& fullClassName);

    // ===== Template Deduplication (Type-Erasure Sharing) =====

    /**
     * Analyze a template class to determine which methods can be shared.
     */
    TemplateClassAnalysis analyzeTemplateClass(
        Parser::ClassDecl* classDecl,
        const std::vector<Parser::TemplateParameter>& templateParams);

    /**
     * Analyze a single method to determine its type parameter usage.
     */
    TemplateMethodAnalysis analyzeMethod(
        Parser::MethodDecl* method,
        const std::set<std::string>& typeParams);

    /**
     * Check if a type reference uses any type parameter.
     */
    bool usesTypeParam(Parser::TypeRef* typeRef, const std::set<std::string>& typeParams);

    /**
     * Check if an expression uses any type parameter.
     */
    bool exprUsesTypeParam(Parser::Expression* expr, const std::set<std::string>& typeParams);

    /**
     * Check if a statement uses any type parameter.
     */
    bool stmtUsesTypeParam(Parser::Statement* stmt, const std::set<std::string>& typeParams);

    /**
     * Generate a shared base implementation for a template class.
     * Returns the mangled name of the generated base class.
     */
    std::string generateSharedBase(
        Parser::ClassDecl* classDecl,
        const std::string& templateName,
        const TemplateClassAnalysis& analysis);

    /**
     * Generate a thin type-specific wrapper that calls the shared base.
     */
    void generateTypeWrapper(
        Parser::ClassDecl* originalClass,
        const std::string& mangledClassName,
        const std::string& sharedBaseName,
        const TemplateClassAnalysis& analysis,
        const ASTCloner::TypeMap& typeMap);

    // Track which shared bases have been generated
    std::set<std::string> generatedSharedBases_;

    // Map from template name to its analysis result
    std::unordered_map<std::string, TemplateClassAnalysis> templateAnalysisCache_;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
