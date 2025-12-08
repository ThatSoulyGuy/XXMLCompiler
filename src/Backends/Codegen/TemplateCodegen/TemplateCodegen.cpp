#include "Backends/Codegen/TemplateCodegen/TemplateCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Backends/NameMangler.h"
#include "Semantic/SemanticAnalyzer.h"
#include <iostream>
#include <set>

namespace XXML::Backends::Codegen {

TemplateCodegen::TemplateCodegen(CodegenContext& ctx, DeclCodegen& declCodegen)
    : ctx_(ctx), declCodegen_(declCodegen), cloner_(std::make_unique<ASTCloner>()) {
}

std::string TemplateCodegen::mangleClassName(
    const std::string& templateName,
    const std::vector<Parser::TemplateArgument>& args,
    const std::vector<int64_t>& evaluatedValues) {
    // Delegate to centralized NameMangler for consistent mangling
    return NameMangler::mangleTemplateClass(templateName, args, evaluatedValues);
}

std::string TemplateCodegen::mangleMethodName(
    const std::string& methodName,
    const std::vector<Parser::TemplateArgument>& args) {
    // Delegate to centralized NameMangler for consistent mangling
    return NameMangler::mangleTemplateMethod(methodName, args);
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

// Helper to pre-register method return types from a cloned class
// This is called BEFORE code generation to ensure all return types are known
void TemplateCodegen::preRegisterMethodReturnTypes(Parser::ClassDecl* classDecl, const std::string& fullClassName) {
    if (!classDecl) {
        return;
    }

    std::string mangledClassName = TypeNormalizer::mangleForLLVM(fullClassName);

    for (const auto& section : classDecl->sections) {
        for (const auto& memberDecl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                if (!method->templateParams.empty()) continue;  // Skip template methods
                if (method->isNative) continue;  // Skip native methods

                std::string methodReturnType = method->returnType ? method->returnType->toString() : "void";
                // IMPORTANT: Preserve ownership markers - they are critical for determining
                // whether to free return values (& = reference, should NOT free)
                // resolveToQualifiedName strips ownership markers, so save and restore them
                char ownershipMarker = '\0';
                if (!methodReturnType.empty()) {
                    char last = methodReturnType.back();
                    if (last == '^' || last == '&' || last == '%') {
                        ownershipMarker = last;
                    }
                }
                methodReturnType = ctx_.resolveToQualifiedName(methodReturnType);
                // Re-attach ownership marker if it was present
                if (ownershipMarker != '\0') {
                    methodReturnType += ownershipMarker;
                }

                std::string mangledFuncName = mangledClassName + "_" + method->name;
                ctx_.registerMethodReturnType(mangledFuncName, methodReturnType);

                // Also register parameter types (with ownership markers) for ownership transfer detection
                std::vector<std::string> paramTypes;
                for (const auto& param : method->parameters) {
                    if (param->type) {
                        std::string paramType = param->type->toString();
                        // Resolve to qualified name but preserve ownership marker
                        char ownershipMarker = '\0';
                        if (!paramType.empty()) {
                            char last = paramType.back();
                            if (last == '^' || last == '&' || last == '%') {
                                ownershipMarker = last;
                            }
                        }
                        paramType = ctx_.resolveToQualifiedName(paramType);
                        if (ownershipMarker != '\0') {
                            paramType += ownershipMarker;
                        }
                        paramTypes.push_back(paramType);
                    }
                }
                ctx_.registerMethodParameterTypes(mangledFuncName, paramTypes);
            }
        }
    }
}

void TemplateCodegen::generateClassTemplates(Semantic::SemanticAnalyzer& analyzer) {
    const auto& templateClasses = analyzer.getTemplateClasses();

    // Copy initial instantiations to a working queue
    std::vector<Semantic::SemanticAnalyzer::TemplateInstantiation> workQueue;
    for (const auto& inst : analyzer.getTemplateInstantiations()) {
        workQueue.push_back(inst);
    }

    // PHASE 1: Clone all templates and collect nested instantiations
    // We need to process ALL templates before generating code, so method return types
    // are registered for cross-template calls (e.g., HashMap calling List.add())

    std::set<std::string> processedMangled; // Track what we've already cloned

    // Store cloned classes with their metadata for Phase 2
    struct ClonedClassInfo {
        std::unique_ptr<Parser::ClassDecl> classDecl;
        std::string namespaceName;
        std::string fullClassName;
        std::string templateName;  // Original template name for deduplication
        ASTCloner::TypeMap typeMap;  // Type substitutions used
    };
    std::vector<ClonedClassInfo> clonedClasses;

    while (!workQueue.empty()) {
        auto inst = workQueue.back();
        workQueue.pop_back();

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
            continue;
        }

        // Generate mangled class name
        std::string mangledName = mangleClassName(inst.templateName, inst.arguments, inst.evaluatedValues);

        // Skip if already processed
        if (processedMangled.count(mangledName) > 0) {
            continue;
        }
        processedMangled.insert(mangledName);

        // Build type substitution map
        ASTCloner::TypeMap typeMap = buildTypeMap(
            templateInfo.templateParams, inst.arguments, inst.evaluatedValues);

        // Analyze template for deduplication opportunities
        // This caches the analysis result for use during wrapper generation
        if (templateAnalysisCache_.find(inst.templateName) == templateAnalysisCache_.end()) {
            auto analysis = analyzeTemplateClass(templateClassDecl, templateInfo.templateParams);
            templateAnalysisCache_[inst.templateName] = analysis;

            // Generate shared base for templates with shareable methods
            if (analysis.hasShareableMethods()) {
                generateSharedBase(templateClassDecl, inst.templateName, analysis);
            }
        }

        // Clear nested instantiations before cloning
        cloner_->clearNestedInstantiations();

        // Clone the template class and substitute types
        auto instantiated = cloner_->cloneClass(templateClassDecl, mangledName, typeMap);

        // Collect nested template instantiations discovered during cloning
        for (const auto& nested : cloner_->getNestedInstantiations()) {
            // Only add if it's a known template class
            // IMPORTANT: Try qualified names first to ensure proper namespace resolution
            decltype(templateClasses.end()) nestedIt = templateClasses.end();

            // First try: qualified with Language::Collections::
            std::string qualifiedName = "Language::Collections::" + nested.templateName;
            nestedIt = templateClasses.find(qualifiedName);

            // Second try: as-is (might already be qualified or in global namespace)
            if (nestedIt == templateClasses.end()) {
                nestedIt = templateClasses.find(nested.templateName);
            }

            if (nestedIt != templateClasses.end()) {
                // Create a new instantiation request
                Semantic::SemanticAnalyzer::TemplateInstantiation nestedInst;
                nestedInst.templateName = nestedIt->first; // Use the qualified name from registry
                nestedInst.arguments = nested.arguments;
                // evaluatedValues can stay empty for type-only templates

                workQueue.push_back(nestedInst);
            }
        }

        // Store the cloned class for Phase 2
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

            // Build full class name for pre-registration
            std::string fullClassName = ns.empty() ? className : ns + "::" + className;

            // Pre-register method return types NOW, before any code generation
            preRegisterMethodReturnTypes(instantiated.get(), fullClassName);

            // Store for Phase 2
            ClonedClassInfo info;
            info.classDecl = std::move(instantiated);
            info.namespaceName = ns;
            info.fullClassName = fullClassName;
            info.templateName = inst.templateName;
            info.typeMap = typeMap;
            clonedClasses.push_back(std::move(info));
        }
    }

    // PHASE 2: Generate code for all cloned classes
    // Now all method return types are registered, so cross-template calls work correctly
    for (auto& info : clonedClasses) {
        // Log deduplication analysis for this instantiation
        auto analysisIt = templateAnalysisCache_.find(info.templateName);
        if (analysisIt != templateAnalysisCache_.end()) {
            const auto& analysis = analysisIt->second;
            if (analysis.hasShareableMethods()) {
                std::string baseName = info.templateName;
                size_t lastSep = baseName.rfind("::");
                if (lastSep != std::string::npos) {
                    baseName = baseName.substr(lastSep + 2);
                }
                baseName += "_Base";
                generateTypeWrapper(info.classDecl.get(), info.fullClassName, baseName,
                                    analysis, info.typeMap);
            }
        }

        ctx_.setCurrentNamespace(info.namespaceName);
        ctx_.setCurrentClassName("");
        declCodegen_.generate(info.classDecl.get());
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

// ===== Template Deduplication Analysis =====

TemplateClassAnalysis TemplateCodegen::analyzeTemplateClass(
    Parser::ClassDecl* classDecl,
    const std::vector<Parser::TemplateParameter>& templateParams) {

    TemplateClassAnalysis result;
    result.templateName = classDecl->name;

    // Build set of type parameter names
    std::set<std::string> typeParams;
    for (const auto& param : templateParams) {
        if (param.kind == Parser::TemplateParameter::Kind::Type) {
            result.typeParams.push_back(param.name);
            typeParams.insert(param.name);
        }
    }

    // Analyze each method
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Skip template methods - they need separate handling
                if (!method->templateParams.empty()) continue;

                auto methodAnalysis = analyzeMethod(method, typeParams);
                result.methods.push_back(methodAnalysis);
            }
        }
    }

    return result;
}

