#pragma once

#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/TypedBuilder.h"
#include "Backends/LLVMIR/TypedModule.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <set>

namespace XXML {

namespace Parser { class ClassDecl; }
namespace Semantic { class SemanticAnalyzer; }
namespace Core { class CompilationContext; }

namespace Backends {
namespace Codegen {

// Forward declarations
class ExprCodegen;
class StmtCodegen;
class DeclCodegen;

// Class property/field information
struct PropertyInfo {
    std::string name;
    std::string xxmlType;
    std::string llvmType;
    size_t index;
    bool isObjectType = false;  // true if owned/reference/copy type (stored as ptr)
};

// Class metadata
struct ClassInfo {
    std::string name;
    std::string mangledName;
    std::vector<PropertyInfo> properties;
    LLVMIR::StructType* structType = nullptr;
    size_t instanceSize = 0;
};

// Variable tracking information
struct VariableInfo {
    std::string name;
    std::string xxmlType;
    LLVMIR::AnyValue value;
    LLVMIR::AllocaInst* alloca = nullptr;
    bool isParameter = false;
};

// Loop context for break/continue
struct LoopContext {
    LLVMIR::BasicBlock* condBlock;
    LLVMIR::BasicBlock* endBlock;
};

// Lambda information
struct LambdaInfo {
    std::string closureTypeName;
    std::string functionName;
    std::string returnType;
    std::vector<std::string> paramTypes;
    std::vector<std::pair<std::string, int>> captures; // name, captureMode
};

// Native FFI method information
struct NativeMethodInfo {
    std::vector<std::string> paramTypes;
    std::vector<std::string> xxmlParamTypes;
    std::vector<bool> isStringPtr;
    std::vector<bool> isCallback;
    std::string returnType;
    std::string xxmlReturnType;
};

// Callback thunk information
struct CallbackThunkInfo {
    std::string callbackTypeName;
    std::string thunkFunctionName;
    std::string returnLLVMType;
    std::vector<std::string> paramLLVMTypes;
};

// RAII destructor tracking for scope cleanup
struct ScopeDestructorInfo {
    std::string varName;
    std::string typeName;
    LLVMIR::AllocaInst* alloca;
};

// Reflection metadata for classes (moved from LLVMBackend)
struct ReflectionClassMetadata {
    std::string name;
    std::string namespaceName;
    std::string fullName;
    std::vector<std::pair<std::string, std::string>> properties;  // name, type
    std::vector<std::string> propertyOwnerships;  // ownership chars (^, &, %)
    std::vector<std::pair<std::string, std::string>> methods;  // name, return type
    std::vector<std::string> methodReturnOwnerships;  // ownership for return types
    std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> methodParameters;  // name, type, ownership
    bool isTemplate = false;
    std::vector<std::string> templateParams;
    size_t instanceSize = 0;
    Parser::ClassDecl* astNode = nullptr;
};

// Annotation argument value (moved from LLVMBackend)
struct AnnotationArgValue {
    enum Kind { Integer, String, Bool, Float, Double } kind = Integer;
    int64_t intValue = 0;
    std::string stringValue;
    bool boolValue = false;
    float floatValue = 0.0f;
    double doubleValue = 0.0;
};

// Pending annotation metadata for retained annotations (moved from LLVMBackend)
struct PendingAnnotationMetadata {
    std::string annotationName;
    std::string targetType;      // "type", "method", or "property"
    std::string typeName;        // Class name
    std::string memberName;      // Method/property name (empty for type-level)
    std::vector<std::pair<std::string, AnnotationArgValue>> arguments;
};

/**
 * @brief Shared context for all codegen modules
 *
 * Provides access to the type-safe IR infrastructure, scope tracking,
 * and shared utilities. All codegen modules receive a reference to this.
 */
class CodegenContext {
public:
    explicit CodegenContext(Core::CompilationContext* compCtx = nullptr);
    ~CodegenContext();

    // === IR Infrastructure ===
    LLVMIR::Module& module() { return *module_; }
    const LLVMIR::Module& module() const { return *module_; }

    LLVMIR::IRBuilder& builder() { return *builder_; }
    const LLVMIR::IRBuilder& builder() const { return *builder_; }

    // === Current Scope ===
    LLVMIR::Function* currentFunction() const { return currentFunction_; }
    void setCurrentFunction(LLVMIR::Function* func) { currentFunction_ = func; }

    LLVMIR::BasicBlock* currentBlock() const { return currentBlock_; }
    void setInsertPoint(LLVMIR::BasicBlock* bb);

