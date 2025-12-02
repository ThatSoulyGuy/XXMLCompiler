#pragma once
#include <memory>
#include "../Parser/AST.h"
#include "SymbolTable.h"
#include "../Common/Error.h"
#include "../Core/TypeContext.h"
#include "../AnnotationProcessor/AnnotationProcessor.h"

namespace XXML {

// Forward declaration
namespace Core { class CompilationContext; }

namespace Semantic {

class SemanticAnalyzer : public Parser::ASTVisitor {
public:
    // ✅ SAFE: Template class info stores COPIES of template parameters, not raw pointers
    // Made public for cross-module template registration
    struct TemplateClassInfo {
        std::string qualifiedName;
        std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
        std::string baseClassName;  // COPIED from AST
        Parser::ClassDecl* astNode = nullptr;  // Optional: only valid for same-module access
    };

    // ✅ SAFE: Template method info stores COPIES of template parameters
    struct TemplateMethodInfo {
        std::string className;
        std::string methodName;
        std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
        Parser::MethodDecl* astNode = nullptr;  // Optional: only valid for same-module access
    };

    // ✅ Template lambda info stores COPIES of template parameters
    struct TemplateLambdaInfo {
        std::string variableName;  // Variable name holding the lambda
        std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
        Parser::LambdaExpr* astNode = nullptr;  // Optional: only valid for same-module access
        std::unordered_map<std::string, std::string> capturedVarTypes;  // varName -> type (e.g., "Integer^")
    };

    // Annotation parameter info - made public for cross-module sharing
    struct AnnotationParamInfo {
        std::string name;
        std::string typeName;
        Parser::OwnershipType ownership;
        bool hasDefault;
    };

    // Annotation info - made public for cross-module sharing
    struct AnnotationInfo {
        std::string name;
        std::vector<Parser::AnnotationTarget> allowedTargets;
        std::vector<AnnotationParamInfo> parameters;
        bool retainAtRuntime;
        Parser::AnnotationDecl* astNode;
    };

    // Pending processor compilation info - made public for cross-module sharing
    struct PendingProcessorCompilation {
        std::string annotationName;
        Parser::AnnotationDecl* annotDecl;
        Parser::ProcessorDecl* processorDecl;
        std::vector<std::string> imports;  // Imports from the source file
        std::vector<Parser::ClassDecl*> userClasses;  // User-defined classes from the same file
    };

private:
    SymbolTable* symbolTable_;  // Now points to context's symbol table
    Core::CompilationContext* context_;  // ✅ Use context instead of static state
    Common::ErrorReporter& errorReporter;
    Core::TypeContext typeContext_;  // Type information for code generation
    AnnotationProcessor::AnnotationProcessor annotationProcessor_;  // Annotation processor
    std::string currentClass;
    std::string currentNamespace;
    bool enableValidation;  // Controls whether to do full validation
    bool inTemplateDefinition;  // True when analyzing template class/method definition
    bool inProcessorContext_ = false;  // True when visiting ProcessorDecl (enables intrinsic types)
    std::string processorTargetType_;  // The type being annotated (for getTargetValue() return type)
    std::string currentAnnotationName_;  // The annotation being processed (for getAnnotationArg() return type)
    std::set<std::string> templateTypeParameters;  // Template parameters in current scope

    // Type checking helpers
    bool isCompatibleType(const std::string& expected, const std::string& actual);
    bool isCompatibleOwnership(Parser::OwnershipType expected, Parser::OwnershipType actual);
    std::string getExpressionType(Parser::Expression* expr);
    Parser::OwnershipType getExpressionOwnership(Parser::Expression* expr);

    // TypeContext population helpers
    void registerExpressionType(Parser::Expression* expr,
                                const std::string& xxmlType,
                                Parser::OwnershipType ownership);
    void registerVariableType(const std::string& varName,
                             const std::string& xxmlType,
                             Parser::OwnershipType ownership);
    std::string convertXXMLTypeToCpp(const std::string& xxmlType,
                                    Parser::OwnershipType ownership);

    // Validation helpers
    void validateOwnershipSemantics(Parser::TypeRef* type, const Common::SourceLocation& loc);
    void validateMethodCall(Parser::CallExpr& node);
    void validateConstructorCall(Parser::CallExpr& node);
    bool isTemporaryExpression(Parser::Expression* expr);  // Check if expression is a temporary (rvalue)
    void validateOnAnnotateSignature(Parser::MethodDecl& method);  // Validate processor onAnnotate method
    bool isCompilerIntrinsicType(const std::string& typeName) const;  // Check for ReflectionContext, CompilationContext
    void setProcessorTargetType(const std::string& typeName) { processorTargetType_ = typeName; }  // Set target type for getTargetValue()

