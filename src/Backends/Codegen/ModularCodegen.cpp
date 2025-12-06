#include "Backends/Codegen/ModularCodegen.h"
#include "Backends/LLVMIR/Emitter.h"

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

} // namespace Codegen
} // namespace Backends
} // namespace XXML