    // === Namespace/Class Context ===
    std::string_view currentNamespace() const { return currentNamespace_; }
    void setCurrentNamespace(std::string_view ns) { currentNamespace_ = std::string(ns); }

    std::string_view currentClassName() const { return currentClassName_; }
    void setCurrentClassName(std::string_view name) { currentClassName_ = std::string(name); }

    std::string_view currentReturnType() const { return currentReturnType_; }
    void setCurrentReturnType(std::string_view type) { currentReturnType_ = std::string(type); }

    // === Variable Management ===
    void declareVariable(const std::string& name, const std::string& xxmlType,
                        LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca = nullptr);
    void declareParameter(const std::string& name, const std::string& xxmlType,
                         LLVMIR::AnyValue value);
    bool hasVariable(const std::string& name) const;
    const VariableInfo* getVariable(const std::string& name) const;
    void setVariableValue(const std::string& name, LLVMIR::AnyValue value);

    // === Alloca Management ===
    void registerAlloca(const std::string& name, LLVMIR::AllocaInst* alloca);
    LLVMIR::AllocaInst* getAlloca(const std::string& name) const;

    // === Class Management ===
    void registerClass(const std::string& name, const ClassInfo& info);
    const ClassInfo* getClass(const std::string& name) const;
    bool hasClass(const std::string& name) const;

    // === Type Mapping ===
    LLVMIR::Type* mapType(std::string_view xxmlType);
    std::string getLLVMTypeString(std::string_view xxmlType) const;
    std::string getDefaultValue(std::string_view llvmType) const;

    // === Method Signature Lookup ===
    // Look up return type for a mangled function name (e.g., "IntKey_hash")
    // Returns the XXML return type, or empty string if not found
    std::string lookupMethodReturnType(const std::string& mangledName) const;

    // === Name Mangling ===
    std::string mangleFunctionName(std::string_view className, std::string_view method) const;
    std::string mangleTypeName(std::string_view typeName) const;

    // === Loop Stack (for break/continue) ===
    void pushLoop(LLVMIR::BasicBlock* condBlock, LLVMIR::BasicBlock* endBlock);
    void popLoop();
    const LoopContext* currentLoop() const;

    // === Expression Result ===
    LLVMIR::AnyValue lastExprValue;

    // === String Literals ===
    void addStringLiteral(const std::string& label, const std::string& content);
    const std::vector<std::pair<std::string, std::string>>& stringLiterals() const;
    std::string allocateStringLabel();

    // === Lambda Management ===
    void registerLambda(const std::string& reg, const LambdaInfo& info);
    const LambdaInfo* getLambda(const std::string& reg) const;
    int allocateLambdaId();
    void addPendingLambdaDefinition(const std::string& def);
    const std::vector<std::string>& pendingLambdaDefinitions() const;

    // === Native Method/FFI Tracking ===
    void registerNativeMethod(const std::string& name, const NativeMethodInfo& info);
    const NativeMethodInfo* getNativeMethod(const std::string& name) const;

    // === Callback Thunk Tracking ===
    void registerCallbackThunk(const std::string& typeName, const CallbackThunkInfo& info);
    const CallbackThunkInfo* getCallbackThunk(const std::string& typeName) const;

    // === Enumeration Tracking ===
    void registerEnumValue(const std::string& fullName, int64_t value);
    bool hasEnumValue(const std::string& fullName) const;
    int64_t getEnumValue(const std::string& fullName) const;

    // === Label/Register Allocation ===
    std::string allocateRegister();
    std::string allocateLabel(std::string_view prefix);

    // === Function Tracking ===
    void markFunctionDeclared(const std::string& name);
    void markFunctionDefined(const std::string& name);
    bool isFunctionDeclared(const std::string& name) const;
    bool isFunctionDefined(const std::string& name) const;

    // === Class Generation Tracking ===
    void markClassGenerated(const std::string& name);
    bool isClassGenerated(const std::string& name) const;

    // === Semantic Analyzer (for templates) ===
    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) { semanticAnalyzer_ = analyzer; }
    Semantic::SemanticAnalyzer* semanticAnalyzer() const { return semanticAnalyzer_; }

    // === Template Parameter Substitution ===
    void setTemplateSubstitutions(const std::unordered_map<std::string, std::string>& subs);
    void clearTemplateSubstitutions();
    std::string substituteTemplateParams(const std::string& typeName) const;

    // === Type Name Resolution ===
    // Resolves a simple or template type name to its fully qualified form
    // E.g., "List<Integer>" -> "Language::Collections::List<Language::Core::Integer>"
    std::string resolveToQualifiedName(const std::string& typeName) const;

    // === Compilation Context ===
    Core::CompilationContext* compilationContext() const { return compCtx_; }

