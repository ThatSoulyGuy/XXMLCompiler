#include "Backends/Codegen/TemplateCodegen/TemplateCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Semantic/SemanticAnalyzer.h"
#include <iostream>

namespace XXML::Backends::Codegen {

TemplateCodegen::TemplateCodegen(CodegenContext& ctx, DeclCodegen& declCodegen)
    : ctx_(ctx), declCodegen_(declCodegen), cloner_(std::make_unique<ASTCloner>()) {
}

std::string TemplateCodegen::mangleClassName(
    const std::string& templateName,
    const std::vector<Parser::TemplateArgument>& args,
    const std::vector<int64_t>& evaluatedValues) {

    std::string mangledName = templateName;
    size_t valueIndex = 0;

    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Replace namespace separators with underscores using TypeNormalizer
            std::string cleanType = TypeNormalizer::mangleForLLVM(arg.typeArg);
            mangledName += "_" + cleanType;
        } else {
            // Use evaluated value for non-type parameters
            if (valueIndex < evaluatedValues.size()) {
                mangledName += "_" + std::to_string(evaluatedValues[valueIndex]);
                valueIndex++;
            }
        }
    }

    return mangledName;
}

std::string TemplateCodegen::mangleMethodName(
    const std::string& methodName,
    const std::vector<Parser::TemplateArgument>& args) {

    std::string mangledName = methodName + "_LT_";
    bool first = true;

    for (const auto& arg : args) {
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
    return mangledName;
}

ASTCloner::TypeMap TemplateCodegen::buildTypeMap(
    const std::vector<Parser::TemplateParameter>& params,
    const std::vector<Parser::TemplateArgument>& args,
    const std::vector<int64_t>& evaluatedValues) {

    ASTCloner::TypeMap typeMap;
    size_t valueIndex = 0;

    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        const auto& param = params[i];
        const auto& arg = args[i];

        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                typeMap[param.name] = arg.typeArg;
            }
        } else {
            // Non-type parameter - substitute with the evaluated constant
            if (valueIndex < evaluatedValues.size()) {
                typeMap[param.name] = std::to_string(evaluatedValues[valueIndex]);
                valueIndex++;
            }
        }
    }

    return typeMap;
}

