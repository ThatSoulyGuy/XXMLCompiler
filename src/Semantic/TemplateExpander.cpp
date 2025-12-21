#include "../../include/Semantic/TemplateExpander.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <sstream>
#include <iostream>

namespace XXML {
namespace Semantic {

TemplateExpander::TemplateExpander(Common::ErrorReporter& errorReporter,
                                   const TypeResolutionResult& typeResolution,
                                   const std::unordered_map<std::string, ClassInfo>& classRegistry,
                                   const std::unordered_map<std::string, ConstraintInfo>& constraintRegistry)
    : errorReporter_(errorReporter),
      typeResolution_(typeResolution),
      classRegistry_(classRegistry),
      constraintRegistry_(constraintRegistry) {
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

    // Constraints are now registered in SemanticAnalyzer and passed to TemplateExpander,
    // so this function just validates the constraint definition syntax

    // Validate that requirements reference valid template parameters
    std::set<std::string> validParams;
    for (const auto& param : decl->templateParams) {
        validParams.insert(param.name);
    }

    // Also collect parameter binding names
    std::set<std::string> validBindings;
    for (const auto& binding : decl->paramBindings) {
        validBindings.insert(binding.bindingName);
        validParams.insert(binding.templateParamName);
    }

    // Validate requirement expressions reference valid parameters
    if (!validateRequirementParams(decl->requirements, validBindings, decl->location)) {
        return false;
    }

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

    if (!original) {
        return nullptr;
    }

    // Clone the class declaration
    auto cloned = std::unique_ptr<Parser::ClassDecl>(
        static_cast<Parser::ClassDecl*>(original->clone().release())
    );

    // Apply substitutions to the cloned AST - deep substitution in all type references
    // Substitute in base class if present
    if (!cloned->baseClass.empty()) {
        auto it = substitutions.find(cloned->baseClass);
        if (it != substitutions.end()) {
            cloned->baseClass = it->second;
        }
    }

    // Substitute in all sections (properties, methods, constructors)
    for (auto& section : cloned->sections) {
        if (!section) continue;
        for (auto& decl : section->declarations) {
            if (!decl) continue;

            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Substitute return type
                if (method->returnType) {
                    substituteTypeRef(method->returnType.get(), substitutions);
                }
                // Substitute parameter types
                for (auto& param : method->parameters) {
                    if (param && param->type) {
                        substituteTypeRef(param->type.get(), substitutions);
                    }
                }
                // Substitute in method body
                for (auto& stmt : method->body) {
                    substituteInStatement(stmt.get(), substitutions);
                }
            } else if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                // Substitute property type
                if (prop->type) {
                    substituteTypeRef(prop->type.get(), substitutions);
                }
            }
        }
    }

    return cloned;
}

std::unique_ptr<Parser::MethodDecl> TemplateExpander::cloneMethodWithSubstitution(
    Parser::MethodDecl* original,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!original) return nullptr;

    auto cloned = std::unique_ptr<Parser::MethodDecl>(
        static_cast<Parser::MethodDecl*>(original->clone().release())
    );

    // Apply deep substitution to the method
    // Substitute return type
    if (cloned->returnType) {
        substituteTypeRef(cloned->returnType.get(), substitutions);
    }

    // Substitute parameter types
    for (auto& param : cloned->parameters) {
        if (param && param->type) {
            substituteTypeRef(param->type.get(), substitutions);
        }
    }

    // Substitute in method body
    for (auto& stmt : cloned->body) {
        substituteInStatement(stmt.get(), substitutions);
    }

    return cloned;
}