    // === RAII Destructor Management ===
    void registerForDestruction(const std::string& varName, const std::string& typeName, LLVMIR::AllocaInst* alloca);
    void emitScopeDestructors();    // Emit destructors for current scope (LIFO order)
    void emitAllDestructors();      // Emit all scope destructors (before return)
    bool needsDestruction(const std::string& typeName) const;

    // === Scope Management ===
    void pushScope();
    void popScope();

    // === Reflection Metadata ===
    void addReflectionMetadata(const std::string& fullName, const ReflectionClassMetadata& metadata);
    const ReflectionClassMetadata* getReflectionMetadata(const std::string& fullName) const;
    const std::unordered_map<std::string, ReflectionClassMetadata>& reflectionMetadata() const { return reflectionMetadata_; }
    bool hasReflectionMetadata(const std::string& fullName) const;

    // === Annotation Metadata ===
    void addAnnotationMetadata(const PendingAnnotationMetadata& metadata);
    const std::vector<PendingAnnotationMetadata>& annotationMetadata() const { return annotationMetadata_; }
    void markAnnotationRetained(const std::string& annotationName);
    bool isAnnotationRetained(const std::string& annotationName) const;
    int allocateAnnotationId() { return annotationMetadataCounter_++; }

    // === Deferred Type Verification ===
    // Checks that a type has been properly resolved (not Deferred or Unknown)
    // Returns true if type is valid, false and logs error if Deferred/Unknown
    bool verifyTypeResolved(const std::string& typeName, const std::string& context);

    // Verify all registered variables have resolved types
    // Call this after template instantiation to ensure no Deferred types remain
    bool verifyAllTypesResolved();

    // Track types encountered during codegen for verification
    void trackTypeUsage(const std::string& typeName, const std::string& location);

    // Get verification statistics
    struct TypeVerificationStats {
        int totalTypes = 0;
        int deferredTypes = 0;
        int unknownTypes = 0;
        std::vector<std::string> unresolvedLocations;
    };
    TypeVerificationStats getTypeVerificationStats() const;

private:
    // IR infrastructure
    std::unique_ptr<LLVMIR::Module> module_;
    std::unique_ptr<LLVMIR::IRBuilder> builder_;

    // Current scope
    LLVMIR::Function* currentFunction_ = nullptr;
    LLVMIR::BasicBlock* currentBlock_ = nullptr;

    // Context
    std::string currentNamespace_;
    std::string currentClassName_;
    std::string currentReturnType_;

    // Variable scopes (stack of scopes for nested blocks)
    std::vector<std::unordered_map<std::string, VariableInfo>> variableScopes_;

    // Alloca tracking
    std::unordered_map<std::string, LLVMIR::AllocaInst*> allocas_;

    // Class info
    std::unordered_map<std::string, ClassInfo> classes_;

    // Loop stack
    std::vector<LoopContext> loopStack_;

    // String literals
    std::vector<std::pair<std::string, std::string>> stringLiterals_;
    int stringLabelCounter_ = 0;

    // Lambda tracking
    std::unordered_map<std::string, LambdaInfo> lambdas_;
    int lambdaCounter_ = 0;
    std::vector<std::string> pendingLambdaDefs_;

    // Native method tracking
    std::unordered_map<std::string, NativeMethodInfo> nativeMethods_;

    // Callback thunk tracking
    std::unordered_map<std::string, CallbackThunkInfo> callbackThunks_;

    // Enumeration values
    std::unordered_map<std::string, int64_t> enumValues_;

    // Counter for registers/labels
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    // Function/class tracking
    std::set<std::string> declaredFunctions_;
    std::set<std::string> definedFunctions_;
    std::set<std::string> generatedClasses_;

    // External references
    Semantic::SemanticAnalyzer* semanticAnalyzer_ = nullptr;
    Core::CompilationContext* compCtx_ = nullptr;

    // Template parameter substitution map (T -> Integer, etc.)
    std::unordered_map<std::string, std::string> templateSubstitutions_;

    // RAII destructor scopes (stack of scope destructor lists)
    std::vector<std::vector<ScopeDestructorInfo>> destructorScopes_;

    // Reflection metadata for classes
    std::unordered_map<std::string, ReflectionClassMetadata> reflectionMetadata_;

    // Annotation metadata
    std::vector<PendingAnnotationMetadata> annotationMetadata_;
    std::set<std::string> retainedAnnotations_;
    int annotationMetadataCounter_ = 0;

    // Type verification tracking (type -> locations where used)
    std::unordered_map<std::string, std::vector<std::string>> typeUsageTracking_;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
