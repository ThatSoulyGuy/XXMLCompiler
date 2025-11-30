#pragma once

#include "../Core/IBackend.h"
#include "../Parser/AST.h"
#include "IR/IR.h"  // New IR infrastructure
#include <sstream>
#include <unordered_map>
#include <set>
#include <memory>

namespace XXML {

namespace Core { class CompilationContext; }
namespace Semantic { class SemanticAnalyzer; }

namespace Backends {

/**
 * @brief LLVM IR code generation backend
 *
 * Generates LLVM Intermediate Representation for the XXML AST.
 * This enables:
 * - Better optimization (LLVM's optimization passes)
 * - Multiple target architectures (x86, ARM, WebAssembly, etc.)
 * - JIT compilation
 * - Link-time optimization
 *
 * Example LLVM IR output:
 * @code
 * define i64 @add(i64 %a, i64 %b) {
 *   %result = add i64 %a, %b
 *   ret i64 %result
 * }
 * @endcode
 */
class LLVMBackend : public Core::BackendBase, public Parser::ASTVisitor {
public:
    explicit LLVMBackend(Core::CompilationContext* context = nullptr);
    ~LLVMBackend() override = default;

    // IBackend interface
    std::string targetName() const override { return "LLVM IR"; }
    Core::BackendTarget targetType() const override { return Core::BackendTarget::LLVM_IR; }
    std::string version() const override { return "1.0.0 (LLVM 17)"; }

    bool supportsFeature(std::string_view feature) const override;

    void initialize(Core::CompilationContext& context) override;

    // Semantic analyzer integration (for template instantiation)
    void setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) {
        semanticAnalyzer_ = analyzer;
    }

    // Set imported modules for code generation
    // These modules' classes will have their code generated before the main program
    void setImportedModules(const std::vector<Parser::Program*>& modules) {
        importedModules_ = modules;
        // Clear names when using old API
        importedModuleNames_.clear();
    }

    // Set imported modules with their names for proper namespace handling
    // Modules without explicit namespace wrappers will use the module name as namespace
    void setImportedModulesWithNames(const std::vector<std::pair<std::string, Parser::Program*>>& modules) {
        importedModules_.clear();
        importedModuleNames_.clear();
        for (const auto& p : modules) {
            importedModules_.push_back(p.second);
            importedModuleNames_.push_back(p.first);
        }
    }

    // Set processor compilation mode (compiles annotation processor to DLL)
    void setProcessorMode(bool enabled, const std::string& annotationName = "") {
        processorMode_ = enabled;
        processorAnnotationName_ = annotationName;
    }

    // Collect reflection metadata from a module without generating code
    // This is needed for runtime modules where we want reflection info but not code
    void collectReflectionMetadataFromModule(Parser::Program& program);

    std::string generate(Parser::Program& program) override;
    std::string generateHeader(Parser::Program& program) override;
    std::string generateImplementation(Parser::Program& program) override;

    std::string generatePreamble() override;
    void initializeIRModule();  // Initialize IR module with built-in types and runtime declarations
    std::vector<std::string> getRequiredIncludes() const override;
    std::vector<std::string> getRequiredLibraries() const override;

    std::string convertType(std::string_view xxmlType) const override;
    std::string convertOwnership(std::string_view type,
                                std::string_view ownershipIndicator) const override;

    // Object file generation (new for complete toolchain)
    /**
     * Generate LLVM object file (.o/.obj) from IR code
     * @param irCode LLVM IR code (text format)
     * @param outputPath Output object file path
     * @param optimizationLevel Optimization level (0-3)
     * @return true if successful, false otherwise
     */
    bool generateObjectFile(const std::string& irCode,
                           const std::string& outputPath,
                           int optimizationLevel = 0);

    // ASTVisitor interface - simplified for IR generation
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

