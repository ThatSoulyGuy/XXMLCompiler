#include "../../include/Semantic/TemplateExpander.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <sstream>

namespace XXML {
namespace Semantic {

TemplateExpander::TemplateExpander(Common::ErrorReporter& errorReporter,
                                   const TypeResolutionResult& typeResolution)
    : errorReporter_(errorReporter),
      typeResolution_(typeResolution) {
}

TemplateExpansionResult TemplateExpander::run(
    const std::set<InstantiationRequest>& classInstantiations,
    const std::set<MethodInstantiationRequest>& methodInstantiations,
    const std::set<LambdaInstantiationRequest>& lambdaInstantiations,
    const std::unordered_map<std::string, SemanticAnalyzer::TemplateClassInfo>& templateClasses,
    const std::unordered_map<std::string, SemanticAnalyzer::TemplateMethodInfo>& templateMethods,
    const std::unordered_map<std::string, SemanticAnalyzer::TemplateLambdaInfo>& templateLambdas) {

    result_ = TemplateExpansionResult{};

    // Instantiate template classes
    for (const auto& request : classInstantiations) {
        auto it = templateClasses.find(request.templateName);
        if (it == templateClasses.end()) {
            errorReporter_.reportError(
                Common::ErrorCode::UndefinedType,
                "Template class '" + request.templateName + "' not found",
                request.location
            );
            continue;
        }

        InstantiatedClass instantiated = instantiateClass(request, it->second);
        if (instantiated.valid) {
            result_.instantiatedClasses.push_back(std::move(instantiated));
        }
    }

    // Instantiate template methods
    for (const auto& request : methodInstantiations) {
        std::string key = request.className + "::" + request.methodName;
        auto it = templateMethods.find(key);
        if (it == templateMethods.end()) {
            errorReporter_.reportError(
                Common::ErrorCode::InvalidMethodCall,
                "Template method '" + key + "' not found",
                request.location
            );
            continue;
        }

        InstantiatedMethod instantiated = instantiateMethod(request, it->second);
        if (instantiated.valid) {
            result_.instantiatedMethods.push_back(std::move(instantiated));
        }
    }

    // Instantiate template lambdas
    for (const auto& request : lambdaInstantiations) {
        auto it = templateLambdas.find(request.variableName);
        if (it == templateLambdas.end()) {
            errorReporter_.reportError(
                Common::ErrorCode::UndeclaredIdentifier,
                "Template lambda '" + request.variableName + "' not found",
                request.location
            );
            continue;
        }

        InstantiatedLambda instantiated = instantiateLambda(request, it->second);
        if (instantiated.valid) {
            result_.instantiatedLambdas.push_back(std::move(instantiated));
        }
    }

    result_.success = !errorReporter_.hasErrors();
    return result_;
}

bool TemplateExpander::validateConstraintDefinition(Parser::ConstraintDecl* decl) {
    if (!decl) return false;

    // Store constraint info
    ConstraintInfo info;
    info.name = decl->name;
    info.templateParams = decl->templateParams;
    info.paramBindings = decl->paramBindings;
    for (const auto& req : decl->requirements) {
        info.requirements.push_back(req.get());
    }
    constraintRegistry_[decl->name] = info;

    // Validate that requirements reference valid template parameters
    std::set<std::string> validParams;
    for (const auto& param : decl->templateParams) {
        validParams.insert(param.name);
    }

    // TODO: Validate requirement expressions reference valid parameters

    return true;
}

const InstantiatedClass* TemplateExpander::getInstantiatedClass(const std::string& mangledName) const {
    for (const auto& c : result_.instantiatedClasses) {
        if (c.mangledName == mangledName) return &c;
    }
    return nullptr;
}

const InstantiatedMethod* TemplateExpander::getInstantiatedMethod(const std::string& mangledName) const {
    for (const auto& m : result_.instantiatedMethods) {
        if (m.mangledName == mangledName) return &m;
    }
    return nullptr;
}

const InstantiatedLambda* TemplateExpander::getInstantiatedLambda(const std::string& mangledName) const {
    for (const auto& l : result_.instantiatedLambdas) {
        if (l.mangledName == mangledName) return &l;
    }
    return nullptr;
}

//==============================================================================
// MANGLING
//==============================================================================

std::string TemplateExpander::mangleClassName(const std::string& baseName,
                                               const std::vector<Parser::TemplateArgument>& args) {
    std::ostringstream oss;

    // Convert qualified base name (e.g., "Language::Collections::List" -> "Language_Collections_List")
    std::string mangledBase = baseName;
    size_t pos = 0;
    while ((pos = mangledBase.find("::")) != std::string::npos) {
        mangledBase.replace(pos, 2, "_");
    }
    oss << mangledBase;

    for (const auto& arg : args) {
        oss << "_";
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Type args are now qualified - convert :: to _
            std::string typeArg = arg.typeArg;
            pos = 0;
            while ((pos = typeArg.find("::")) != std::string::npos) {
                typeArg.replace(pos, 2, "_");
            }
            oss << typeArg;
        } else if (arg.kind == Parser::TemplateArgument::Kind::Value) {
            oss << "V";  // Value marker - actual value substituted later
        }
    }
    return oss.str();
}

std::string TemplateExpander::mangleMethodName(const std::string& className,
                                                const std::string& methodName,
                                                const std::vector<Parser::TemplateArgument>& args) {
    std::ostringstream oss;

    // className may be qualified or already mangled - ensure :: is converted
    std::string mangledClass = className;
    size_t pos = 0;
    while ((pos = mangledClass.find("::")) != std::string::npos) {
        mangledClass.replace(pos, 2, "_");
    }

    oss << mangledClass << "_" << methodName << "_LT_";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << "_C_";
        if (args[i].kind == Parser::TemplateArgument::Kind::Type) {
            // Type args are now qualified - convert :: to _
            std::string typeArg = args[i].typeArg;
            pos = 0;
            while ((pos = typeArg.find("::")) != std::string::npos) {
                typeArg.replace(pos, 2, "_");
            }
            oss << typeArg;
        }
    }
    oss << "_GT_";
    return oss.str();
}

