#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include "../Parser/AST.h"
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// CLASS/METHOD INFORMATION STRUCTURES
// (Used by multiple passes for class and method registry)
//==============================================================================

// Method information for validation and lookup
struct MethodInfo {
    std::string returnType;
    Parser::OwnershipType returnOwnership;
    std::vector<std::pair<std::string, Parser::OwnershipType>> parameters;  // (type, ownership) pairs
    bool isConstructor = false;
    bool isCompiletime = false;  // Whether this method can be evaluated at compile-time
};

// Class information for semantic analysis
struct ClassInfo {
    std::string qualifiedName;  // Full name including namespace
    std::unordered_map<std::string, MethodInfo> methods;
    std::unordered_map<std::string, std::pair<std::string, Parser::OwnershipType>> properties;  // name -> (type, ownership)
    std::string baseClassName;  // COPIED from AST, not pointer
    std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
    bool isTemplate = false;  // Whether this is a template class
    bool isInstantiated = false;  // Whether this is an instantiated template class
    bool isCompiletime = false;  // Whether this is a compile-time class
    bool isValueType = false;  // Whether this is a value type (Structure, not Class)
    Parser::ClassDecl* astNode = nullptr;  // Optional: only valid for same-module access
};

// Constraint information for template constraint validation
struct ConstraintInfo {
    std::string name;
    std::vector<Parser::TemplateParameter> templateParams;
    std::vector<Parser::ConstraintParamBinding> paramBindings;
    std::vector<Parser::RequireStmt*> requirements;  // Raw pointers to AST nodes
    Parser::ConstraintDecl* astNode = nullptr;  // Optional: only valid for same-module access
};

//==============================================================================
// SHARED TYPE INFORMATION STRUCTURES
// (Used across multiple pass headers to avoid circular dependencies)
//==============================================================================

// Template class info - stores COPIES of template parameters, not raw pointers
struct TemplateClassInfo {
    std::string qualifiedName;
    std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
    std::string baseClassName;  // COPIED from AST
    Parser::ClassDecl* astNode = nullptr;  // Optional: only valid for same-module access
};

// Information about a call expression in a template body (extracted at registration)
struct TemplateBodyCallInfo {
    std::string className;      // Class being called (may be template param like "T")
    std::string methodName;     // Method being called (e.g., "Constructor")
    std::vector<std::string> argumentTypes;  // Types of arguments (e.g., ["Integer"])
};

// Template method info - stores COPIES of template parameters
struct TemplateMethodInfo {
    std::string className;
    std::string methodName;
    std::string returnTypeName;  // COPIED from AST for safe access
    std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
    std::vector<TemplateBodyCallInfo> callsInBody;  // Extracted calls for validation
    Parser::MethodDecl* astNode = nullptr;  // Optional: only valid for same-module access
};

// Template lambda info - stores COPIES of template parameters
struct TemplateLambdaInfo {
    std::string variableName;  // Variable name holding the lambda
    std::vector<Parser::TemplateParameter> templateParams;  // COPIED from AST
    Parser::LambdaExpr* astNode = nullptr;  // Optional: only valid for same-module access
    std::unordered_map<std::string, std::string> capturedVarTypes;  // varName -> type (e.g., "Integer^")
};

// Callback parameter info for FFI callbacks
struct CallbackParamInfo {
    std::string name;
    std::string typeName;
    Parser::OwnershipType ownership;
};

// Callback type info for FFI callbacks
struct CallbackTypeInfo {
    std::string name;
    std::string qualifiedName;
    Parser::CallingConvention convention;
    std::string returnType;
    Parser::OwnershipType returnOwnership;
    std::vector<CallbackParamInfo> parameters;
    Parser::CallbackTypeDecl* astNode = nullptr;
};

//==============================================================================
// TYPE RESOLUTION PASS RESULTS
//==============================================================================

// Represents a fully qualified, canonicalized type
struct QualifiedType {
    std::string qualifiedName;      // e.g., "Language::Core::Integer"
    std::string simpleName;         // e.g., "Integer"
    Parser::OwnershipType ownership;
    bool isTemplate;
    bool isPrimitive;               // Integer, Float, Double, Bool, String
    bool isNativeType;              // NativeType<X>
    std::vector<QualifiedType> templateArgs;

    bool isResolved() const { return !qualifiedName.empty(); }
};

// Tracks unresolved forward references
struct ForwardReference {
    std::string typeName;
    Common::SourceLocation location;
    std::string referencedFrom;     // Class/method that references this type
};

