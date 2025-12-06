#include "Backends/Codegen/ModularCodegen.h"
#include "Backends/LLVMIR/Emitter.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include "Backends/TypeNormalizer.h"
#include <unordered_set>
#include <functional>

namespace XXML {
namespace Backends {
namespace Codegen {

ModularCodegen::ModularCodegen(Core::CompilationContext* compCtx)
    : ctx_(compCtx) {
    initializeCodegens();
}

ModularCodegen::~ModularCodegen() = default;

void ModularCodegen::initializeCodegens() {
    // Create the codegen modules (order matters - some depend on others)
    exprCodegen_ = std::make_unique<ExprCodegen>(ctx_);
    stmtCodegen_ = std::make_unique<StmtCodegen>(ctx_, *exprCodegen_);
    declCodegen_ = std::make_unique<DeclCodegen>(ctx_, *exprCodegen_, *stmtCodegen_);
    reflectionCodegen_ = std::make_unique<ReflectionCodegen>(ctx_);
    annotationCodegen_ = std::make_unique<AnnotationCodegen>(ctx_);

    // New modules
    templateCodegen_ = std::make_unique<TemplateCodegen>(ctx_, *declCodegen_);
    moduleCodegen_ = std::make_unique<ModuleCodegen>(ctx_, *declCodegen_);
    nativeCodegen_ = std::make_unique<NativeCodegen>(ctx_, ctx_.compilationContext());
    // Note: lambdaTemplateCodegen_ needs 'this' pointer, so we initialize it after construction
    lambdaTemplateCodegen_ = std::make_unique<LambdaTemplateCodegen>(ctx_, this);
}

LLVMIR::AnyValue ModularCodegen::generateExpr(Parser::Expression* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(builder().getNullPtr());
    }
    return exprCodegen_->generate(expr);
}

void ModularCodegen::generateStmt(Parser::Statement* stmt) {
    if (stmt) {
        stmtCodegen_->generate(stmt);
    }
}

void ModularCodegen::generateDecl(Parser::ASTNode* decl) {
    if (decl) {
        declCodegen_->generate(decl);
    }
}

void ModularCodegen::generateProgram(const std::vector<std::unique_ptr<Parser::ASTNode>>& nodes) {
    for (const auto& node : nodes) {
        // Handle top-level declarations
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(node.get())) {
            generateDecl(classDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(node.get())) {
            generateDecl(nsDecl);
        } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(node.get())) {
            generateDecl(entryDecl);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(node.get())) {
            generateDecl(enumDecl);
        } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(node.get())) {
            generateDecl(nativeDecl);
        }
    }
}

void ModularCodegen::generateProgramDecls(const std::vector<std::unique_ptr<Parser::Declaration>>& decls) {
    for (const auto& decl : decls) {
        // Handle top-level declarations
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            generateDecl(classDecl);
        } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            generateDecl(nsDecl);
        } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(decl.get())) {
            generateDecl(entryDecl);
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            generateDecl(enumDecl);
        } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            generateDecl(nativeDecl);
        } else if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(decl.get())) {
            generateDecl(annotDecl);
        }
    }
}

std::string ModularCodegen::getIR() const {
    LLVMIR::LLVMEmitter emitter(ctx_.module());
    return emitter.emit();
}

std::string ModularCodegen::getFunctionsIR() const {
    LLVMIR::LLVMEmitter emitter(ctx_.module());
    return emitter.emitFunctionsOnly();
}

void ModularCodegen::generateMetadata() {
    // Generate reflection metadata
    if (reflectionCodegen_) {
        reflectionCodegen_->generate();
    }
    // Generate annotation metadata
    if (annotationCodegen_) {
        annotationCodegen_->generate();
    }
}

std::string ModularCodegen::getMetadataIR() const {
    std::string result;
    if (reflectionCodegen_) {
        result += reflectionCodegen_->getIR();
    }
    if (annotationCodegen_) {
        result += annotationCodegen_->getIR();
    }
    return result;
}

void ModularCodegen::generateTemplates(Semantic::SemanticAnalyzer& analyzer) {
    if (templateCodegen_) {
        templateCodegen_->generateClassTemplates(analyzer);
        templateCodegen_->generateMethodTemplates(analyzer);
    }

    // Verify that all Deferred types have been resolved after template instantiation
    // This is a debug check to ensure templates are properly instantiated
    #ifndef NDEBUG
    auto stats = ctx_.getTypeVerificationStats();
    if (stats.deferredTypes > 0 || stats.unknownTypes > 0) {
        std::cerr << "[DEBUG] After template generation: "
                  << stats.deferredTypes << " Deferred types, "
                  << stats.unknownTypes << " Unknown types still present\n";
        // Note: Some Deferred types may be resolved later during actual codegen
        // Full verification happens at the end of compilation
    }
    #endif
}