    void visit(Parser::InstantiateStmt& node) override;
    void visit(Parser::AssignmentStmt& node) override;
    void visit(Parser::RunStmt& node) override;
    void visit(Parser::ForStmt& node) override;
    void visit(Parser::ExitStmt& node) override;
    void visit(Parser::ReturnStmt& node) override;
    void visit(Parser::IfStmt& node) override;
    void visit(Parser::WhileStmt& node) override;
    void visit(Parser::BreakStmt& node) override;
    void visit(Parser::ContinueStmt& node) override;
    void visit(Parser::RequireStmt& node) override;
    void visit(Parser::ConstraintDecl& node) override;
    void visit(Parser::AnnotateDecl& node) override;
    void visit(Parser::ProcessorDecl& node) override;
    void visit(Parser::AnnotationDecl& node) override;
    void visit(Parser::AnnotationUsage& node) override;

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

private:
    // === New IR Infrastructure ===
    std::unique_ptr<IR::Module> module_;           // IR Module being built
    std::unique_ptr<IR::IRBuilder> builder_;       // IR instruction builder
    IR::Function* currentFunction_ = nullptr;      // Current function being generated
    IR::BasicBlock* currentBlock_ = nullptr;       // Current basic block

    // Value tracking: XXML variable name -> IR Value
    std::unordered_map<std::string, IR::Value*> irValues_;

    // Alloca tracking for local variables (name -> alloca instruction)
    std::unordered_map<std::string, IR::AllocaInst*> localAllocas_;

    // Expression result tracking (set by expression visitors, consumed by statement visitors)
    IR::Value* lastExprValue_ = nullptr;

    // Flag to determine whether to use new IR or legacy string generation
    bool useNewIR_ = true;  // Enabled for type-safe IR generation

    // === Legacy members (being phased out) ===
    std::stringstream output_;
    std::unordered_map<std::string, std::string> valueMap_;  // XXML var -> LLVM register
    std::unordered_map<std::string, std::string> registerTypes_;  // LLVM register -> type name
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    // Semantic analyzer for template instantiation
    Semantic::SemanticAnalyzer* semanticAnalyzer_ = nullptr;

    // Imported modules for code generation
    std::vector<Parser::Program*> importedModules_;
    std::vector<std::string> importedModuleNames_;  // Module names for namespace handling

    // Processor compilation mode (for compiling annotation processors to DLLs)
    bool processorMode_ = false;
    std::string processorAnnotationName_;

    // Track declared functions to avoid duplicates
    std::set<std::string> declaredFunctions_;

    // Track generated (defined) functions to avoid duplicate definitions
    std::set<std::string> generatedFunctions_;

    // Track generated classes to avoid duplicate class definitions
    std::set<std::string> generatedClasses_;

    // Target platform support
    enum class TargetPlatform {
        X86_64_Windows,     // x86-64 Windows (MSVC ABI)
        X86_64_Linux,       // x86-64 Linux (System V ABI)
        X86_64_MacOS,       // x86-64 macOS
        ARM64_Linux,        // ARM64/AArch64 Linux
        ARM64_MacOS,        // ARM64 macOS (Apple Silicon)
        WebAssembly,        // WebAssembly (wasm32)
        Native              // Use host platform
    };
    TargetPlatform targetPlatform_ = TargetPlatform::Native;

    // Loop label stack for break/continue
    struct LoopLabels {
        std::string condLabel;
        std::string endLabel;
        // IR basic blocks for break/continue
        IR::BasicBlock* condBlock = nullptr;
        IR::BasicBlock* endBlock = nullptr;
    };
    std::vector<LoopLabels> loopStack_;

    // Class context tracking
    std::string currentNamespace_;  // Track current namespace
    std::string currentClassName_;
    std::string currentFunctionReturnType_;  // Track current function's return type for return statements
    struct ClassInfo {
        std::vector<std::pair<std::string, std::string>> properties;  // name, type
    };
    std::unordered_map<std::string, ClassInfo> classes_;

    // Ownership tracking
    enum class OwnershipKind {
        Owned,      // ^ - exclusive ownership, moveable
        Reference,  // & - borrowed reference, no ownership
        Value       // % - value semantics, copyable
    };

    struct VariableInfo {
        std::string llvmRegister;  // LLVM register/pointer
        std::string type;          // XXML type name
        std::string llvmType;      // LLVM type (i64, ptr, etc.)
        OwnershipKind ownership;   // Ownership semantics
        bool isMovedFrom;          // Track if value has been moved
    };
    std::unordered_map<std::string, VariableInfo> variables_;

    // Template instantiation tracking
    std::set<std::string> generatedTemplateInstantiations_;  // Track generated template types

    // String literal storage
    std::vector<std::pair<std::string, std::string>> stringLiterals_;  // label -> content

