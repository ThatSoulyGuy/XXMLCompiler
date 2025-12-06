#pragma once

#include "Backends/Codegen/TemplateCodegen/ASTCloner.h"
#include "Parser/AST.h"
#include <string>
#include <sstream>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>

namespace XXML {

namespace Semantic { class SemanticAnalyzer; }

namespace Backends {
namespace Codegen {

// Forward declarations
class CodegenContext;
class ModularCodegen;

/**
 * @brief Handles lambda template instantiation (monomorphization)
 *
 * This class generates LLVM IR for lambda template instantiations.
 * It uses text-based IR emission (legacy pattern) and integrates with
 * ModularCodegen for statement generation within lambda bodies.
 */
class LambdaTemplateCodegen {
public:
    /// Information about a generated lambda function
    struct LambdaInfo {
        std::string functionName;           // e.g., @lambda.template.identity_LT_Integer_GT_
        std::string returnType;             // LLVM return type
        std::vector<std::string> paramTypes; // LLVM param types (including closure ptr)
    };

    LambdaTemplateCodegen(CodegenContext& ctx, ModularCodegen* modularCodegen);
    ~LambdaTemplateCodegen() = default;

    // Non-copyable
    LambdaTemplateCodegen(const LambdaTemplateCodegen&) = delete;
    LambdaTemplateCodegen& operator=(const LambdaTemplateCodegen&) = delete;

    /**
     * Generate all lambda template instantiations.
     * Reads from SemanticAnalyzer's lambda template instantiation records.
     *
     * @param analyzer The semantic analyzer with template instantiation info
     */
    void generateLambdaTemplates(Semantic::SemanticAnalyzer& analyzer);

    /**
     * Get generated lambda function definitions as IR text.
     * These need to be appended to the module output.
     */
    const std::vector<std::string>& getLambdaDefinitions() const {
        return lambdaDefinitions_;
    }

    /**
     * Look up the function name for a lambda template instantiation.
     * @param key Either "varname<Type>" format or mangled "varname_LT_Type_GT_" format
     * @return The function name or empty string if not found
     */
    std::string getLambdaFunction(const std::string& key) const;

    /**
     * Look up the LambdaInfo for a lambda template instantiation.
     * @param key Either "varname<Type>" format or mangled name
     * @return Pointer to LambdaInfo or nullptr if not found
     */
    const LambdaInfo* getLambdaInfo(const std::string& key) const;

    /**
     * Check if a lambda instantiation has been generated.
     */
    bool isInstantiationGenerated(const std::string& mangledName) const;

    /**
     * Clear all state for a fresh compilation.
     */
    void reset();

private:
    CodegenContext& ctx_;
    ModularCodegen* modularCodegen_;  // For statement generation in lambda bodies
    std::unique_ptr<ASTCloner> cloner_;

    // Track generated instantiations to avoid duplicates
    std::set<std::string> generatedInstantiations_;

    // Lambda function definitions to emit
    std::vector<std::string> lambdaDefinitions_;

    // Function name lookup: "identity<Integer>" or "identity_LT_Integer_GT_" -> "@lambda.template.identity_LT_Integer_GT_"
    std::unordered_map<std::string, std::string> lambdaFunctions_;

    // Lambda info lookup for parameter types etc.
    std::unordered_map<std::string, LambdaInfo> lambdaInfos_;

    // Counter for unique lambda IDs
    int lambdaCounter_ = 0;

    /**
     * Generate mangled name for a lambda template instantiation.
     * E.g., "identity" + [Integer] -> "identity_LT_Integer_GT_"
     */
    std::string mangleName(const std::string& variableName,
                          const std::vector<Parser::TemplateArgument>& args);

    /**
     * Build type substitution map from template parameters to concrete types.
     */
    ASTCloner::TypeMap buildTypeMap(
        const std::vector<Parser::TemplateParameter>& params,
        const std::vector<Parser::TemplateArgument>& args,
        const std::vector<int64_t>& evaluatedValues);

    /**
     * Map XXML type to LLVM type string.
     */
    std::string getLLVMType(const std::string& xxmlType) const;

    /**
     * Get default return value for an LLVM type.
     */
    static std::string getDefaultReturnValue(const std::string& llvmType);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