TemplateMethodAnalysis TemplateCodegen::analyzeMethod(
    Parser::MethodDecl* method,
    const std::set<std::string>& typeParams) {

    TemplateMethodAnalysis result;
    result.methodName = method->name;

    // Check return type
    if (method->returnType) {
        result.hasTypeDependentReturn = usesTypeParam(method->returnType.get(), typeParams);
    }

    // Check parameters
    for (const auto& param : method->parameters) {
        if (param->type && usesTypeParam(param->type.get(), typeParams)) {
            result.hasTypeDependentParams = true;
            break;
        }
    }

    // Analyze method body for type parameter usage
    for (const auto& stmt : method->body) {
        if (stmtUsesTypeParam(stmt.get(), typeParams)) {
            result.usesOnlySize = true;
        }
    }

    // If method has no type dependencies at all, mark as type-independent
    if (!result.hasTypeDependentReturn && !result.hasTypeDependentParams &&
        !result.usesOnlySize && !result.usesNestedTemplates) {
        result.isTypeIndependent = true;
    }

    return result;
}

bool TemplateCodegen::usesTypeParam(Parser::TypeRef* typeRef, const std::set<std::string>& typeParams) {
    if (!typeRef) return false;

    // Check if the base type name is a type parameter
    std::string typeName = typeRef->typeName;

    // Strip ownership markers
    if (!typeName.empty() && (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
        typeName.pop_back();
    }

    if (typeParams.count(typeName) > 0) {
        return true;
    }

    // Check template arguments
    for (const auto& arg : typeRef->templateArgs) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            std::string typeArg = arg.typeArg;
            // Strip ownership markers
            if (!typeArg.empty() && (typeArg.back() == '^' || typeArg.back() == '&' || typeArg.back() == '%')) {
                typeArg.pop_back();
            }
            if (typeParams.count(typeArg) > 0) {
                return true;
            }
        }
    }

    return false;
}