    // Temporary storage for expression type information
    std::unordered_map<Parser::Expression*, std::string> expressionTypes;
    std::unordered_map<Parser::Expression*, Parser::OwnershipType> expressionOwnerships;

    // Template tracking
    struct TemplateInstantiation {
        std::string templateName;
        std::vector<Parser::TemplateArgument> arguments;  // Can be type or value arguments
        std::vector<int64_t> evaluatedValues;  // Evaluated constant values for non-type parameters

        bool operator<(const TemplateInstantiation& other) const {
            if (templateName != other.templateName) return templateName < other.templateName;
            if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
            // Compare arguments (simplified - just compare types for now)
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (arguments[i].kind != other.arguments[i].kind) return arguments[i].kind < other.arguments[i].kind;
                if (arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                    if (arguments[i].typeArg != other.arguments[i].typeArg)
                        return arguments[i].typeArg < other.arguments[i].typeArg;
                } else if (i < evaluatedValues.size() && i < other.evaluatedValues.size()) {
                    if (evaluatedValues[i] != other.evaluatedValues[i])
                        return evaluatedValues[i] < other.evaluatedValues[i];
                }
            }
            return false;
        }
    };

    struct MethodTemplateInstantiation {
        std::string className;  // Base class containing the method (e.g., "Holder")
        std::string instantiatedClassName;  // Full instantiated class name (e.g., "Holder_Integer")
        std::string methodName;  // Template method name
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;

        bool operator<(const MethodTemplateInstantiation& other) const {
            if (className != other.className) return className < other.className;
            if (instantiatedClassName != other.instantiatedClassName) return instantiatedClassName < other.instantiatedClassName;
            if (methodName != other.methodName) return methodName < other.methodName;
            if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (arguments[i].kind != other.arguments[i].kind) return arguments[i].kind < other.arguments[i].kind;
                if (arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                    if (arguments[i].typeArg != other.arguments[i].typeArg)
                        return arguments[i].typeArg < other.arguments[i].typeArg;
                } else if (i < evaluatedValues.size() && i < other.evaluatedValues.size()) {
                    if (evaluatedValues[i] != other.evaluatedValues[i])
                        return evaluatedValues[i] < other.evaluatedValues[i];
                }
            }
            return false;
        }
    };

    struct LambdaTemplateInstantiation {
        std::string variableName;  // Variable name holding the lambda
        std::vector<Parser::TemplateArgument> arguments;
        std::vector<int64_t> evaluatedValues;