void ModularCodegen::collectReflectionMetadata(Parser::Program& program) {
    if (moduleCodegen_) {
        moduleCodegen_->collectReflectionMetadata(program);
    }
}

void ModularCodegen::generateImportedModule(Parser::Program& program, const std::string& moduleName) {
    if (moduleCodegen_) {
        moduleCodegen_->generateImportedModuleCode(program, moduleName);
    }
}

void ModularCodegen::generateNativeThunk(Parser::MethodDecl& node,
                                        const std::string& className,
                                        const std::string& namespaceName) {
    if (nativeCodegen_) {
        nativeCodegen_->generateNativeThunk(node, className, namespaceName);
    }
}

std::string ModularCodegen::getNativeThunkIR() const {
    if (nativeCodegen_) {
        return nativeCodegen_->getIR();
    }
    return "";
}

const std::vector<std::pair<std::string, std::string>>& ModularCodegen::getNativeStringLiterals() const {
    static std::vector<std::pair<std::string, std::string>> empty;
    if (nativeCodegen_) {
        return nativeCodegen_->stringLiterals();
    }
    return empty;
}

void ModularCodegen::resetNativeCodegen() {
    if (nativeCodegen_) {
        nativeCodegen_->reset();
    }
}

void ModularCodegen::generateLambdaTemplates(Semantic::SemanticAnalyzer& analyzer) {
    if (lambdaTemplateCodegen_) {
        lambdaTemplateCodegen_->generateLambdaTemplates(analyzer);
    }
}

const std::vector<std::string>& ModularCodegen::getLambdaDefinitions() const {
    static std::vector<std::string> empty;
    if (lambdaTemplateCodegen_) {
        return lambdaTemplateCodegen_->getLambdaDefinitions();
    }
    return empty;
}

std::string ModularCodegen::getLambdaFunction(const std::string& key) const {
    if (lambdaTemplateCodegen_) {
        return lambdaTemplateCodegen_->getLambdaFunction(key);
    }
    return "";
}

const LambdaTemplateCodegen::LambdaInfo* ModularCodegen::getLambdaInfo(const std::string& key) const {
    if (lambdaTemplateCodegen_) {
        return lambdaTemplateCodegen_->getLambdaInfo(key);
    }
    return nullptr;
}

void ModularCodegen::resetLambdaCodegen() {
    if (lambdaTemplateCodegen_) {
        lambdaTemplateCodegen_->reset();
    }
}

// === Function Declarations ===

void ModularCodegen::markFunctionDeclared(const std::string& funcName) {
    declaredFunctions_.insert(funcName);
}

bool ModularCodegen::isFunctionDeclared(const std::string& funcName) const {
    return declaredFunctions_.find(funcName) != declaredFunctions_.end();
}

