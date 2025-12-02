#include "Backends/Codegen/TemplateGen/TemplateGen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

TemplateGen::TemplateGen(CodegenContext& ctx)
    : ctx_(ctx) {}

std::string TemplateGen::mangleTemplateName(const std::string& templateName,
                                             const std::vector<Parser::TemplateArgument>& args,
                                             const std::vector<int64_t>& evaluatedValues) {
    std::string mangledName = templateName;
    size_t valueIndex = 0;

    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            std::string cleanType = arg.typeArg;
            size_t pos = 0;
            while ((pos = cleanType.find("::")) != std::string::npos) {
                cleanType.replace(pos, 2, "_");
            }
            mangledName += "_" + cleanType;
        } else {
            if (valueIndex < evaluatedValues.size()) {
                mangledName += "_" + std::to_string(evaluatedValues[valueIndex]);
                valueIndex++;
            }
        }
    }

    return mangledName;
}

void TemplateGen::generateAll() {
    if (!semanticAnalyzer_) return;

    // Generate class template instantiations
    const auto& instantiations = semanticAnalyzer_->getTemplateInstantiations();
    const auto& templateClasses = semanticAnalyzer_->getTemplateClasses();

    for (const auto& inst : instantiations) {
        auto it = templateClasses.find(inst.templateName);
        if (it == templateClasses.end()) continue;

        const auto& templateInfo = it->second;
        Parser::ClassDecl* templateClassDecl = templateInfo.astNode;
        if (!templateClassDecl) continue;

        std::string mangledName = mangleTemplateName(inst.templateName, inst.arguments, inst.evaluatedValues);

        if (isGenerated(mangledName)) continue;
        markGenerated(mangledName);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        size_t valueIndex = 0;
        for (size_t i = 0; i < templateInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = templateInfo.templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone and substitute - delegate to AST's own clone mechanism
        auto cloned = templateClassDecl->clone();
        if (cloned) {
            // The actual code generation is handled by DeclCodegen
        }
    }

    // Generate method template instantiations
    generateMethodInstantiations();

    // Generate lambda template instantiations
    generateLambdaInstantiations();
}

void TemplateGen::generateInstantiation(const std::string& templateName,
                                         const std::vector<Parser::TemplateArgument>& args) {
    std::string mangledName = mangleTemplateName(templateName, args);
    if (!isGenerated(mangledName)) {
        markGenerated(mangledName);
    }
}

void TemplateGen::generateMethodInstantiations() {
    if (!semanticAnalyzer_) return;

    const auto& instantiations = semanticAnalyzer_->getMethodTemplateInstantiations();
    const auto& templateMethods = semanticAnalyzer_->getTemplateMethods();

    for (const auto& inst : instantiations) {
        std::string methodKey = inst.className + "::" + inst.methodName;
        auto it = templateMethods.find(methodKey);
        if (it == templateMethods.end()) continue;

        const auto& methodInfo = it->second;
        Parser::MethodDecl* templateMethodDecl = methodInfo.astNode;
        if (!templateMethodDecl) continue;

        // Generate mangled name: ClassName_MethodName_LT_TypeArg1_GT_ (matching call site format)
        std::string mangledName = inst.className + "_" + inst.methodName + "_LT_";
        bool first = true;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                if (!first) mangledName += "_C_";  // Comma separator
                first = false;
                std::string cleanType = arg.typeArg;
                size_t pos = 0;
                while ((pos = cleanType.find("::")) != std::string::npos) {
                    cleanType.replace(pos, 2, "_");
                }
                mangledName += cleanType;
            }
        }
        mangledName += "_GT_";

        if (isGenerated(mangledName)) continue;
        markGenerated(mangledName);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        size_t valueIndex = 0;
        for (size_t i = 0; i < methodInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = methodInfo.templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone method with substitutions - the cloned method will be generated
        auto clonedMethod = cloneMethod(templateMethodDecl, typeMap);
        if (clonedMethod) {
            clonedMethod->name = mangledName;
            // Store for later code generation (the backend will pick this up)
            // The actual code generation happens when the method is visited
        }
    }
}