std::string TemplateExpander::mangleLambdaName(const std::string& variableName,
                                                const std::vector<Parser::TemplateArgument>& args) {
    std::ostringstream oss;
    oss << "lambda_" << variableName;
    for (const auto& arg : args) {
        oss << "_";
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            std::string typeArg = arg.typeArg;
            size_t pos = 0;
            while ((pos = typeArg.find("::")) != std::string::npos) {
                typeArg.replace(pos, 2, "_");
            }
            oss << typeArg;
        }
    }
    return oss.str();
}

//==============================================================================
// SUBSTITUTION
//==============================================================================

std::unordered_map<std::string, std::string> TemplateExpander::buildSubstitutionMap(
    const std::vector<Parser::TemplateParameter>& params,
    const std::vector<Parser::TemplateArgument>& args) {

    std::unordered_map<std::string, std::string> subs;

    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        if (args[i].kind == Parser::TemplateArgument::Kind::Type) {
            subs[params[i].name] = args[i].typeArg;
        }
        // Value arguments are substituted differently
    }

    return subs;
}

//==============================================================================
// AST CLONING WITH SUBSTITUTION
//==============================================================================

std::unique_ptr<Parser::ClassDecl> TemplateExpander::cloneClassWithSubstitution(
    Parser::ClassDecl* original,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!original) return nullptr;

    // Clone the class declaration
    auto cloned = std::unique_ptr<Parser::ClassDecl>(
        static_cast<Parser::ClassDecl*>(original->clone().release())
    );

    // Apply substitutions to the cloned AST
    // TODO: Implement deep substitution in all type references

    return cloned;
}

std::unique_ptr<Parser::MethodDecl> TemplateExpander::cloneMethodWithSubstitution(
    Parser::MethodDecl* original,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!original) return nullptr;

    auto cloned = std::unique_ptr<Parser::MethodDecl>(
        static_cast<Parser::MethodDecl*>(original->clone().release())
    );

    // Apply substitutions
    // TODO: Implement deep substitution

    return cloned;
}

std::unique_ptr<Parser::LambdaExpr> TemplateExpander::cloneLambdaWithSubstitution(
    Parser::LambdaExpr* original,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!original) return nullptr;

    auto cloned = std::unique_ptr<Parser::LambdaExpr>(
        static_cast<Parser::LambdaExpr*>(original->cloneExpr().release())
    );

    // Apply substitutions
    // TODO: Implement deep substitution

    return cloned;
}

//==============================================================================
// CONSTRAINT VALIDATION
//==============================================================================

ConstraintProof TemplateExpander::proveConstraint(
    const std::string& typeName,
    const Parser::ConstraintRef& constraint,
    const std::unordered_map<std::string, std::string>& substitutions) {

    ConstraintProof proof;
    proof.constraintName = constraint.name;
    proof.typeArgs = constraint.templateArgs;
    proof.satisfied = false;

    // Look up constraint definition
    auto it = constraintRegistry_.find(constraint.name);
    if (it == constraintRegistry_.end()) {
        proof.failureReason = "Constraint '" + constraint.name + "' not defined";
        return proof;
    }

    const ConstraintInfo& info = it->second;

    // Build substitution map for constraint parameters
    std::unordered_map<std::string, std::string> constraintSubs = substitutions;
    for (size_t i = 0; i < info.templateParams.size() && i < constraint.templateArgs.size(); ++i) {
        constraintSubs[info.templateParams[i].name] = constraint.templateArgs[i];
    }

    // Validate each requirement
    for (const auto* req : info.requirements) {
        // TODO: Validate requirement with substitutions
    }

    proof.satisfied = true;
    return proof;
}