std::unique_ptr<Parser::LambdaExpr> TemplateExpander::cloneLambdaWithSubstitution(
    Parser::LambdaExpr* original,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!original) return nullptr;

    auto cloned = std::unique_ptr<Parser::LambdaExpr>(
        static_cast<Parser::LambdaExpr*>(original->cloneExpr().release())
    );

    // Apply deep substitution to the lambda
    // Substitute return type
    if (cloned->returnType) {
        substituteTypeRef(cloned->returnType.get(), substitutions);
    }

    // Substitute parameter types
    for (auto& param : cloned->parameters) {
        if (param && param->type) {
            substituteTypeRef(param->type.get(), substitutions);
        }
    }

    // Substitute in lambda body
    for (auto& stmt : cloned->body) {
        substituteInStatement(stmt.get(), substitutions);
    }

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
        switch (req->kind) {
            case Parser::RequirementKind::Method:
            case Parser::RequirementKind::CompiletimeMethod: {
                // Look up the target parameter binding to get the actual type
                std::string targetType = typeName;
                for (const auto& binding : info.paramBindings) {
                    if (binding.bindingName == req->targetParam) {
                        // Find the substitution for this template param
                        auto subIt = constraintSubs.find(binding.templateParamName);
                        if (subIt != constraintSubs.end()) {
                            targetType = subIt->second;
                        }
                        break;
                    }
                }

                // Validate the method exists on the target type
                if (!validateMethodRequirement(targetType, req->methodName, req->methodReturnType.get())) {
                    std::string returnTypeStr = req->methodReturnType ?
                        req->methodReturnType->typeName : "?";

                    proof.failureReason = "Type '" + targetType + "' is missing required method:\n"
                        "  " + req->methodName + "() -> " + returnTypeStr + "\n\n"
                        "Hint: Add a '" + req->methodName + "' method to '" + targetType + "'\n"
                        "See: Language/Core/" + constraint.name + ".XXML for constraint definition";
                    return proof;
                }
                break;
            }
            case Parser::RequirementKind::Constructor:
            case Parser::RequirementKind::CompiletimeConstructor: {
                // Look up the target parameter binding
                std::string targetType = typeName;
                for (const auto& binding : info.paramBindings) {
                    if (binding.bindingName == req->targetParam) {
                        auto subIt = constraintSubs.find(binding.templateParamName);
                        if (subIt != constraintSubs.end()) {
                            targetType = subIt->second;
                        }
                        break;
                    }
                }

                if (!validateConstructorRequirement(targetType, req->constructorParamTypes)) {
                    proof.failureReason = "Type '" + targetType + "' is missing required constructor\n\n"
                        "Hint: Add the required constructor to '" + targetType + "'\n"
                        "See: Language/Core/" + constraint.name + ".XXML for constraint definition";
                    return proof;
                }
                break;
            }
            case Parser::RequirementKind::Truth: {
                if (!validateTruthRequirement(req->truthCondition.get(), constraintSubs)) {
                    proof.failureReason = "Type '" + typeName + "' does not satisfy truth condition for constraint '" + constraint.name + "'";
                    return proof;
                }
                break;
            }
        }
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
    // Look up the class in the registry
    auto it = classRegistry_.find(typeName);
    if (it == classRegistry_.end()) {
        // Type not found in registry - could be a builtin or not yet registered
        return false;
    }

    const ClassInfo& classInfo = it->second;

    // Look for the method in the class
    auto methodIt = classInfo.methods.find(methodName);
    if (methodIt == classInfo.methods.end()) {
        return false;
    }

    // Validate return type if specified
    if (returnType && !returnType->typeName.empty()) {
        const std::string& actualReturnType = methodIt->second.returnType;
        const std::string& expectedReturnType = returnType->typeName;

        // Check for exact match or with Language::Core:: prefix
        bool matches = (expectedReturnType == actualReturnType) ||
                       ("Language::Core::" + expectedReturnType == actualReturnType) ||
                       (expectedReturnType == "Language::Core::" + actualReturnType);

        if (!matches) {
            return false;
        }
    }

    return true;
}

bool TemplateExpander::validateConstructorRequirement(
    const std::string& typeName,
    const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes) {

    // Look up the class in the registry
    auto it = classRegistry_.find(typeName);
    if (it == classRegistry_.end()) {
        return false;
    }

    const ClassInfo& classInfo = it->second;

    // Check if class has a Constructor method
    auto methodIt = classInfo.methods.find("Constructor");
    if (methodIt == classInfo.methods.end()) {
        return false;
    }

    // Validate parameter types match if specified
    if (!paramTypes.empty()) {
        const auto& actualParams = methodIt->second.parameters;

        // Check parameter count
        if (paramTypes.size() != actualParams.size()) {
            return false;
        }

        // Check each parameter type
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (!paramTypes[i]) continue;

            const std::string& expectedType = paramTypes[i]->typeName;
            const std::string& actualType = actualParams[i].first;

            // Check for exact match or with Language::Core:: prefix
            bool matches = (expectedType == actualType) ||
                           ("Language::Core::" + expectedType == actualType) ||
                           (expectedType == "Language::Core::" + actualType);

            if (!matches) {
                return false;
            }
        }
    }

    return true;
}

