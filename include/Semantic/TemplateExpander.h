#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include "PassResults.h"
#include "SemanticError.h"

namespace XXML {
namespace Semantic {

// Forward declarations - types are defined in PassResults.h

//==============================================================================
// TEMPLATE EXPANDER
//
// This pass fully instantiates all templates before semantic validation.
// It performs:
//   1. Constraint definition validation (at registration time)
//   2. Template instantiation (clone AST with type substitutions)
//   3. Constraint satisfaction proofs
//
// After this pass completes successfully:
//   - All template parameters are bound to concrete types
//   - All constraints are validated
//   - No unexpanded templates remain
//==============================================================================

class TemplateExpander {
public:
    // Template instantiation request (from semantic analysis)
    struct InstantiationRequest {
        std::string templateName;
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;  // For non-type parameters
        Common::SourceLocation location;
    };

    // Method template instantiation request
    struct MethodInstantiationRequest {
        std::string className;
        std::string instantiatedClassName;
        std::string methodName;
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;
        Common::SourceLocation location;
    };

    // Lambda template instantiation request
    struct LambdaInstantiationRequest {
        std::string variableName;
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;
        Common::SourceLocation location;
    };

    // Constructor
    TemplateExpander(Common::ErrorReporter& errorReporter,
                     const TypeResolutionResult& typeResolution);

    // Main entry point
    TemplateExpansionResult run(
        const std::set<InstantiationRequest>& classInstantiations,
        const std::set<MethodInstantiationRequest>& methodInstantiations,
        const std::set<LambdaInstantiationRequest>& lambdaInstantiations,
        const std::unordered_map<std::string, TemplateClassInfo>& templateClasses,
        const std::unordered_map<std::string, TemplateMethodInfo>& templateMethods,
        const std::unordered_map<std::string, TemplateLambdaInfo>& templateLambdas);

    // Validate constraint definition (called during registration)
    bool validateConstraintDefinition(Parser::ConstraintDecl* decl);

    // Get the result
    const TemplateExpansionResult& result() const { return result_; }

    // Query instantiated templates
    const InstantiatedClass* getInstantiatedClass(const std::string& mangledName) const;
    const InstantiatedMethod* getInstantiatedMethod(const std::string& mangledName) const;
    const InstantiatedLambda* getInstantiatedLambda(const std::string& mangledName) const;

private:
    Common::ErrorReporter& errorReporter_;
    const TypeResolutionResult& typeResolution_;
    TemplateExpansionResult result_;

    // Constraint registry (from semantic analyzer)
    struct ConstraintInfo {
        std::string name;
        std::vector<Parser::TemplateParameter> templateParams;
        std::vector<Parser::ConstraintParamBinding> paramBindings;
        std::vector<Parser::RequireStmt*> requirements;
    };
    std::unordered_map<std::string, ConstraintInfo> constraintRegistry_;

    // Mangling helpers
    std::string mangleClassName(const std::string& baseName,
                                const std::vector<Parser::TemplateArgument>& args);
    std::string mangleMethodName(const std::string& className,
                                 const std::string& methodName,
                                 const std::vector<Parser::TemplateArgument>& args);
    std::string mangleLambdaName(const std::string& variableName,
                                 const std::vector<Parser::TemplateArgument>& args);

    // Substitution helpers
    std::unordered_map<std::string, std::string> buildSubstitutionMap(
        const std::vector<Parser::TemplateParameter>& params,
        const std::vector<Parser::TemplateArgument>& args);

    // AST cloning with substitution
    std::unique_ptr<Parser::ClassDecl> cloneClassWithSubstitution(
        Parser::ClassDecl* original,
        const std::unordered_map<std::string, std::string>& substitutions);
    std::unique_ptr<Parser::MethodDecl> cloneMethodWithSubstitution(
        Parser::MethodDecl* original,
        const std::unordered_map<std::string, std::string>& substitutions);
    std::unique_ptr<Parser::LambdaExpr> cloneLambdaWithSubstitution(
        Parser::LambdaExpr* original,
        const std::unordered_map<std::string, std::string>& substitutions);

    // Constraint validation
    ConstraintProof proveConstraint(const std::string& typeName,
                                    const Parser::ConstraintRef& constraint,
                                    const std::unordered_map<std::string, std::string>& substitutions);
    std::vector<ConstraintProof> validateAllConstraints(
        const std::vector<Parser::TemplateParameter>& params,
        const std::vector<Parser::TemplateArgument>& args,
        const std::unordered_map<std::string, std::string>& substitutions);

    // Requirement validation
    bool validateMethodRequirement(const std::string& typeName,
                                   const std::string& methodName,
                                   Parser::TypeRef* returnType);
    bool validateConstructorRequirement(const std::string& typeName,
                                        const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes);
    bool validateTruthRequirement(Parser::Expression* expr,
                                  const std::unordered_map<std::string, std::string>& substitutions);

    // Instantiation
    InstantiatedClass instantiateClass(
        const InstantiationRequest& request,
        const TemplateClassInfo& templateInfo);
    InstantiatedMethod instantiateMethod(
        const MethodInstantiationRequest& request,
        const TemplateMethodInfo& templateInfo);
    InstantiatedLambda instantiateLambda(
        const LambdaInstantiationRequest& request,
        const TemplateLambdaInfo& templateInfo);
};

// Comparison operators for request types (for std::set)
inline bool operator<(const TemplateExpander::InstantiationRequest& a,
                      const TemplateExpander::InstantiationRequest& b) {
    if (a.templateName != b.templateName) return a.templateName < b.templateName;
    if (a.arguments.size() != b.arguments.size()) return a.arguments.size() < b.arguments.size();
    for (size_t i = 0; i < a.arguments.size(); ++i) {
        if (a.arguments[i].kind != b.arguments[i].kind) return a.arguments[i].kind < b.arguments[i].kind;
        if (a.arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
            if (a.arguments[i].typeArg != b.arguments[i].typeArg)
                return a.arguments[i].typeArg < b.arguments[i].typeArg;
        }
    }
    return false;
}

inline bool operator<(const TemplateExpander::MethodInstantiationRequest& a,
                      const TemplateExpander::MethodInstantiationRequest& b) {
    if (a.className != b.className) return a.className < b.className;
    if (a.methodName != b.methodName) return a.methodName < b.methodName;
    if (a.arguments.size() != b.arguments.size()) return a.arguments.size() < b.arguments.size();
    for (size_t i = 0; i < a.arguments.size(); ++i) {
        if (a.arguments[i].kind != b.arguments[i].kind) return a.arguments[i].kind < b.arguments[i].kind;
        if (a.arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
            if (a.arguments[i].typeArg != b.arguments[i].typeArg)
                return a.arguments[i].typeArg < b.arguments[i].typeArg;
        }
    }
    return false;
}

inline bool operator<(const TemplateExpander::LambdaInstantiationRequest& a,
                      const TemplateExpander::LambdaInstantiationRequest& b) {
    if (a.variableName != b.variableName) return a.variableName < b.variableName;
    if (a.arguments.size() != b.arguments.size()) return a.arguments.size() < b.arguments.size();
    for (size_t i = 0; i < a.arguments.size(); ++i) {
        if (a.arguments[i].kind != b.arguments[i].kind) return a.arguments[i].kind < b.arguments[i].kind;
        if (a.arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
            if (a.arguments[i].typeArg != b.arguments[i].typeArg)
                return a.arguments[i].typeArg < b.arguments[i].typeArg;
        }
    }
    return false;
}

} // namespace Semantic
} // namespace XXML
