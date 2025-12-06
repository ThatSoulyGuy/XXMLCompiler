#include "Backends/Codegen/LambdaTemplateCodegen/LambdaTemplateCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/ModularCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Semantic/SemanticAnalyzer.h"
#include <iostream>
#include <sstream>

namespace XXML::Backends::Codegen {

LambdaTemplateCodegen::LambdaTemplateCodegen(CodegenContext& ctx, ModularCodegen* modularCodegen)
    : ctx_(ctx), modularCodegen_(modularCodegen), cloner_(std::make_unique<ASTCloner>()) {
}

void LambdaTemplateCodegen::reset() {
    generatedInstantiations_.clear();
    lambdaDefinitions_.clear();
    lambdaFunctions_.clear();
    lambdaInfos_.clear();
    lambdaCounter_ = 0;
}

std::string LambdaTemplateCodegen::mangleName(
    const std::string& variableName,
    const std::vector<Parser::TemplateArgument>& args) {

    std::string mangledName = variableName + "_LT_";
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

ASTCloner::TypeMap LambdaTemplateCodegen::buildTypeMap(
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
            if (valueIndex < evaluatedValues.size()) {
                typeMap[param.name] = std::to_string(evaluatedValues[valueIndex]);
                valueIndex++;
            }
        }
    }

    return typeMap;
}

std::string LambdaTemplateCodegen::getLLVMType(const std::string& xxmlType) const {
    // Handle primitive types
    if (xxmlType == "Integer" || xxmlType == "Int" || xxmlType == "i64") return "i64";
    if (xxmlType == "Bool" || xxmlType == "Boolean" || xxmlType == "i1") return "i1";
    if (xxmlType == "Float" || xxmlType == "f32" || xxmlType == "float") return "float";
    if (xxmlType == "Double" || xxmlType == "f64" || xxmlType == "double") return "double";
    if (xxmlType == "None" || xxmlType == "void") return "void";

    // Handle NativeType markers
    if (xxmlType.find("NativeType<") == 0) {
        size_t start = xxmlType.find('<');
        size_t end = xxmlType.find('>');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string inner = xxmlType.substr(start + 1, end - start - 1);
            // Remove quotes if present
            if (!inner.empty() && inner.front() == '"' && inner.back() == '"') {
                inner = inner.substr(1, inner.size() - 2);
            }
            if (inner == "int64") return "i64";
            if (inner == "int32") return "i32";
            if (inner == "ptr") return "ptr";
            return inner;
        }
    }

    // Default to pointer for object types
    return "ptr";
}

std::string LambdaTemplateCodegen::getDefaultReturnValue(const std::string& llvmType) {
    if (llvmType == "void") return "";
    if (llvmType == "ptr") return "null";
    if (llvmType == "i64" || llvmType == "i32" || llvmType == "i16" || llvmType == "i8") return "0";
    if (llvmType == "i1") return "false";
    if (llvmType == "float" || llvmType == "double") return "0.0";
    return "zeroinitializer";
}

std::string LambdaTemplateCodegen::getLambdaFunction(const std::string& key) const {
    auto it = lambdaFunctions_.find(key);
    return (it != lambdaFunctions_.end()) ? it->second : "";
}

const LambdaTemplateCodegen::LambdaInfo* LambdaTemplateCodegen::getLambdaInfo(const std::string& key) const {
    auto it = lambdaInfos_.find(key);
    return (it != lambdaInfos_.end()) ? &it->second : nullptr;
}

bool LambdaTemplateCodegen::isInstantiationGenerated(const std::string& mangledName) const {
    return generatedInstantiations_.count(mangledName) > 0;
}