bool TemplateExpander::validateTruthRequirement(
    Parser::Expression* expr,
    const std::unordered_map<std::string, std::string>& substitutions) {

    if (!expr) return true;

    // Handle TypeOf comparisons like: TypeOf<T>() == TypeOf<SomeClass>()
    if (auto* binaryExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        if (binaryExpr->op == "==" || binaryExpr->op == "!=") {
            std::string leftType, rightType;

            // Check if left side is a TypeOf expression or a call to TypeOf
            if (auto* leftTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->left.get())) {
                if (leftTypeOf->type) {
                    leftType = leftTypeOf->type->typeName;
                    // Apply substitution
                    auto it = substitutions.find(leftType);
                    if (it != substitutions.end()) {
                        leftType = it->second;
                    }
                }
            } else if (auto* leftCall = dynamic_cast<Parser::CallExpr*>(binaryExpr->left.get())) {
                if (auto* leftTypeOfCallee = dynamic_cast<Parser::TypeOfExpr*>(leftCall->callee.get())) {
                    if (leftTypeOfCallee->type) {
                        leftType = leftTypeOfCallee->type->typeName;
                        auto subIt = substitutions.find(leftType);
                        if (subIt != substitutions.end()) {
                            leftType = subIt->second;
                        }
                    }
                }
            }

            // Check if right side is a TypeOf expression or a call to TypeOf
            if (auto* rightTypeOf = dynamic_cast<Parser::TypeOfExpr*>(binaryExpr->right.get())) {
                if (rightTypeOf->type) {
                    rightType = rightTypeOf->type->typeName;
                    // Apply substitution
                    auto it = substitutions.find(rightType);
                    if (it != substitutions.end()) {
                        rightType = it->second;
                    }
                }
            } else if (auto* rightCall = dynamic_cast<Parser::CallExpr*>(binaryExpr->right.get())) {
                if (auto* rightTypeOfCallee = dynamic_cast<Parser::TypeOfExpr*>(rightCall->callee.get())) {
                    if (rightTypeOfCallee->type) {
                        rightType = rightTypeOfCallee->type->typeName;
                        auto subIt = substitutions.find(rightType);
                        if (subIt != substitutions.end()) {
                            rightType = subIt->second;
                        }
                    }
                }
            }

            if (!leftType.empty() && !rightType.empty()) {
                bool typesEqual = (leftType == rightType) ||
                                  ("Language::Core::" + leftType == rightType) ||
                                  (leftType == "Language::Core::" + rightType);

                return binaryExpr->op == "==" ? typesEqual : !typesEqual;
            }
        }
    }

    // For other expressions, default to true (conservative)
    // Full compile-time evaluation would require the CompiletimeInterpreter
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

//==============================================================================
// TYPE SUBSTITUTION HELPERS
//==============================================================================

void TemplateExpander::substituteTypeRef(Parser::TypeRef* typeRef,
                                         const std::unordered_map<std::string, std::string>& substitutions) {
    if (!typeRef) return;

    // Direct substitution of the type name
    auto it = substitutions.find(typeRef->typeName);
    if (it != substitutions.end()) {
        typeRef->typeName = it->second;
    }

    // Recursive substitution in template arguments
    for (auto& arg : typeRef->templateArgs) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Substitute the type argument string
            auto argIt = substitutions.find(arg.typeArg);
            if (argIt != substitutions.end()) {
                arg.typeArg = argIt->second;
            }
        }
    }
}

void TemplateExpander::substituteInStatement(Parser::Statement* stmt,
                                             const std::unordered_map<std::string, std::string>& substitutions) {
    if (!stmt) return;

    // Handle different statement types
    if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        if (instStmt->type) {
            substituteTypeRef(instStmt->type.get(), substitutions);
        }
        if (instStmt->initializer) {
            substituteInExpression(instStmt->initializer.get(), substitutions);
        }
    } else if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        if (runStmt->expression) {
            substituteInExpression(runStmt->expression.get(), substitutions);
        }
    } else if (auto* returnStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            substituteInExpression(returnStmt->value.get(), substitutions);
        }
    } else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        if (ifStmt->condition) {
            substituteInExpression(ifStmt->condition.get(), substitutions);
        }
        for (auto& thenStmt : ifStmt->thenBranch) {
            substituteInStatement(thenStmt.get(), substitutions);
        }
        for (auto& elseStmt : ifStmt->elseBranch) {
            substituteInStatement(elseStmt.get(), substitutions);
        }
    } else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        if (whileStmt->condition) {
            substituteInExpression(whileStmt->condition.get(), substitutions);
        }
        for (auto& bodyStmt : whileStmt->body) {
            substituteInStatement(bodyStmt.get(), substitutions);
        }
    } else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        if (forStmt->iteratorType) {
            substituteTypeRef(forStmt->iteratorType.get(), substitutions);
        }
        if (forStmt->rangeStart) {
            substituteInExpression(forStmt->rangeStart.get(), substitutions);
        }
        if (forStmt->rangeEnd) {
            substituteInExpression(forStmt->rangeEnd.get(), substitutions);
        }
        if (forStmt->condition) {
            substituteInExpression(forStmt->condition.get(), substitutions);
        }
        if (forStmt->increment) {
            substituteInExpression(forStmt->increment.get(), substitutions);
        }
        for (auto& bodyStmt : forStmt->body) {
            substituteInStatement(bodyStmt.get(), substitutions);
        }
    } else if (auto* assignStmt = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        if (assignStmt->target) {
            substituteInExpression(assignStmt->target.get(), substitutions);
        }
        if (assignStmt->value) {
            substituteInExpression(assignStmt->value.get(), substitutions);
        }
    }
}

