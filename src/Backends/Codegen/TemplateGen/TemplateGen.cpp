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
}

void TemplateGen::generateInstantiation(const std::string& templateName,
                                         const std::vector<Parser::TemplateArgument>& args) {
    std::string mangledName = mangleTemplateName(templateName, args);
    if (!isGenerated(mangledName)) {
        markGenerated(mangledName);
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