void ModularCodegen::generateFunctionDeclarations(Parser::Program& program) {
    // Use GlobalBuilder to create function declarations in the Module
    LLVMIR::GlobalBuilder globalBuilder(ctx_.module());
    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();

    // Primitive types that should use simple names (no namespace prefix)
    static const std::unordered_set<std::string> primitives = {
        "Integer", "String", "Bool", "Float", "Double", "None", "Void",
        "Int", "Int8", "Int16", "Int32", "Int64", "Byte"
    };

    // Helper lambda to get LLVM type from XXML type
    auto getLLVMType = [&](const std::string& xxmlType) -> LLVMIR::Type* {
        if (xxmlType == "None" || xxmlType == "void") return typeCtx.getVoidTy();
        if (xxmlType == "Integer" || xxmlType == "Int" || xxmlType == "Int64") return typeCtx.getInt64Ty();
        if (xxmlType == "Int32") return typeCtx.getInt32Ty();
        if (xxmlType == "Int16") return typeCtx.getInt16Ty();
        if (xxmlType == "Int8" || xxmlType == "Byte") return typeCtx.getInt8Ty();
        if (xxmlType == "Bool") return typeCtx.getInt1Ty();
        if (xxmlType == "Float") return typeCtx.getFloatTy();
        if (xxmlType == "Double") return typeCtx.getDoubleTy();
        // All other types are pointers
        return typeCtx.getPtrTy();
    };

    // Helper lambda to process a class
    std::function<void(Parser::ClassDecl*, const std::string&)> processClass =
        [&](Parser::ClassDecl* classDecl, const std::string& namespacePrefix) {
        if (!classDecl) return;

        // Skip template declarations (only process instantiations)
        if (!classDecl->templateParams.empty() && classDecl->name.find('<') == std::string::npos) {
            return;
        }

        // Skip primitive types - their declarations are in the preamble
        if (primitives.count(classDecl->name) > 0) {
            return;
        }

        // Build the full class name with namespace
        std::string fullClassName = namespacePrefix.empty() ?
            classDecl->name : namespacePrefix + "_" + classDecl->name;
        std::string mangledClassName = TypeNormalizer::mangleForLLVM(fullClassName);

        // Process constructors and methods from sections
        for (const auto& section : classDecl->sections) {
            if (!section) continue;
            for (const auto& decl : section->declarations) {
                // Check for constructors
                if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                    if (ctor->isDefault) continue;

                    // Build parameter types
                    std::vector<LLVMIR::Type*> paramTypes;
                    paramTypes.push_back(typeCtx.getPtrTy());  // 'this' pointer
                    for (const auto& param : ctor->parameters) {
                        LLVMIR::Type* paramType = getLLVMType(param->type->typeName);
                        if (dynamic_cast<LLVMIR::VoidType*>(paramType)) {
                            paramType = typeCtx.getPtrTy();  // void not valid for params
                        }
                        paramTypes.push_back(paramType);
                    }

                    std::string funcName = mangledClassName + "_Constructor_" +
                        std::to_string(ctor->parameters.size());

                    if (!isFunctionDeclared(funcName)) {
                        markFunctionDeclared(funcName);
                        LLVMIR::FunctionType* funcType = typeCtx.getFunctionTy(
                            typeCtx.getPtrTy(), std::move(paramTypes));
                        globalBuilder.declareFunction(funcType, funcName);
                    }
                }
                // Check for methods
                else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                    // Skip native FFI methods
                    if (method->isNative) continue;

                    // Build parameter types
                    std::vector<LLVMIR::Type*> paramTypes;
                    paramTypes.push_back(typeCtx.getPtrTy());  // 'this' pointer
                    for (const auto& param : method->parameters) {
                        // Check if parameter should be a pointer based on ownership
                        bool isNativeParam = param->type->typeName.find("NativeType<") != std::string::npos;
                        bool isObjectParam = !isNativeParam &&
                            (param->type->ownership == Parser::OwnershipType::Owned ||
                             param->type->ownership == Parser::OwnershipType::Reference ||
                             param->type->ownership == Parser::OwnershipType::Copy);
                        LLVMIR::Type* paramType = isObjectParam
                            ? typeCtx.getPtrTy()
                            : getLLVMType(param->type->typeName);
                        if (dynamic_cast<LLVMIR::VoidType*>(paramType)) {
                            paramType = typeCtx.getPtrTy();
                        }
                        paramTypes.push_back(paramType);
                    }

                    std::string funcName = mangledClassName + "_" + method->name;

                    // Check if return type should be a pointer based on ownership
                    bool isNativeReturnType = method->returnType->typeName.find("NativeType<") != std::string::npos;
                    bool isObjectReturnType = !isNativeReturnType &&
                        (method->returnType->ownership == Parser::OwnershipType::Owned ||
                         method->returnType->ownership == Parser::OwnershipType::Reference ||
                         method->returnType->ownership == Parser::OwnershipType::Copy);
                    LLVMIR::Type* returnType = isObjectReturnType
                        ? typeCtx.getPtrTy()
                        : getLLVMType(method->returnType->typeName);

                    if (!isFunctionDeclared(funcName)) {
                        markFunctionDeclared(funcName);
                        LLVMIR::FunctionType* funcType = typeCtx.getFunctionTy(
                            returnType, std::move(paramTypes));
                        globalBuilder.declareFunction(funcType, funcName);
                    }
                }
            }
        }
    };

    // Helper lambda to process namespace declarations recursively
    std::function<void(Parser::NamespaceDecl*, const std::string&)> processNamespace =
        [&](Parser::NamespaceDecl* ns, const std::string& prefix) {
        if (!ns) return;

        std::string newPrefix = prefix.empty() ? ns->name : prefix + "_" + ns->name;

        for (const auto& decl : ns->declarations) {
            if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
                processClass(classDecl, newPrefix);
            } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
                processNamespace(nestedNs, newPrefix);
            }
        }
    };

    // Process all declarations in the program
    for (const auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            processClass(classDecl, "");
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            processNamespace(ns, "");
        }
    }
}