void TemplateExpander::substituteInExpression(Parser::Expression* expr,
                                              const std::unordered_map<std::string, std::string>& substitutions) {
    if (!expr) return;

    // Handle different expression types
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        if (callExpr->callee) {
            substituteInExpression(callExpr->callee.get(), substitutions);
        }
        for (auto& arg : callExpr->arguments) {
            substituteInExpression(arg.get(), substitutions);
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        if (memberExpr->object) {
            substituteInExpression(memberExpr->object.get(), substitutions);
        }
    } else if (auto* binaryExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        substituteInExpression(binaryExpr->left.get(), substitutions);
        substituteInExpression(binaryExpr->right.get(), substitutions);
    } else if (auto* refExpr = dynamic_cast<Parser::ReferenceExpr*>(expr)) {
        if (refExpr->expr) {
            substituteInExpression(refExpr->expr.get(), substitutions);
        }
    } else if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        if (lambdaExpr->returnType) {
            substituteTypeRef(lambdaExpr->returnType.get(), substitutions);
        }
        for (auto& param : lambdaExpr->parameters) {
            if (param && param->type) {
                substituteTypeRef(param->type.get(), substitutions);
            }
        }
        for (auto& stmt : lambdaExpr->body) {
            substituteInStatement(stmt.get(), substitutions);
        }
    } else if (auto* typeofExpr = dynamic_cast<Parser::TypeOfExpr*>(expr)) {
        // TypeOf expressions have a type reference that may need substitution
        if (typeofExpr->type) {
            substituteTypeRef(typeofExpr->type.get(), substitutions);
        }
    }
}

//==============================================================================
// CONSTRAINT REQUIREMENT VALIDATION HELPERS
//==============================================================================

bool TemplateExpander::validateRequirementParams(
    const std::vector<std::unique_ptr<Parser::RequireStmt>>& requirements,
    const std::set<std::string>& validParams,
    const Common::SourceLocation& constraintLoc) {

    for (const auto& req : requirements) {
        if (!req) continue;

        switch (req->kind) {
            case Parser::RequirementKind::Method:
            case Parser::RequirementKind::CompiletimeMethod:
                // Check that targetParam is a valid parameter binding
                if (!req->targetParam.empty() && validParams.find(req->targetParam) == validParams.end()) {
                    errorReporter_.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Constraint requirement references unknown parameter: " + req->targetParam,
                        req->location
                    );
                    return false;
                }
                break;

            case Parser::RequirementKind::Constructor:
                // Check that targetParam is a valid parameter binding
                if (!req->targetParam.empty() && validParams.find(req->targetParam) == validParams.end()) {
                    errorReporter_.reportError(
                        Common::ErrorCode::UndeclaredIdentifier,
                        "Constructor requirement references unknown parameter: " + req->targetParam,
                        req->location
                    );
                    return false;
                }
                break;

            case Parser::RequirementKind::Truth:
                // For truth requirements, we could validate identifiers in the expression
                // For now, just accept them
                break;
        }
    }

    return true;
}

void TemplateExpander::collectExpressionIdentifiers(Parser::Expression* expr,
                                                    std::set<std::string>& identifiers) {
    if (!expr) return;

    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        identifiers.insert(identExpr->name);
    } else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        collectExpressionIdentifiers(callExpr->callee.get(), identifiers);
        for (auto& arg : callExpr->arguments) {
            collectExpressionIdentifiers(arg.get(), identifiers);
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        collectExpressionIdentifiers(memberExpr->object.get(), identifiers);
    } else if (auto* binaryExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        collectExpressionIdentifiers(binaryExpr->left.get(), identifiers);
        collectExpressionIdentifiers(binaryExpr->right.get(), identifiers);
    }
}

} // namespace Semantic
} // namespace XXML