        bool operator<(const LambdaTemplateInstantiation& other) const {
            if (variableName != other.variableName) return variableName < other.variableName;
            if (arguments.size() != other.arguments.size()) return arguments.size() < other.arguments.size();
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (arguments[i].kind != other.arguments[i].kind) return arguments[i].kind < other.arguments[i].kind;
                if (arguments[i].kind == Parser::TemplateArgument::Kind::Type) {
                    if (arguments[i].typeArg != other.arguments[i].typeArg)
                        return arguments[i].typeArg < other.arguments[i].typeArg;
                } else if (i < evaluatedValues.size() && i < other.evaluatedValues.size()) {
                    if (evaluatedValues[i] != other.evaluatedValues[i])
                        return evaluatedValues[i] < other.evaluatedValues[i];
                }
            }
            return false;
        }
    };

    std::unordered_map<std::string, TemplateClassInfo> templateClasses;  // Template class name -> info
    std::set<TemplateInstantiation> templateInstantiations;  // Set of template instantiations

    // Method template tracking (key: className::methodName -> info)
    std::unordered_map<std::string, TemplateMethodInfo> templateMethods;
    std::set<MethodTemplateInstantiation> methodTemplateInstantiations;

    // Lambda template tracking (key: variableName -> info)
    std::unordered_map<std::string, TemplateLambdaInfo> templateLambdas_;
    std::set<LambdaTemplateInstantiation> lambdaTemplateInstantiations_;

    // Class member registry for validation
    struct MethodInfo {
        std::string returnType;
        Parser::OwnershipType returnOwnership;
        std::vector<std::pair<std::string, Parser::OwnershipType>> parameters; // (type, ownership) pairs
        bool isConstructor;
        bool isCompiletime = false;  // Whether this method can be evaluated at compile-time
    };

    struct ClassInfo {
        std::string qualifiedName;  // Full name including namespace
        std::unordered_map<std::string, MethodInfo> methods;
        std::unordered_map<std::string, std::pair<std::string, Parser::OwnershipType>> properties; // name -> (type, ownership)
        std::string baseClassName;  // ✅ SAFE: COPIED from AST, not pointer
        std::vector<Parser::TemplateParameter> templateParams;  // ✅ SAFE: COPIED from AST
        bool isTemplate = false;  // Whether this is a template class
        bool isCompiletime = false;  // Whether this is a compile-time class (all methods must be compiletime)
        Parser::ClassDecl* astNode = nullptr;  // Optional: only valid for same-module access
    };

    // ✅ REMOVED STATIC STATE - now instance-based in context
    std::unordered_map<std::string, ClassInfo> classRegistry_;  // Qualified class name -> ClassInfo
    std::set<std::string> validNamespaces_;  // Track all valid namespaces
    std::set<std::string> importedNamespaces_;  // Track imported namespaces for unqualified name lookup
    std::vector<Parser::ClassDecl*> localClasses_;  // User-defined classes in the current file (for processor access)

    // Move tracking for ownership safety
    std::set<std::string> movedVariables_;  // Variables that have been moved from (owned capture or owned param)

    // Function type tracking for lambda .call() ownership validation
    // Maps variable name -> vector of parameter ownership types
    std::unordered_map<std::string, std::vector<Parser::OwnershipType>> functionTypeParams_;

    // Move tracking helpers
    void markVariableMoved(const std::string& varName, const Common::SourceLocation& loc);
    bool isVariableMoved(const std::string& varName) const;
    void checkVariableNotMoved(const std::string& varName, const Common::SourceLocation& loc);
    void resetMovedVariables();  // Called when entering new scope

    // Function type tracking helpers
    void registerFunctionType(const std::string& varName, Parser::FunctionTypeRef* funcType);
    std::vector<Parser::OwnershipType>* getFunctionTypeParams(const std::string& varName);

    // Helper for templates
    void recordTemplateInstantiation(const std::string& templateName, const std::vector<Parser::TemplateArgument>& args);
    void recordMethodTemplateInstantiation(const std::string& className, const std::string& instantiatedClassName, const std::string& methodName, const std::vector<Parser::TemplateArgument>& args);
    void recordLambdaTemplateInstantiation(const std::string& variableName, const std::vector<Parser::TemplateArgument>& args);
    int64_t evaluateConstantExpression(Parser::Expression* expr);  // Evaluate constant expressions at compile time
    bool isTemplateClass(const std::string& className);
    bool isTemplateMethod(const std::string& className, const std::string& methodName);
    bool isTemplateLambda(const std::string& variableName);

    // Constraint registry and validation
    struct ConstraintInfo {
        std::string name;
        std::vector<Parser::TemplateParameter> templateParams;
        std::vector<Parser::ConstraintParamBinding> paramBindings;
        std::vector<Parser::RequireStmt*> requirements;
        Parser::ConstraintDecl* astNode;
    };

    std::unordered_map<std::string, ConstraintInfo> constraintRegistry_;  // Constraint name -> info

    // Annotation registry (uses public structs AnnotationInfo, AnnotationParamInfo, PendingProcessorCompilation)
    std::unordered_map<std::string, AnnotationInfo> annotationRegistry_;  // Annotation name -> info
    std::vector<PendingProcessorCompilation> pendingProcessorCompilations_;  // Annotations with inline processors

    // Enumeration registry
    struct EnumValueInfo {
        std::string name;
        int64_t value;
    };
    struct EnumInfo {
        std::string name;
        std::string qualifiedName;  // Namespace::EnumName
        std::vector<EnumValueInfo> values;
    };
    std::unordered_map<std::string, EnumInfo> enumRegistry_;  // Qualified enum name -> EnumInfo

    // Callback type registry for FFI callbacks
    struct CallbackParamInfo {
        std::string name;
        std::string typeName;
        Parser::OwnershipType ownership;
    };
    struct CallbackTypeInfo {
        std::string name;
        std::string qualifiedName;
        Parser::CallingConvention convention;
        std::string returnType;
        Parser::OwnershipType returnOwnership;
        std::vector<CallbackParamInfo> parameters;
        Parser::CallbackTypeDecl* astNode;
    };
    std::unordered_map<std::string, CallbackTypeInfo> callbackTypeRegistry_;  // Qualified callback type name -> info

    // Annotation validation helpers
    void validateAnnotationUsage(Parser::AnnotationUsage& usage,
                                 Parser::AnnotationTarget targetKind,
                                 const std::string& targetName,
                                 const Common::SourceLocation& targetLoc,
                                 Parser::ASTNode* astNode = nullptr);
    bool isValidAnnotationTarget(const AnnotationInfo& annotation, Parser::AnnotationTarget target);
    std::string annotationTargetToString(Parser::AnnotationTarget target);

    bool validateConstraint(const std::string& typeName,
                           const std::vector<Parser::ConstraintRef>& constraints,
                           bool constraintsAreAnd,
                           const std::unordered_map<std::string, std::string>& typeSubstitutions = {});
    bool validateSingleConstraint(const std::string& typeName,
                                  const Parser::ConstraintRef& constraint,
                                  const std::unordered_map<std::string, std::string>& typeSubstitutions,
                                  bool reportErrors = true);
    bool validateConstraintRequirements(const std::string& typeName,
                                       const ConstraintInfo& constraint,
                                       const Common::SourceLocation& loc,
                                       const std::unordered_map<std::string, std::string>& providedSubstitutions = {},
                                       bool reportErrors = true);
    bool hasMethod(const std::string& className,
                  const std::string& methodName,
                  Parser::TypeRef* returnType);
    bool hasConstructor(const std::string& className,
                       const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes);
    bool evaluateTruthCondition(Parser::Expression* expr,
                               const std::unordered_map<std::string, std::string>& typeSubstitutions);
    bool isTypeCompatible(const std::string& actualType, const std::string& constraintType);
    // Compile-time helpers
    bool isCompiletimeType(const std::string& typeName) const;
    bool isCompiletimeMethod(const std::string& className, const std::string& methodName);
    bool hasCompiletimeConstructor(const std::string& className,
                                   const std::vector<std::unique_ptr<Parser::TypeRef>>& paramTypes);
    // Helper for class member lookup
    ClassInfo* findClass(const std::string& className);
    bool validateQualifiedIdentifier(const std::string& qualifiedName, const Common::SourceLocation& loc);

    // Template-aware qualified name parsing
    std::string extractClassName(const std::string& qualifiedName);
    std::string extractMethodName(const std::string& qualifiedName);
    std::string buildQualifiedName(Parser::Expression* expr);