    // Lambda/closure tracking
    int lambdaCounter_ = 0;  // Unique ID for each lambda
    struct LambdaInfo {
        std::string closureTypeName;       // e.g., %closure.0
        std::string functionName;          // e.g., @lambda.0
        std::string returnType;            // LLVM return type
        std::vector<std::string> paramTypes;  // LLVM param types
        std::vector<std::pair<std::string, Parser::LambdaExpr::CaptureMode>> captures;  // varName, mode
    };
    std::unordered_map<std::string, LambdaInfo> lambdaInfos_;  // closure register -> info
    std::vector<std::string> pendingLambdaDefinitions_;  // Lambda function definitions to emit later

    // Native FFI method tracking for string marshalling and callback support
    // Maps function name -> list of parameter types (for detecting FFI calls)
    struct NativeMethodInfo {
        std::vector<std::string> paramTypes;      // LLVM types for parameters
        std::vector<std::string> xxmlParamTypes;  // Original XXML types (for callback detection)
        std::vector<bool> isStringPtr;            // True if param expects C string (ptr)
        std::vector<bool> isCallback;             // True if param is a callback type
        std::string returnType;                   // LLVM return type (e.g., "i32", "ptr", "void")
        std::string xxmlReturnType;               // Original XXML return type for tracking
    };
    std::unordered_map<std::string, NativeMethodInfo> nativeMethods_;

    // Enumeration tracking: EnumName::ValueName -> int64_t value
    std::unordered_map<std::string, int64_t> enumValues_;  // "EnumName::VALUE" -> value

    // Callback type tracking for FFI callbacks
    struct CallbackThunkInfo {
        std::string callbackTypeName;           // XXML callback type name (e.g., "GLFW::ErrorCallback")
        std::string thunkFunctionName;          // Generated thunk function name
        Parser::CallingConvention convention;   // Calling convention
        std::string returnLLVMType;             // LLVM return type (e.g., "void", "i32")
        std::vector<std::string> paramLLVMTypes;  // LLVM parameter types
        std::vector<std::string> paramNames;      // Parameter names for documentation
    };
    std::unordered_map<std::string, CallbackThunkInfo> callbackThunks_;  // callback type name -> thunk info
    int callbackThunkCounter_ = 0;  // Unique ID for callback thunks

    // Maps lambda/closure register -> callback type name (for thunk lookup when passing to FFI)
    std::unordered_map<std::string, std::string> lambdaCallbackTypes_;

    std::string allocateRegister();
    std::string allocateLabel(std::string_view prefix);
    void emitLine(const std::string& line);

    /// Get target triple for current platform
    std::string getTargetTriple() const;

    /// Get data layout for current platform
    std::string getDataLayout() const;

    /// Generate qualified function name (Class_Method format)
    std::string getQualifiedName(const std::string& className, const std::string& methodName) const;

    /// Generate a native FFI method thunk that loads/calls a DLL function
    void generateNativeMethodThunk(Parser::MethodDecl& node);

    /// Generate callback thunk function for passing XXML lambda to native code
    void generateCallbackThunk(const std::string& callbackTypeName);

    /// Get calling convention string for LLVM IR
    std::string getLLVMCallingConvention(Parser::CallingConvention conv) const;

    /// Qualify a type name with the current namespace if needed
    /// (e.g., "Cursor^" -> "GLFW::Cursor^" when inside GLFW namespace)
    std::string qualifyTypeName(const std::string& typeName) const;

    /// Map XXML types to LLVM types using TypeRegistry
    std::string getLLVMType(const std::string& xxmlType) const;

    /// Get default/zero value for an LLVM type (null for pointers, 0 for integers, etc.)
    std::string getDefaultValueForType(const std::string& llvmType) const;

    /// Ownership helper methods
    OwnershipKind parseOwnership(char ownershipChar) const;
    void registerVariable(const std::string& name, const std::string& type,
                         const std::string& llvmReg, OwnershipKind ownership);
    bool checkAndMarkMoved(const std::string& varName);
    void emitDestructor(const std::string& varName);

    /// Template helper methods
    std::string mangleTemplateName(const std::string& baseName,
                                   const std::vector<std::string>& typeArgs) const;
    std::string getFullTypeName(const Parser::TypeRef& typeRef) const;

    /// Extract type name from an expression (for static method calls like Class::Constructor)
    std::string extractTypeName(const Parser::Expression* expr) const;

    /// Template instantiation (monomorphization)
    void generateTemplateInstantiations();

    /// Forward declare all user-defined functions before code generation
    void generateFunctionDeclarations(Parser::Program& program);