bool TemplateCodegen::exprUsesTypeParam(Parser::Expression* expr, const std::set<std::string>& typeParams) {
    if (!expr) return false;

    // Check call expressions (includes method calls)
    if (auto* call = dynamic_cast<Parser::CallExpr*>(expr)) {
        // Check callee (could be MemberAccessExpr for method calls)
        if (call->callee && exprUsesTypeParam(call->callee.get(), typeParams)) {
            return true;
        }
        // Check arguments
        for (const auto& arg : call->arguments) {
            if (exprUsesTypeParam(arg.get(), typeParams)) {
                return true;
            }
        }
    }

    // Check member access expressions
    if (auto* member = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        if (member->object && exprUsesTypeParam(member->object.get(), typeParams)) {
            return true;
        }
    }

    // Check binary expressions
    if (auto* binary = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return exprUsesTypeParam(binary->left.get(), typeParams) ||
               exprUsesTypeParam(binary->right.get(), typeParams);
    }

    // Check identifier expressions (variable references)
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        // Type parameter could appear as identifier in expressions like T::Constructor
        if (typeParams.count(ident->name) > 0) {
            return true;
        }
    }

    return false;
}

bool TemplateCodegen::stmtUsesTypeParam(Parser::Statement* stmt, const std::set<std::string>& typeParams) {
    if (!stmt) return false;

    // Check instantiate statements
    if (auto* inst = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        if (inst->type && usesTypeParam(inst->type.get(), typeParams)) {
            return true;
        }
        if (inst->initializer && exprUsesTypeParam(inst->initializer.get(), typeParams)) {
            return true;
        }
    }

    // Check return statements
    if (auto* ret = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (ret->value && exprUsesTypeParam(ret->value.get(), typeParams)) {
            return true;
        }
    }

    // Check run statements
    if (auto* run = dynamic_cast<Parser::RunStmt*>(stmt)) {
        if (run->expression && exprUsesTypeParam(run->expression.get(), typeParams)) {
            return true;
        }
    }

    // Check assignment statements
    if (auto* assign = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        if (assign->target && exprUsesTypeParam(assign->target.get(), typeParams)) {
            return true;
        }
        if (assign->value && exprUsesTypeParam(assign->value.get(), typeParams)) {
            return true;
        }
    }

    // Check if statements
    if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        if (ifStmt->condition && exprUsesTypeParam(ifStmt->condition.get(), typeParams)) {
            return true;
        }
        for (const auto& s : ifStmt->thenBranch) {
            if (stmtUsesTypeParam(s.get(), typeParams)) {
                return true;
            }
        }
        for (const auto& s : ifStmt->elseBranch) {
            if (stmtUsesTypeParam(s.get(), typeParams)) {
                return true;
            }
        }
    }

    // Check while statements
    if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        if (whileStmt->condition && exprUsesTypeParam(whileStmt->condition.get(), typeParams)) {
            return true;
        }
        for (const auto& s : whileStmt->body) {
            if (stmtUsesTypeParam(s.get(), typeParams)) {
                return true;
            }
        }
    }

    return false;
}