// Result from Type Resolution Pass
struct TypeResolutionResult {
    std::unordered_map<std::string, QualifiedType> resolvedTypes;
    std::set<std::string> validNamespaces;
    std::vector<ForwardReference> unresolvedReferences;
    bool success = false;

    const QualifiedType* lookupType(const std::string& name) const {
        auto it = resolvedTypes.find(name);
        return it != resolvedTypes.end() ? &it->second : nullptr;
    }

    bool hasUnresolvedTypes() const { return !unresolvedReferences.empty(); }
};

//==============================================================================
// TEMPLATE EXPANSION PASS RESULTS
//==============================================================================

// Proof that a constraint was satisfied (or not)
struct ConstraintProof {
    std::string constraintName;
    std::vector<std::string> typeArgs;
    bool satisfied = false;
    std::string failureReason;
    Common::SourceLocation location;
};

// An instantiated template class
struct InstantiatedClass {
    std::string originalName;       // e.g., "List"
    std::string mangledName;        // e.g., "List_Integer"
    std::vector<std::pair<std::string, std::string>> substitutions;  // T -> Integer
    Parser::ClassDecl* expandedAST = nullptr;  // Raw pointer - ownership managed externally
    std::vector<ConstraintProof> constraintProofs;
    bool valid = false;
};

// An instantiated template method
struct InstantiatedMethod {
    std::string className;          // Base class
    std::string instantiatedClassName;  // After class template substitution
    std::string methodName;
    std::string mangledName;
    std::vector<std::pair<std::string, std::string>> substitutions;
    Parser::MethodDecl* expandedAST = nullptr;  // Raw pointer - ownership managed externally
    std::vector<ConstraintProof> constraintProofs;
    bool valid = false;
};

// An instantiated template lambda
struct InstantiatedLambda {
    std::string variableName;
    std::string mangledName;
    std::vector<std::pair<std::string, std::string>> substitutions;
    Parser::LambdaExpr* expandedAST = nullptr;  // Raw pointer - ownership managed externally
    bool valid = false;
};

// Result from Template Expansion Pass
struct TemplateExpansionResult {
    std::vector<InstantiatedClass> instantiatedClasses;
    std::vector<InstantiatedMethod> instantiatedMethods;
    std::vector<InstantiatedLambda> instantiatedLambdas;
    std::unordered_map<std::string, std::string> allSubstitutions;  // Global substitution map
    bool success = false;

    const InstantiatedClass* findClass(const std::string& mangledName) const {
        for (const auto& c : instantiatedClasses) {
            if (c.mangledName == mangledName) return &c;
        }
        return nullptr;
    }
};

//==============================================================================
// SEMANTIC VALIDATION PASS RESULTS
//==============================================================================

// Resolved type information for an expression
struct ResolvedExprType {
    std::string typeName;
    Parser::OwnershipType ownership;
    bool isLValue = false;
    bool isTemporary = false;
};

// Result from Semantic Validation Pass
struct SemanticValidationResult {
    std::unordered_map<Parser::Expression*, ResolvedExprType> expressionTypes;
    std::unordered_map<std::string, ClassInfo*> classRegistry;
    std::unordered_map<std::string, MethodInfo*> methodRegistry;
    std::vector<std::string> errors;
    bool success = false;
};

//==============================================================================
// OWNERSHIP ANALYSIS PASS RESULTS
//==============================================================================

// State of a variable's ownership at a program point
enum class OwnershipState {
    Owned,      // Variable holds owned value
    Moved,      // Value has been moved out
    Borrowed,   // Being borrowed (reference exists)
    Invalid     // Invalid state (error)
};

// Tracks ownership violations
struct OwnershipViolation {
    enum class Kind {
        UseAfterMove,
        DoubleMoveViolation,
        DanglingReference,
        InvalidCapture,
        BorrowWhileMoved
    };

    Kind kind;
    std::string variableName;
    Common::SourceLocation useLocation;
    Common::SourceLocation moveLocation;  // Where it was moved (if applicable)
    std::string message;
};

// Lifetime information for a reference
struct LifetimeInfo {
    int scopeLevel;
    std::string targetVariable;     // What it references
    int targetScopeLevel;           // Scope of the target
    bool isValid = true;
};

// Result from Ownership Analysis Pass
struct OwnershipAnalysisResult {
    std::unordered_map<std::string, OwnershipState> variableStates;
    std::unordered_map<Parser::Expression*, LifetimeInfo> lifetimes;
    std::vector<OwnershipViolation> violations;
    bool success = false;

    bool hasViolations() const { return !violations.empty(); }
};

//==============================================================================
// LAYOUT COMPUTATION PASS RESULTS
//==============================================================================