bool TemplateCodegen::hasUnboundTemplateParams(
    const std::vector<Parser::TemplateArgument>& args,
    Semantic::SemanticAnalyzer& analyzer) {

    const auto& templateClasses = analyzer.getTemplateClasses();

    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Check if this type argument matches any template parameter name
            for (const auto& [tplName, tplInfo] : templateClasses) {
                for (const auto& param : tplInfo.templateParams) {
                    if (param.name == arg.typeArg) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool TemplateCodegen::isInstantiationGenerated(const std::string& mangledName) const {
    return generatedClassInstantiations_.count(mangledName) > 0 ||
           generatedMethodInstantiations_.count(mangledName) > 0;
}

void TemplateCodegen::markInstantiationGenerated(const std::string& mangledName) {
    generatedClassInstantiations_.insert(mangledName);
}

void TemplateCodegen::generateClassTemplates(Semantic::SemanticAnalyzer& analyzer) {
    const auto& instantiations = analyzer.getTemplateInstantiations();
    const auto& templateClasses = analyzer.getTemplateClasses();

    for (const auto& inst : instantiations) {
        auto it = templateClasses.find(inst.templateName);
        if (it == templateClasses.end()) {
            continue; // Template class not found, skip
        }

        const auto& templateInfo = it->second;
        Parser::ClassDecl* templateClassDecl = templateInfo.astNode;
        if (!templateClassDecl) {
            continue; // AST node not available
        }

        // Skip if any type argument is itself a template parameter (not a concrete type)
        if (hasUnboundTemplateParams(inst.arguments, analyzer)) {
            std::cerr << "[DEBUG] Skipping template instantiation " << inst.templateName
                      << " due to unbound template parameters\n";
            continue;
        }

        std::cerr << "[DEBUG] Generating template instantiation: " << inst.templateName << "<";
        for (size_t i = 0; i < inst.arguments.size(); ++i) {
            if (i > 0) std::cerr << ", ";
            if (inst.arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                std::cerr << inst.arguments[i].typeArg;
            } else {
                std::cerr << "[value]";
            }
        }
        std::cerr << ">\n";

        // Generate mangled class name
        std::string mangledName = mangleClassName(inst.templateName, inst.arguments, inst.evaluatedValues);

        // Build type substitution map
        ASTCloner::TypeMap typeMap = buildTypeMap(
            templateInfo.templateParams, inst.arguments, inst.evaluatedValues);

        // Clone the template class and substitute types
        auto instantiated = cloner_->cloneClass(templateClassDecl, mangledName, typeMap);

        // Generate code for the instantiated class
        if (instantiated) {
            // Extract namespace from template name
            std::string ns;
            size_t lastSep = inst.templateName.rfind("::");
            if (lastSep != std::string::npos) {
                ns = inst.templateName.substr(0, lastSep);
            }

            // Extract just the class name part from the mangled name
            std::string className = mangledName;
            size_t classLastSep = mangledName.rfind("::");
            if (classLastSep != std::string::npos) {
                className = mangledName.substr(classLastSep + 2);
            }

            // Update the cloned class with just the class name (without namespace)
            instantiated->name = className;

            ctx_.setCurrentNamespace(ns);
            ctx_.setCurrentClassName("");

            declCodegen_.generate(instantiated.get());
        }
    }
}

void TemplateCodegen::generateMethodTemplates(Semantic::SemanticAnalyzer& analyzer) {
    const auto& instantiations = analyzer.getMethodTemplateInstantiations();
    const auto& templateMethods = analyzer.getTemplateMethods();

    for (const auto& inst : instantiations) {
        std::string methodKey = inst.className + "::" + inst.methodName;
        auto it = templateMethods.find(methodKey);
        if (it == templateMethods.end()) {
            continue; // Template method not found, skip
        }

        const auto& methodInfo = it->second;
        Parser::MethodDecl* templateMethodDecl = methodInfo.astNode;
        if (!templateMethodDecl) {
            continue; // AST node not available
        }

        // Generate mangled name
        std::string mangledName = mangleMethodName(inst.methodName, inst.arguments);

        // Skip if already generated
        if (generatedMethodInstantiations_.count(mangledName) > 0) {
            continue;
        }
        generatedMethodInstantiations_.insert(mangledName);

        // Build type substitution map
        ASTCloner::TypeMap typeMap = buildTypeMap(
            methodInfo.templateParams, inst.arguments, inst.evaluatedValues);

        // Clone method and substitute types
        auto clonedMethod = cloner_->cloneMethodWithName(templateMethodDecl, mangledName, typeMap);
        if (clonedMethod) {
            // Determine the class name for context
            std::string classNameForMethod = inst.instantiatedClassName.empty()
                ? inst.className
                : inst.instantiatedClassName;

            // Mangle the class name
            size_t pos;
            while ((pos = classNameForMethod.find('<')) != std::string::npos) {
                classNameForMethod.replace(pos, 1, "_");
            }
            while ((pos = classNameForMethod.find('>')) != std::string::npos) {
                classNameForMethod.erase(pos, 1);
            }
            while ((pos = classNameForMethod.find(',')) != std::string::npos) {
                classNameForMethod.replace(pos, 1, "_");
            }
            while ((pos = classNameForMethod.find(' ')) != std::string::npos) {
                classNameForMethod.erase(pos, 1);
            }

            // Set context and generate
            ctx_.setCurrentClassName(classNameForMethod);
            declCodegen_.generate(clonedMethod.get());
        }
    }
}

} // namespace XXML::Backends::Codegen