    /// Generate code for imported modules (safely processes classes without entrypoints)
    void generateImportedModuleCode(Parser::Program& program, const std::string& moduleName = "");
    std::unique_ptr<Parser::ClassDecl> cloneAndSubstituteClassDecl(
        Parser::ClassDecl* original,
        const std::string& newName,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// AST cloning helpers
    std::unique_ptr<Parser::TypeRef> cloneTypeRef(
        const Parser::TypeRef* original,
        const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::MethodDecl> cloneMethodDecl(
        const Parser::MethodDecl* original,
        const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::PropertyDecl> clonePropertyDecl(
        const Parser::PropertyDecl* original,
        const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::Statement> cloneStatement(
        const Parser::Statement* original,
        const std::unordered_map<std::string, std::string>& typeMap);
    std::unique_ptr<Parser::Expression> cloneExpression(
        const Parser::Expression* original,
        const std::unordered_map<std::string, std::string>& typeMap);

    /// Generate LLVM instruction for binary operation using OperatorRegistry
    std::string generateBinaryOp(const std::string& op,
                                const std::string& lhs,
                                const std::string& rhs,
                                const std::string& type);

    /// Calculate size in bytes for a given class
    size_t calculateClassSize(const std::string& className) const;

    // === IR Infrastructure Helpers ===

    /// Convert XXML type to IR Type
    IR::Type* getIRType(const std::string& xxmlType);

    /// Get or create IR struct type for a class
    IR::StructType* getOrCreateClassType(const std::string& className);

    /// Create function in IR module
    IR::Function* createIRFunction(const std::string& name, IR::Type* returnType,
                                   const std::vector<std::pair<std::string, IR::Type*>>& params);

    /// Generate IR for an expression, returning the IR Value
    IR::Value* generateExprIR(Parser::Expression* expr);

    /// Generate IR for a statement
    void generateStmtIR(Parser::Statement* stmt);

    /// Get IR value for a variable (load if needed)
    IR::Value* getIRValue(const std::string& name);

    /// Store IR value to a variable
    void storeIRValue(const std::string& name, IR::Value* value);

    /// Reflection metadata generation
    void generateReflectionMetadata();

    /// Processor entry point generation (for --processor mode)
    void generateProcessorEntryPoints(Parser::Program& program);

    struct ReflectionClassMetadata {
        std::string name;
        std::string namespaceName;
        std::string fullName;
        std::vector<std::pair<std::string, std::string>> properties;  // name, type
        std::vector<std::string> propertyOwnerships;  // ownership chars (^, &, %)
        std::vector<std::pair<std::string, std::string>> methods;  // name, return type
        std::vector<std::string> methodReturnOwnerships;  // ownership for return types
        std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> methodParameters;  // name, type, ownership
        bool isTemplate;
        std::vector<std::string> templateParams;
        size_t instanceSize;
        Parser::ClassDecl* astNode;
    };
    std::unordered_map<std::string, ReflectionClassMetadata> reflectionMetadata_;

    // === Annotation Code Generation ===

    // Track annotation definitions that have Retain keyword
    std::set<std::string> retainedAnnotations_;

    // Pending annotation metadata for retained annotations
    struct AnnotationArgValue {
        enum Kind { Integer, String, Bool, Float, Double } kind;
        int64_t intValue = 0;
        std::string stringValue;
        bool boolValue = false;
        float floatValue = 0.0f;
        double doubleValue = 0.0;
    };

    struct PendingAnnotationMetadata {
        std::string annotationName;
        std::string targetType;      // "type", "method", or "property"
        std::string typeName;        // Class name
        std::string memberName;      // Method/property name (empty for type-level)
        std::vector<std::pair<std::string, AnnotationArgValue>> arguments;
    };
    std::vector<PendingAnnotationMetadata> pendingAnnotationMetadata_;

    // Counter for unique annotation metadata globals
    int annotationMetadataCounter_ = 0;

    // Helper methods for annotation code generation
    void generateAnnotationMetadata();
    void collectRetainedAnnotations(Parser::ClassDecl& node);
    void collectRetainedAnnotations(Parser::MethodDecl& node, const std::string& className);
    void collectRetainedAnnotations(Parser::PropertyDecl& node, const std::string& className);
    AnnotationArgValue evaluateAnnotationArg(Parser::Expression* expr);
    bool isAnnotationRetained(const std::string& annotationName) const;
};

} // namespace Backends
} // namespace XXML