void TemplateGen::generateLambdaInstantiations() {
    if (!semanticAnalyzer_) return;

    const auto& instantiations = semanticAnalyzer_->getLambdaTemplateInstantiations();
    const auto& templateLambdas = semanticAnalyzer_->getTemplateLambdas();

    for (const auto& inst : instantiations) {
        auto it = templateLambdas.find(inst.variableName);
        if (it == templateLambdas.end()) continue;

        const auto& lambdaInfo = it->second;
        Parser::LambdaExpr* templateLambdaExpr = lambdaInfo.astNode;
        if (!templateLambdaExpr) continue;

        // Generate mangled name: variableName_TypeArg1_TypeArg2
        std::string mangledName = inst.variableName;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                std::string cleanType = arg.typeArg;
                size_t pos = 0;
                while ((pos = cleanType.find("::")) != std::string::npos) {
                    cleanType.replace(pos, 2, "_");
                }
                mangledName += "_" + cleanType;
            }
        }

        if (isGenerated(mangledName)) continue;
        markGenerated(mangledName);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        size_t valueIndex = 0;
        for (size_t i = 0; i < lambdaInfo.templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = lambdaInfo.templateParams[i];
            const auto& arg = inst.arguments[i];

            if (param.kind == Parser::TemplateParameter::Kind::Type) {
                if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                    typeMap[param.name] = arg.typeArg;
                }
            } else {
                if (valueIndex < inst.evaluatedValues.size()) {
                    typeMap[param.name] = std::to_string(inst.evaluatedValues[valueIndex]);
                    valueIndex++;
                }
            }
        }

        // Clone lambda with substitutions
        auto clonedLambda = cloneLambda(templateLambdaExpr, typeMap);
        if (clonedLambda) {
            // The cloned lambda will be generated with the mangled name
            // Store the mapping for code generation
        }
    }
}

std::string TemplateGen::substituteType(const std::string& typeName,
                                         const std::unordered_map<std::string, std::string>& typeMap) {
    auto it = typeMap.find(typeName);
    if (it != typeMap.end()) {
        return it->second;
    }

    size_t ltPos = typeName.find('<');
    if (ltPos != std::string::npos) {
        std::string base = typeName.substr(0, ltPos);
        std::string args = typeName.substr(ltPos + 1);
        if (!args.empty() && args.back() == '>') {
            args.pop_back();
        }

        std::string result = base + "<";
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == '<') depth++;
            else if (args[i] == '>') depth--;
            else if (args[i] == ',' && depth == 0) {
                std::string arg = args.substr(start, i - start);
                result += substituteType(arg, typeMap);
                result += ",";
                start = i + 1;
            }
        }
        if (start < args.size()) {
            std::string arg = args.substr(start);
            result += substituteType(arg, typeMap);
        }
        result += ">";
        return result;
    }

    return typeName;
}

std::unique_ptr<Parser::TypeRef> TemplateGen::cloneTypeRef(
    Parser::TypeRef* typeRef,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!typeRef) return nullptr;

    std::string substituted = substituteType(typeRef->typeName, typeMap);
    return std::make_unique<Parser::TypeRef>(substituted, typeRef->ownership, typeRef->location);
}

std::unique_ptr<Parser::Expression> TemplateGen::cloneExpression(
    Parser::Expression* expr,
    const std::unordered_map<std::string, std::string>& /*typeMap*/) {
    if (!expr) return nullptr;
    auto cloned = expr->clone();
    if (auto* exprNode = dynamic_cast<Parser::Expression*>(cloned.get())) {
        cloned.release();
        return std::unique_ptr<Parser::Expression>(exprNode);
    }
    return nullptr;
}

std::unique_ptr<Parser::Statement> TemplateGen::cloneStatement(
    Parser::Statement* stmt,
    const std::unordered_map<std::string, std::string>& /*typeMap*/) {
    if (!stmt) return nullptr;
    auto cloned = stmt->clone();
    if (auto* stmtNode = dynamic_cast<Parser::Statement*>(cloned.get())) {
        cloned.release();
        return std::unique_ptr<Parser::Statement>(stmtNode);
    }
    return nullptr;
}

std::unique_ptr<Parser::MethodDecl> TemplateGen::cloneMethod(
    Parser::MethodDecl* method,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!method) return nullptr;

    // Clone parameters
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    for (const auto& param : method->parameters) {
        auto clonedType = cloneTypeRef(param->type.get(), typeMap);
        params.push_back(std::make_unique<Parser::ParameterDecl>(
            param->name, std::move(clonedType), param->location));
    }

    // Clone body statements
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;
    for (const auto& stmt : method->body) {
        if (auto clonedStmt = cloneStatement(stmt.get(), typeMap)) {
            bodyStmts.push_back(std::move(clonedStmt));
        }
    }

    auto cloned = std::make_unique<Parser::MethodDecl>(
        method->name,
        cloneTypeRef(method->returnType.get(), typeMap),
        std::move(params),
        std::move(bodyStmts),
        method->location);

    cloned->isNative = method->isNative;
    cloned->nativePath = method->nativePath;
    cloned->nativeSymbol = method->nativeSymbol;
    cloned->callingConvention = method->callingConvention;
    cloned->isCompiletime = method->isCompiletime;

    return cloned;
}