public:
    // Method lookup for code generation (needed by backends)
    MethodInfo* findMethod(const std::string& className, const std::string& methodName);
    // Get template instantiations for code generation
    const std::set<TemplateInstantiation>& getTemplateInstantiations() const {
        return templateInstantiations;
    }
    const std::unordered_map<std::string, TemplateClassInfo>& getTemplateClasses() const {
        return templateClasses;
    }
    const std::set<MethodTemplateInstantiation>& getMethodTemplateInstantiations() const {
        return methodTemplateInstantiations;
    }

    // Get type context for code generation
    Core::TypeContext& getTypeContext() {
        return typeContext_;
    }
    const Core::TypeContext& getTypeContext() const {
        return typeContext_;
    }
    const std::unordered_map<std::string, TemplateMethodInfo>& getTemplateMethods() const {
        return templateMethods;
    }
    const std::set<LambdaTemplateInstantiation>& getLambdaTemplateInstantiations() const {
        return lambdaTemplateInstantiations_;
    }
    const std::unordered_map<std::string, TemplateLambdaInfo>& getTemplateLambdas() const {
        return templateLambdas_;
    }

    // Register template classes and instantiations from other modules
    void registerTemplateClass(const std::string& name, const TemplateClassInfo& info) {
        templateClasses[name] = info;
    }

    // Helper to create TemplateClassInfo from a ClassDecl (for same-module registration)
    static TemplateClassInfo createTemplateClassInfo(const std::string& qualifiedName, Parser::ClassDecl* classDecl) {
        TemplateClassInfo info;
        info.qualifiedName = qualifiedName;
        info.templateParams = classDecl->templateParams;  // Copy template params
        info.baseClassName = classDecl->baseClass;  // Copy base class name
        info.astNode = classDecl;  // Keep reference for same-module access
        return info;
    }

    void mergeTemplateInstantiation(const TemplateInstantiation& inst) {
        templateInstantiations.insert(inst);
    }

    // Public accessor for expression types (used by code generator)
    std::string getExpressionTypePublic(Parser::Expression* expr) {
        return getExpressionType(expr);
    }

    // Get annotation processor for processing after semantic analysis
    AnnotationProcessor::AnnotationProcessor& getAnnotationProcessor() {
        return annotationProcessor_;
    }

    // Get pending processor compilations (inline processors in annotations)
    const std::vector<PendingProcessorCompilation>& getPendingProcessorCompilations() const {
        return pendingProcessorCompilations_;
    }

    // Get annotation registry (for cross-module annotation sharing)
    const std::unordered_map<std::string, AnnotationInfo>& getAnnotationRegistry() const {
        return annotationRegistry_;
    }

    // Register annotation from another module
    void registerAnnotation(const std::string& name, const AnnotationInfo& info) {
        if (annotationRegistry_.find(name) == annotationRegistry_.end()) {
            annotationRegistry_[name] = info;
        }
    }

    // Get enum registry (for code generation)
    const std::unordered_map<std::string, EnumInfo>& getEnumRegistry() const {
        return enumRegistry_;
    }

    // Get callback type registry (for code generation)
    const std::unordered_map<std::string, CallbackTypeInfo>& getCallbackTypeRegistry() const {
        return callbackTypeRegistry_;
    }

    // Register callback type from another module
    void registerCallbackType(const std::string& name, const CallbackTypeInfo& info) {
        if (callbackTypeRegistry_.find(name) == callbackTypeRegistry_.end()) {
            callbackTypeRegistry_[name] = info;
        }
    }

    // Merge pending processor compilations from another module
    void mergePendingProcessorCompilations(const std::vector<PendingProcessorCompilation>& pending) {
        for (const auto& p : pending) {
            pendingProcessorCompilations_.push_back(p);
        }
    }