void LambdaTemplateCodegen::generateLambdaTemplates(Semantic::SemanticAnalyzer& analyzer) {
    const auto& instantiations = analyzer.getLambdaTemplateInstantiations();
    const auto& templateLambdas = analyzer.getTemplateLambdas();

    for (const auto& inst : instantiations) {
        // Find the template lambda by variable name
        auto it = templateLambdas.find(inst.variableName);
        if (it == templateLambdas.end()) {
            continue; // Template lambda not found, skip
        }

        const auto& lambdaInfo = it->second;
        Parser::LambdaExpr* templateLambdaExpr = lambdaInfo.astNode;
        if (!templateLambdaExpr) {
            continue; // AST node not available
        }

        // Generate mangled name
        std::string mangledName = mangleName(inst.variableName, inst.arguments);

        // Skip if already generated
        if (generatedInstantiations_.count(mangledName) > 0) {
            continue;
        }
        generatedInstantiations_.insert(mangledName);

        std::cerr << "[DEBUG] Generating lambda template instantiation: " << mangledName << "\n";

        // Build type substitution map
        ASTCloner::TypeMap typeMap = buildTypeMap(
            lambdaInfo.templateParams, inst.arguments, inst.evaluatedValues);

        // Clone lambda with type substitutions using ASTCloner
        auto clonedLambda = cloner_->cloneLambda(templateLambdaExpr, typeMap);
        if (!clonedLambda) {
            continue;
        }

        // Generate unique function name for this instantiation
        std::string lambdaFuncName = "@lambda.template." + mangledName;

        // Determine return type
        std::string returnType = "ptr";  // Default to ptr for objects
        if (clonedLambda->returnType) {
            returnType = getLLVMType(clonedLambda->returnType->typeName);
        }

        // Build parameter types list (closure ptr + actual params)
        std::vector<std::string> paramTypes;
        paramTypes.push_back("ptr");  // First param is always closure pointer
        for (const auto& param : clonedLambda->parameters) {
            std::string paramType = getLLVMType(param->type->typeName);
            paramTypes.push_back(paramType);
        }

        // Build closure struct type: { ptr (func), ptr (capture0), ptr (capture1), ... }
        std::string closureTypeStr = "{ ptr";
        for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
            closureTypeStr += ", ptr";
        }
        closureTypeStr += " }";

        // Generate lambda function definition
        std::stringstream lambdaDef;
        lambdaDef << "\n; Lambda template function: " << mangledName << "\n";
        lambdaDef << "define " << returnType << " " << lambdaFuncName << "(ptr %closure";
        for (size_t i = 0; i < clonedLambda->parameters.size(); ++i) {
            lambdaDef << ", " << paramTypes[i + 1] << " %" << clonedLambda->parameters[i]->name;
        }
        lambdaDef << ") {\n";
        lambdaDef << "entry:\n";

        // Load captured variables from closure struct
        for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
            const auto& capture = clonedLambda->captures[i];
            std::string captureReg = "%capture." + capture.varName;
            // GEP to get pointer to capture field (index i+1, since index 0 is function ptr)
            lambdaDef << "  " << captureReg << ".ptr = getelementptr inbounds "
                      << closureTypeStr << ", ptr %closure, i32 0, i32 " << (i + 1) << "\n";
            // Load the captured value pointer
            lambdaDef << "  " << captureReg << " = load ptr, ptr " << captureReg << ".ptr\n";
        }

        // Check if body contains a return statement
        bool hasReturn = false;
        for (const auto& stmt : clonedLambda->body) {
            if (dynamic_cast<Parser::ReturnStmt*>(stmt.get())) {
                hasReturn = true;
                break;
            }
        }

        // Generate lambda body statements using modular codegen
        // Note: This generates directly to the LLVMIR module, not to our text output
        // TODO: Full lambda template instantiation needs proper LLVMIR extraction
        if (modularCodegen_) {
            for (const auto& stmt : clonedLambda->body) {
                modularCodegen_->generateStmt(stmt.get());
            }
        }

        // Add default return if body doesn't have one
        if (!hasReturn) {
            std::string defaultVal = getDefaultReturnValue(returnType);
            if (returnType == "void") {
                lambdaDef << "  ret void\n";
            } else {
                lambdaDef << "  ret " << returnType << " " << defaultVal << "\n";
            }
        }
        lambdaDef << "}\n";

        // Store lambda definition for later emission
        lambdaDefinitions_.push_back(lambdaDef.str());

        // Build the lookup key: "identity<Integer>"
        std::string lookupKey = inst.variableName + "<";
        bool firstArg = true;
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                if (!firstArg) lookupKey += ", ";
                firstArg = false;
                lookupKey += arg.typeArg;
            }
        }
        lookupKey += ">";

        // Store function mapping for call site lookup
        lambdaFunctions_[lookupKey] = lambdaFuncName;
        lambdaFunctions_[mangledName] = lambdaFuncName;

        // Store LambdaInfo for parameter types etc.
        LambdaInfo info;
        info.functionName = lambdaFuncName;
        info.returnType = returnType;
        info.paramTypes = paramTypes;
        lambdaInfos_[lookupKey] = info;
        lambdaInfos_[mangledName] = info;
    }
}

} // namespace XXML::Backends::Codegen