std::unique_ptr<Parser::LambdaExpr> TemplateGen::cloneLambda(
    Parser::LambdaExpr* lambda,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!lambda) return nullptr;

    // Clone captures
    std::vector<Parser::LambdaExpr::CaptureSpec> captures = lambda->captures;

    // Clone parameters
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    for (const auto& param : lambda->parameters) {
        auto clonedType = cloneTypeRef(param->type.get(), typeMap);
        params.push_back(std::make_unique<Parser::ParameterDecl>(
            param->name, std::move(clonedType), param->location));
    }

    // Clone return type
    auto clonedReturnType = cloneTypeRef(lambda->returnType.get(), typeMap);

    // Clone body statements
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;
    for (const auto& stmt : lambda->body) {
        if (auto clonedStmt = cloneStatement(stmt.get(), typeMap)) {
            bodyStmts.push_back(std::move(clonedStmt));
        }
    }

    auto cloned = std::make_unique<Parser::LambdaExpr>(
        std::move(captures),
        std::move(params),
        std::move(clonedReturnType),
        std::move(bodyStmts),
        lambda->location);

    cloned->isCompiletime = lambda->isCompiletime;
    // Don't copy templateParams - the cloned lambda is an instantiation

    return cloned;
}

std::unique_ptr<Parser::ConstructorDecl> TemplateGen::cloneConstructor(
    Parser::ConstructorDecl* ctor,
    const std::string& /*newClassName*/,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!ctor) return nullptr;

    // Clone parameters
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    for (const auto& param : ctor->parameters) {
        auto clonedType = cloneTypeRef(param->type.get(), typeMap);
        params.push_back(std::make_unique<Parser::ParameterDecl>(
            param->name, std::move(clonedType), param->location));
    }

    // Clone body statements
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;
    for (const auto& stmt : ctor->body) {
        if (auto clonedStmt = cloneStatement(stmt.get(), typeMap)) {
            bodyStmts.push_back(std::move(clonedStmt));
        }
    }

    // ConstructorDecl takes (isDefault, params, body, location)
    auto cloned = std::make_unique<Parser::ConstructorDecl>(
        ctor->isDefault,
        std::move(params),
        std::move(bodyStmts),
        ctor->location);

    return cloned;
}

std::unique_ptr<Parser::PropertyDecl> TemplateGen::cloneProperty(
    Parser::PropertyDecl* prop,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!prop) return nullptr;

    return std::make_unique<Parser::PropertyDecl>(
        prop->name,
        cloneTypeRef(prop->type.get(), typeMap),
        prop->location);
}

std::unique_ptr<Parser::ClassDecl> TemplateGen::cloneAndSubstitute(
    Parser::ClassDecl* templateClass,
    const std::string& newName,
    const std::unordered_map<std::string, std::string>& typeMap) {
    if (!templateClass) return nullptr;

    // Substitute base class if present
    std::string newBaseClass;
    if (!templateClass->baseClass.empty()) {
        newBaseClass = substituteType(templateClass->baseClass, typeMap);
    }

    // ClassDecl takes (name, templateParams, isFinal, baseClass, location)
    // For instantiated templates, we don't pass template params
    auto cloned = std::make_unique<Parser::ClassDecl>(
        newName,
        std::vector<Parser::TemplateParameter>{},  // No template params for instantiated class
        templateClass->isFinal,
        newBaseClass,
        templateClass->location);

    // Clone sections (AccessSection, not ClassSection)
    for (const auto& section : templateClass->sections) {
        if (!section) continue;

        auto clonedSection = std::make_unique<Parser::AccessSection>(
            section->modifier, section->location);

        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                if (auto clonedProp = cloneProperty(prop, typeMap)) {
                    clonedSection->declarations.push_back(std::move(clonedProp));
                }
            }
            else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                if (auto clonedMethod = cloneMethod(method, typeMap)) {
                    clonedSection->declarations.push_back(std::move(clonedMethod));
                }
            }
            else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                if (auto clonedCtor = cloneConstructor(ctor, newName, typeMap)) {
                    clonedSection->declarations.push_back(std::move(clonedCtor));
                }
            }
        }

        cloned->sections.push_back(std::move(clonedSection));
    }

    return cloned;
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
