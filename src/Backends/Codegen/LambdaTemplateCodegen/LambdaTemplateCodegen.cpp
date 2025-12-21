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

        // Build parameter types list (closure ptr + actual params) as LLVM types
        auto& llvmCtx = ctx_.module().getContext();
        auto* ptrTy = llvmCtx.getPtrTy();

        // Determine return type as LLVM type
        std::string returnTypeStr = "ptr";  // Default to ptr for objects
        LLVMIR::Type* returnType = ptrTy;
        if (clonedLambda->returnType) {
            returnTypeStr = getLLVMType(clonedLambda->returnType->typeName);
            if (returnTypeStr == "void") {
                returnType = llvmCtx.getVoidTy();
            } else if (returnTypeStr == "i64") {
                returnType = llvmCtx.getInt64Ty();
            } else if (returnTypeStr == "i32") {
                returnType = llvmCtx.getInt32Ty();
            } else if (returnTypeStr == "i1") {
                returnType = llvmCtx.getInt1Ty();
            } else if (returnTypeStr == "float") {
                returnType = llvmCtx.getFloatTy();
            } else if (returnTypeStr == "double") {
                returnType = llvmCtx.getDoubleTy();
            }
        }

        // Build string-based parameter types for LambdaInfo
        std::vector<std::string> paramTypesStr;
        paramTypesStr.push_back("ptr");  // First param is always closure pointer

        // Build LLVM parameter types
        std::vector<LLVMIR::Type*> paramTypes;
        paramTypes.push_back(ptrTy);  // Closure pointer
        for (const auto& param : clonedLambda->parameters) {
            std::string paramTypeStr = getLLVMType(param->type->typeName);
            paramTypesStr.push_back(paramTypeStr);
            if (paramTypeStr == "i64") {
                paramTypes.push_back(llvmCtx.getInt64Ty());
            } else if (paramTypeStr == "i32") {
                paramTypes.push_back(llvmCtx.getInt32Ty());
            } else if (paramTypeStr == "i1") {
                paramTypes.push_back(llvmCtx.getInt1Ty());
            } else if (paramTypeStr == "float") {
                paramTypes.push_back(llvmCtx.getFloatTy());
            } else if (paramTypeStr == "double") {
                paramTypes.push_back(llvmCtx.getDoubleTy());
            } else {
                paramTypes.push_back(ptrTy);
            }
        }

        // Create function type and function in the typed module
        auto* funcType = llvmCtx.getFunctionTy(returnType, paramTypes);
        std::string actualFuncName = "lambda.template." + mangledName;
        auto* func = ctx_.module().createFunction(funcType, actualFuncName);
        if (!func) {
            std::cerr << "[ERROR] Failed to create lambda function: " << actualFuncName << "\n";
            continue;
        }

        // Save current function state and set up for lambda generation
        auto* savedFunc = ctx_.currentFunction();
        auto* savedBlock = ctx_.builder().getInsertBlock();
        std::string savedReturnType(ctx_.currentReturnType());

        ctx_.setCurrentFunction(func);
        ctx_.setCurrentReturnType(returnTypeStr);

        // Create entry block
        auto* entryBlock = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBlock);

        // Set up function arguments - closure pointer is first argument
        func->getArg(0)->setName("closure");
        for (size_t i = 0; i < clonedLambda->parameters.size(); ++i) {
            func->getArg(i + 1)->setName(clonedLambda->parameters[i]->name);
            // Declare parameter in context for body access
            auto argVal = LLVMIR::AnyValue(LLVMIR::PtrValue(func->getArg(i + 1)));
            ctx_.declareParameter(clonedLambda->parameters[i]->name,
                                  clonedLambda->parameters[i]->type->typeName, argVal);
        }

        // Build closure struct type for GEP operations
        std::vector<LLVMIR::Type*> closureFieldTypes;
        closureFieldTypes.push_back(ptrTy);  // Function pointer
        for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
            closureFieldTypes.push_back(ptrTy);  // Captured values are pointers
        }
        auto* closureStructType = llvmCtx.createStructTy("lambda.closure." + mangledName);
        closureStructType->setBody(closureFieldTypes);

        // Load captured variables from closure struct
        auto closurePtr = LLVMIR::AnyValue(LLVMIR::PtrValue(func->getArg(0)));
        for (size_t i = 0; i < clonedLambda->captures.size(); ++i) {
            const auto& capture = clonedLambda->captures[i];
            // GEP to get pointer to capture field (index i+1, since index 0 is function ptr)
            auto gepVal = ctx_.builder().createStructGEP(closureStructType, closurePtr.asPtr(), i + 1,
                                                          "capture." + capture.varName + ".ptr");
            // Load the captured value pointer
            auto loadedVal = ctx_.builder().createLoad(ptrTy, gepVal, "capture." + capture.varName);
            ctx_.declareVariable(capture.varName, "ptr", LLVMIR::AnyValue(loadedVal));
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
        if (modularCodegen_) {
            for (const auto& stmt : clonedLambda->body) {
                modularCodegen_->generateStmt(stmt.get());
            }
        }

        // Add default return if body doesn't have one
        if (!hasReturn) {
            if (returnTypeStr == "void") {
                ctx_.builder().createRetVoid();
            } else if (returnTypeStr == "ptr") {
                auto nullVal = ctx_.builder().getNullPtr();
                ctx_.builder().createRet(LLVMIR::AnyValue(nullVal));
            } else if (returnTypeStr == "i64") {
                auto zeroVal = ctx_.builder().getInt64(0);
                ctx_.builder().createRet(LLVMIR::AnyValue(zeroVal));
            } else if (returnTypeStr == "i32") {
                auto zeroVal = ctx_.builder().getInt32(0);
                ctx_.builder().createRet(LLVMIR::AnyValue(zeroVal));
            } else if (returnTypeStr == "i1") {
                auto falseVal = ctx_.builder().getInt1(false);
                ctx_.builder().createRet(LLVMIR::AnyValue(falseVal));
            } else if (returnTypeStr == "float") {
                auto zeroVal = ctx_.builder().getFloat(0.0f);
                ctx_.builder().createRet(LLVMIR::AnyValue(zeroVal));
            } else if (returnTypeStr == "double") {
                auto zeroVal = ctx_.builder().getDouble(0.0);
                ctx_.builder().createRet(LLVMIR::AnyValue(zeroVal));
            } else {
                auto nullVal = ctx_.builder().getNullPtr();
                ctx_.builder().createRet(LLVMIR::AnyValue(nullVal));
            }
        }

        // Restore previous function state
        if (savedFunc) {
            ctx_.setCurrentFunction(savedFunc);
        }
        if (savedBlock) {
            ctx_.builder().setInsertPoint(savedBlock);
        }
        ctx_.setCurrentReturnType(savedReturnType);

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
        info.returnType = returnTypeStr;
        info.paramTypes = paramTypesStr;
        lambdaInfos_[lookupKey] = info;
        lambdaInfos_[mangledName] = info;
    }
}

} // namespace XXML::Backends::Codegen