// Layout information for a single field
struct FieldLayout {
    std::string name;
    std::string typeName;
    size_t offset;          // Byte offset from struct start
    size_t size;            // Size in bytes
    size_t alignment;       // Alignment requirement
    Parser::OwnershipType ownership;
};

// Computed layout for a class
struct ClassLayout {
    std::string className;
    std::string qualifiedName;
    std::vector<FieldLayout> fields;
    size_t totalSize;       // Total size including padding
    size_t alignment;       // Overall alignment requirement
    bool isPacked = false;
    bool hasBaseClass = false;
    std::string baseClassName;
    size_t baseClassSize = 0;
};

// Reflection metadata for a class
struct ReflectionMetadataInfo {
    std::string fullName;
    std::string namespaceName;
    bool isTemplate = false;
    std::vector<std::string> templateParams;
    std::vector<std::pair<std::string, std::string>> properties;  // (name, type)
    std::vector<std::pair<std::string, std::string>> methods;     // (name, returnType)
    int64_t instanceSize;
};

// Result from Layout Computation Pass
struct LayoutComputationResult {
    std::unordered_map<std::string, ClassLayout> layouts;
    std::unordered_map<std::string, ReflectionMetadataInfo> metadata;
    bool success = false;

    const ClassLayout* getLayout(const std::string& className) const {
        auto it = layouts.find(className);
        return it != layouts.end() ? &it->second : nullptr;
    }
};

//==============================================================================
// ABI LOWERING PASS RESULTS
//==============================================================================

// Strategy for marshaling a value across FFI boundary
enum class MarshalStrategy {
    None,           // Direct pass (no conversion)
    StringToC,      // XXML String -> C string (char*)
    CToString,      // C string -> XXML String
    IntToI64,       // XXML Integer -> i64
    I64ToInt,       // i64 -> XXML Integer
    FloatToC,       // XXML Float -> C float
    CToFloat,       // C float -> XXML Float
    DoubleToC,      // XXML Double -> C double
    CToDouble,      // C double -> XXML Double
    BoolToI1,       // XXML Bool -> i1
    I1ToBool,       // i1 -> XXML Bool
    ObjectToPtr,    // XXML object -> opaque pointer
    PtrToObject,    // opaque pointer -> XXML object
    CallbackThunk   // XXML closure -> C function pointer
};

// Information about a native parameter
struct NativeParamInfo {
    std::string paramName;
    std::string xxmlType;       // Original XXML type
    std::string llvmType;       // Target LLVM type (i64, float, ptr, etc.)
    MarshalStrategy marshal;
    bool isPointer = false;
    bool isCallback = false;
    Parser::OwnershipType ownership;
};

// Information about a native return type
struct NativeReturnInfo {
    std::string xxmlType;
    std::string llvmType;
    MarshalStrategy marshal;
    bool isVoid = false;
};

// Fully lowered FFI signature
struct LoweredSignature {
    std::string functionName;
    std::string mangledName;
    std::string dllPath;
    std::string symbolName;
    Parser::CallingConvention convention;
    std::vector<NativeParamInfo> parameters;
    NativeReturnInfo returnInfo;
    bool requiresMarshaling = false;
    bool isValid = true;
};

// Callback thunk information
struct CallbackThunkInfo {
    std::string thunkName;
    std::string callbackTypeName;
    Parser::CallingConvention convention;
    std::string returnType;
    std::vector<std::string> paramTypes;
};

// Result from ABI Lowering Pass
struct ABILoweringResult {
    std::unordered_map<std::string, LoweredSignature> nativeMethods;
    std::unordered_map<std::string, CallbackThunkInfo> callbackThunks;
    bool success = false;

    const LoweredSignature* getSignature(const std::string& methodName) const {
        auto it = nativeMethods.find(methodName);
        return it != nativeMethods.end() ? &it->second : nullptr;
    }
};

//==============================================================================
// COMBINED PASS RESULTS (for passing through pipeline)
//==============================================================================

struct CompilationPassResults {
    TypeResolutionResult typeResolution;
    TemplateExpansionResult templateExpansion;
    SemanticValidationResult semanticValidation;
    OwnershipAnalysisResult ownershipAnalysis;
    LayoutComputationResult layoutComputation;
    ABILoweringResult abiLowering;

    bool allSuccessful() const {
        return typeResolution.success &&
               templateExpansion.success &&
               semanticValidation.success &&
               ownershipAnalysis.success &&
               layoutComputation.success &&
               abiLowering.success;
    }
};

} // namespace Semantic
} // namespace XXML