public:
    // ✅ NEW: Accept CompilationContext for registry access
    SemanticAnalyzer(Core::CompilationContext& context, Common::ErrorReporter& reporter);

    // Legacy constructor for backwards compatibility
    SemanticAnalyzer(Common::ErrorReporter& reporter);

    void analyze(Parser::Program& program);

    // Control validation (for two-phase analysis)
    void setValidationEnabled(bool enabled) { enableValidation = enabled; }
    bool isValidationEnabled() const { return enableValidation; }

    // Set module name for symbol table registration
    void setModuleName(const std::string& moduleName) {
        if (symbolTable_) {
            symbolTable_->setModuleName(moduleName);
        }
    }

    // Visitor methods
    void visit(Parser::Program& node) override;
    void visit(Parser::ImportDecl& node) override;
    void visit(Parser::NamespaceDecl& node) override;
    void visit(Parser::ClassDecl& node) override;
    void visit(Parser::NativeStructureDecl& node) override;
    void visit(Parser::CallbackTypeDecl& node) override;
    void visit(Parser::EnumValueDecl& node) override;
    void visit(Parser::EnumerationDecl& node) override;
    void visit(Parser::AccessSection& node) override;
    void visit(Parser::PropertyDecl& node) override;
    void visit(Parser::ConstructorDecl& node) override;
    void visit(Parser::DestructorDecl& node) override;
    void visit(Parser::MethodDecl& node) override;
    void visit(Parser::ParameterDecl& node) override;
    void visit(Parser::EntrypointDecl& node) override;
    void visit(Parser::ConstraintDecl& node) override;
    void visit(Parser::AnnotateDecl& node) override;
    void visit(Parser::ProcessorDecl& node) override;
    void visit(Parser::AnnotationDecl& node) override;
    void visit(Parser::AnnotationUsage& node) override;

    void visit(Parser::InstantiateStmt& node) override;
    void visit(Parser::RequireStmt& node) override;
    void visit(Parser::AssignmentStmt& node) override;
    void visit(Parser::RunStmt& node) override;
    void visit(Parser::ForStmt& node) override;
    void visit(Parser::ExitStmt& node) override;
    void visit(Parser::ReturnStmt& node) override;
    void visit(Parser::IfStmt& node) override;
    void visit(Parser::WhileStmt& node) override;
    void visit(Parser::BreakStmt& node) override;
    void visit(Parser::ContinueStmt& node) override;

    void visit(Parser::IntegerLiteralExpr& node) override;
    void visit(Parser::FloatLiteralExpr& node) override;
    void visit(Parser::DoubleLiteralExpr& node) override;
    void visit(Parser::StringLiteralExpr& node) override;
    void visit(Parser::BoolLiteralExpr& node) override;
    void visit(Parser::ThisExpr& node) override;
    void visit(Parser::IdentifierExpr& node) override;
    void visit(Parser::ReferenceExpr& node) override;
    void visit(Parser::MemberAccessExpr& node) override;
    void visit(Parser::CallExpr& node) override;
    void visit(Parser::BinaryExpr& node) override;
    void visit(Parser::TypeOfExpr& node) override;
    void visit(Parser::LambdaExpr& node) override;

    void visit(Parser::TypeRef& node) override;
    void visit(Parser::FunctionTypeRef& node) override;
};

} // namespace Semantic
} // namespace XXML