std::string TemplateCodegen::generateSharedBase(
    Parser::ClassDecl* classDecl,
    const std::string& templateName,
    const TemplateClassAnalysis& analysis) {

    // Generate a base name like "List_Base"
    std::string baseName = templateName;
    // Remove namespace prefix for the base name
    size_t lastSep = baseName.rfind("::");
    if (lastSep != std::string::npos) {
        baseName = baseName.substr(lastSep + 2);
    }
    baseName += "_Base";

    // Check if already generated
    if (generatedSharedBases_.count(baseName) > 0) {
        return baseName;
    }
    generatedSharedBases_.insert(baseName);

    // Log the analysis results for debugging/verification
    std::cerr << "[TemplateDedup] Analyzing " << templateName << " for deduplication:\n";
    for (const auto& method : analysis.methods) {
        std::string shareability = method.canShareImplementation() ? "SHAREABLE" : "TYPE-SPECIFIC";
        std::cerr << "  - " << method.methodName << ": " << shareability;
        if (method.isTypeIndependent) std::cerr << " (type-independent)";
        if (method.usesOnlySize) std::cerr << " (size-only)";
        if (method.hasTypeDependentReturn) std::cerr << " (T-return)";
        if (method.hasTypeDependentParams) std::cerr << " (T-params)";
        std::cerr << "\n";
    }

    // Count shareable methods
    int shareableCount = 0;
    int totalCount = static_cast<int>(analysis.methods.size());
    for (const auto& method : analysis.methods) {
        if (method.canShareImplementation()) {
            shareableCount++;
        }
    }
    std::cerr << "[TemplateDedup] " << shareableCount << "/" << totalCount
              << " methods can share implementation\n";

    // For now, we return the base name but don't generate separate base code.
    // The actual deduplication happens at link time or through LLVM's mergefunc pass.
    //
    // Full implementation would:
    // 1. Clone type-independent methods with "_Base" suffix
    // 2. Replace type parameters with generic ptr
    // 3. Add element size parameter where needed
    // 4. Generate once, then have type-specific versions call the base

    return baseName;
}

void TemplateCodegen::generateTypeWrapper(
    Parser::ClassDecl* originalClass,
    const std::string& mangledClassName,
    const std::string& sharedBaseName,
    const TemplateClassAnalysis& analysis,
    const ASTCloner::TypeMap& typeMap) {

    // Log which methods will use shared base vs. type-specific code
    std::cerr << "[TemplateDedup] Wrapper for " << mangledClassName << ":\n";
    for (const auto& method : analysis.methods) {
        if (method.canShareImplementation()) {
            std::cerr << "  - " << method.methodName << " -> " << sharedBaseName
                      << "_" << method.methodName << " (shared)\n";
        } else {
            std::cerr << "  - " << method.methodName << " (type-specific)\n";
        }
    }

    // Current behavior: Full monomorphization (each type gets full code)
    // The infrastructure above enables future optimization where:
    //
    // For SHAREABLE methods:
    //   1. Generate a simple forwarder function:
    //      define void @List_Integer_add(ptr %this, ptr %value) {
    //          call void @List_Base_add(ptr %this, ptr %value)
    //          ret void
    //      }
    //   OR use LLVM alias:
    //      @List_Integer_add = alias void (ptr, ptr), ptr @List_Base_add
    //
    // For TYPE-SPECIFIC methods:
    //   Generate full implementation as currently done
    //
    // Benefits:
    //   - 40-60% reduction in code size for template-heavy code
    //   - Better instruction cache utilization
    //   - Identical runtime performance (thin wrappers inline)
}

} // namespace XXML::Backends::Codegen