std::vector<ConstraintProof> TemplateExpander::validateAllConstraints(
    const std::vector<Parser::TemplateParameter>& params,
    const std::vector<Parser::TemplateArgument>& args,
    const std::unordered_map<std::string, std::string>& substitutions) {

    std::vector<ConstraintProof> proofs;

    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        const auto& param = params[i];
        const auto& arg = args[i];

        if (arg.kind != Parser::TemplateArgument::Kind::Type) continue;

        // Validate each constraint on this parameter
        for (const auto& constraint : param.constraints) {
            ConstraintProof proof = proveConstraint(arg.typeArg, constraint, substitutions);
            proof.location = arg.location;
            proofs.push_back(proof);

            if (!proof.satisfied && param.constraintsAreAnd) {
                // AND semantics: all must pass
                break;
            }
        }
    }

    return proofs;
}

//==============================================================================
// REQUIREMENT VALIDATION
//==============================================================================

bool TemplateExpander::validateMethodRequirement(const std::string& typeName,
                                                  const std::string& methodName,
                                                  Parser::TypeRef* returnType) {
    // Look up the type in type resolution result
    const QualifiedType* type = typeResolution_.lookupType(typeName);
    if (!type) return false;

    // TODO: Look up method in class registry
    return true;
}

bool TemplateExpander::validateConstructorRequirement(
    const std::string& typeName,
    const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes) {

    const QualifiedType* type = typeResolution_.lookupType(typeName);
    if (!type) return false;

    // TODO: Look up constructor in class registry
    return true;
}

bool TemplateExpander::validateTruthRequirement(
    Parser::Expression* expr,
    const std::unordered_map<std::string, std::string>& substitutions) {

    // TODO: Evaluate truth expression with substitutions
    return true;
}

//==============================================================================
// INSTANTIATION
//==============================================================================

InstantiatedClass TemplateExpander::instantiateClass(
    const InstantiationRequest& request,
    const SemanticAnalyzer::TemplateClassInfo& templateInfo) {

    InstantiatedClass result;
    result.originalName = request.templateName;
    result.mangledName = mangleClassName(request.templateName, request.arguments);
    result.valid = false;

    // Build substitution map
    auto subs = buildSubstitutionMap(templateInfo.templateParams, request.arguments);
    for (const auto& [k, v] : subs) {
        result.substitutions.push_back({k, v});
    }

    // Validate constraints
    result.constraintProofs = validateAllConstraints(
        templateInfo.templateParams, request.arguments, subs);

    // Check all constraints satisfied
    for (const auto& proof : result.constraintProofs) {
        if (!proof.satisfied) {
            errorReporter_.reportError(
                Common::ErrorCode::ConstraintViolation,
                "Constraint '" + proof.constraintName + "' not satisfied: " + proof.failureReason,
                request.location
            );
            return result;
        }
    }

    // Clone and substitute AST (if available)
    if (templateInfo.astNode) {
        result.expandedAST = cloneClassWithSubstitution(templateInfo.astNode, subs).release();
    }

    result.valid = true;
    return result;
}

InstantiatedMethod TemplateExpander::instantiateMethod(
    const MethodInstantiationRequest& request,
    const SemanticAnalyzer::TemplateMethodInfo& templateInfo) {

    InstantiatedMethod result;
    result.className = request.className;
    result.instantiatedClassName = request.instantiatedClassName;
    result.methodName = request.methodName;
    result.mangledName = mangleMethodName(
        request.instantiatedClassName, request.methodName, request.arguments);
    result.valid = false;

    // Build substitution map
    auto subs = buildSubstitutionMap(templateInfo.templateParams, request.arguments);
    for (const auto& [k, v] : subs) {
        result.substitutions.push_back({k, v});
    }

    // Validate constraints
    result.constraintProofs = validateAllConstraints(
        templateInfo.templateParams, request.arguments, subs);

    for (const auto& proof : result.constraintProofs) {
        if (!proof.satisfied) {
            errorReporter_.reportError(
                Common::ErrorCode::ConstraintViolation,
                "Constraint '" + proof.constraintName + "' not satisfied for method template: " +
                proof.failureReason,
                request.location
            );
            return result;
        }
    }

    // Clone and substitute
    if (templateInfo.astNode) {
        result.expandedAST = cloneMethodWithSubstitution(templateInfo.astNode, subs).release();
    }

    result.valid = true;
    return result;
}

InstantiatedLambda TemplateExpander::instantiateLambda(
    const LambdaInstantiationRequest& request,
    const TemplateLambdaInfo& templateInfo) {

    InstantiatedLambda result;
    result.variableName = request.variableName;
    result.mangledName = mangleLambdaName(request.variableName, request.arguments);
    result.valid = false;

    // Build substitution map
    auto subs = buildSubstitutionMap(templateInfo.templateParams, request.arguments);
    for (const auto& [k, v] : subs) {
        result.substitutions.push_back({k, v});
    }

    // Clone and substitute
    if (templateInfo.astNode) {
        result.expandedAST = cloneLambdaWithSubstitution(templateInfo.astNode, subs).release();
    }

    result.valid = true;
    return result;
}

} // namespace Semantic
} // namespace XXML