// === Processor Entry Points ===

void ModularCodegen::generateProcessorEntryPoints(Parser::Program& program,
                                                  const std::string& annotationName) {
    LLVMIR::GlobalBuilder globalBuilder(ctx_.module());
    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();

    // Find the annotation declaration with a processor block
    Parser::AnnotationDecl* processorAnnotation = nullptr;
    for (const auto& decl : program.declarations) {
        if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            for (const auto& nsDecl : ns->declarations) {
                if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(nsDecl.get())) {
                    if (annotDecl->processor) {
                        processorAnnotation = annotDecl;
                        break;
                    }
                }
            }
        } else if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(decl.get())) {
            if (annotDecl->processor) {
                processorAnnotation = annotDecl;
                break;
            }
        }
    }

    if (!processorAnnotation) {
        return;  // No processor block found
    }

    std::string finalAnnotationName = annotationName.empty() ?
        processorAnnotation->name : annotationName;

    // Create annotation name string constant
    LLVMIR::GlobalVariable* nameStrGlobal =
        globalBuilder.createStringConstant(finalAnnotationName, "__processor_annotation_name");

    // === Generate __xxml_processor_annotation_name() ===
    {
        LLVMIR::FunctionType* funcType = typeCtx.getFunctionTy(
            typeCtx.getPtrTy(), {});
        LLVMIR::Function* func = globalBuilder.defineFunction(
            funcType, "__xxml_processor_annotation_name", LLVMIR::Function::Linkage::DLLExport);

        LLVMIR::BasicBlock* entryBB = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBB);
        ctx_.builder().createRet(LLVMIR::AnyValue(nameStrGlobal->toTypedValue()));
    }

    // === Generate __xxml_processor_process() ===
    {
        std::vector<LLVMIR::Type*> paramTypes = {
            typeCtx.getPtrTy(),  // reflection
            typeCtx.getPtrTy(),  // compilation
            typeCtx.getPtrTy()   // args
        };
        LLVMIR::FunctionType* funcType = typeCtx.getFunctionTy(
            typeCtx.getVoidTy(), std::move(paramTypes));
        LLVMIR::Function* func = globalBuilder.defineFunction(
            funcType, "__xxml_processor_process", LLVMIR::Function::Linkage::DLLExport);

        // Name the parameters
        func->getArg(0)->setName("reflection");
        func->getArg(1)->setName("compilation");
        func->getArg(2)->setName("args");

        LLVMIR::BasicBlock* entryBB = func->createBasicBlock("entry");
        ctx_.builder().setInsertPoint(entryBB);

        // Find the onAnnotate method in the processor block
        bool hasOnAnnotate = false;
        std::string processorClassName = finalAnnotationName + "_Processor";

        if (processorAnnotation->processor) {
            for (const auto& section : processorAnnotation->processor->sections) {
                for (const auto& decl : section->declarations) {
                    if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                        if (methodDecl->name == "onAnnotate") {
                            hasOnAnnotate = true;
                            break;
                        }
                    }
                }
                if (hasOnAnnotate) break;
            }
        }

        if (hasOnAnnotate) {
            // Get or declare the onAnnotate method
            std::string methodName = processorClassName + "_onAnnotate";

            // Declare the method if not already declared
            LLVMIR::Function* onAnnotateFunc = ctx_.module().getFunction(methodName);
            if (!onAnnotateFunc) {
                std::vector<LLVMIR::Type*> onAnnotateParams = {
                    typeCtx.getPtrTy(),  // this (null for processors)
                    typeCtx.getPtrTy(),  // reflection context
                    typeCtx.getPtrTy()   // compilation context
                };
                LLVMIR::FunctionType* onAnnotateFuncType = typeCtx.getFunctionTy(
                    typeCtx.getVoidTy(), std::move(onAnnotateParams));
                onAnnotateFunc = globalBuilder.declareFunction(onAnnotateFuncType, methodName);
            }

            // Call the onAnnotate method with null for 'this'
            std::vector<LLVMIR::AnyValue> args = {
                LLVMIR::AnyValue(ctx_.builder().getNullPtr()),
                LLVMIR::AnyValue(LLVMIR::PtrValue(func->getArg(0))),
                LLVMIR::AnyValue(LLVMIR::PtrValue(func->getArg(1)))
            };
            ctx_.builder().createCallVoid(onAnnotateFunc, args);
        }

        ctx_.builder().createRetVoid();
    }
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
