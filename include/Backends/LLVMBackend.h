#pragma once

#include "../Core/IBackend.h"
#include "../Parser/AST.h"
#include "Codegen/ModularCodegen.h"  // Modular code generation
#include "Codegen/Preamble/PreambleGen.h"  // Preamble generation
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <memory>

namespace XXML {

namespace Core { class CompilationContext; }
namespace Semantic {
    class SemanticAnalyzer;
    class CompiletimeValue;
    class CompiletimeInterpreter;
}

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
class LLVMBackend : public Core::BackendBase {
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
        // Also set on modular codegen for type resolution
        if (modularCodegen_) {
            modularCodegen_->setSemanticAnalyzer(analyzer);
        }
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


    std::string generate(Parser::Program& program) override;
    std::string generateHeader(Parser::Program& program) override;
    std::string generateImplementation(Parser::Program& program) override;

    std::string generatePreamble() override;
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

private:
    // === Modular Codegen (type-safe IR system) ===
    std::unique_ptr<Codegen::ModularCodegen> modularCodegen_;

    // === Legacy members (still needed for template instantiation) ===
    std::stringstream output_;  // Accumulates template instantiation IR
    std::unordered_map<std::string, std::string> valueMap_;  // XXML var -> LLVM register
    std::unordered_map<std::string, std::string> registerTypes_;  // LLVM register -> type name
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    // Compile-time value tracking: variable name -> compile-time evaluated value
    // Variables in this map are constants that don't need runtime allocation
    std::unordered_map<std::string, std::unique_ptr<Semantic::CompiletimeValue>> compiletimeValues_;

    // Cache for materialized compile-time values: variable name -> LLVM register holding the object
    // This ensures each compile-time value is only constructed once at runtime
    std::unordered_map<std::string, std::string> compiletimeMaterialized_;

    // Track NativeType compile-time variables (always emit raw values, never wrap)
    std::unordered_set<std::string> compiletimeNativeTypes_;

    // Value context tracking for compile-time constant folding
    // Controls whether to emit raw constants or wrapper objects
    enum class ValueContext {
        Default,        // Needs wrapper object (e.g., for method calls that can't be evaluated at compile-time)
        RawValue,       // Can use raw primitive (i64, float, etc.) - for arithmetic operands
        MethodReceiver, // Method call - try compile-time eval first
        OperandContext  // Binary operation operand
    };
    ValueContext currentValueContext_ = ValueContext::Default;

    // Global string constant pool for compile-time string values
    std::unordered_map<std::string, std::string> globalStringConstants_;  // content -> label
    size_t stringConstantCounter_ = 0;  // Dedicated counter for unique string labels

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

    // Target platform support (use PreambleGen's enum)
    using TargetPlatform = Codegen::TargetPlatform;
    TargetPlatform targetPlatform_ = TargetPlatform::Native;

    // Preamble generator
    std::unique_ptr<Codegen::PreambleGen> preambleGen_;

    // Loop label stack for break/continue
    struct LoopLabels {
        std::string condLabel;
        std::string endLabel;
    };
    std::vector<LoopLabels> loopStack_;

    // Class context tracking
    std::string currentNamespace_;  // Track current namespace
    std::string currentClassName_;
    std::string currentFunctionReturnType_;  // Track current function's return type for return statements
    struct ClassInfo {
        // Properties: (name, llvmType, xxmlType)
        std::vector<std::tuple<std::string, std::string, std::string>> properties;
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

    // Legacy helper methods (still needed for template instantiation)
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
    std::string getFullTypeName(const Parser::TypeRef& typeRef) const;

    /// Extract type name from an expression (for static method calls like Class::Constructor)
    std::string extractTypeName(const Parser::Expression* expr) const;

    /// Forward declare all user-defined functions before code generation
    void generateFunctionDeclarations(Parser::Program& program);

    /// Generate LLVM instruction for binary operation using OperatorRegistry
    std::string generateBinaryOp(const std::string& op,
                                const std::string& lhs,
                                const std::string& rhs,
                                const std::string& type);

    /// Calculate size in bytes for a given class
    size_t calculateClassSize(const std::string& className) const;

    /// Get or create a global string constant, returning its label
    /// Deduplicates identical strings to minimize code size
    std::string getOrCreateGlobalString(const std::string& content);

    /// Processor entry point generation (for --processor mode)
    void generateProcessorEntryPoints(Parser::Program& program);
};

} // namespace Backends
} // namespace XXML
