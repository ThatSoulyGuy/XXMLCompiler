#pragma once

#include "../Core/IBackend.h"
#include "../Parser/AST.h"
#include <sstream>
#include <unordered_map>
#include <set>

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

    // ASTVisitor interface - simplified for IR generation
    void visit(Parser::Program& node) override;
    void visit(Parser::ImportDecl& node) override;
    void visit(Parser::NamespaceDecl& node) override;
    void visit(Parser::ClassDecl& node) override;
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
    void visit(Parser::TypeRef& node) override;

private:
    std::stringstream output_;
    std::unordered_map<std::string, std::string> valueMap_;  // XXML var -> LLVM register
    std::unordered_map<std::string, std::string> registerTypes_;  // LLVM register -> type name
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    // Semantic analyzer for template instantiation
    Semantic::SemanticAnalyzer* semanticAnalyzer_ = nullptr;

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

    std::string allocateRegister();
    std::string allocateLabel(std::string_view prefix);
    void emitLine(const std::string& line);

    /// Get target triple for current platform
    std::string getTargetTriple() const;

    /// Get data layout for current platform
    std::string getDataLayout() const;

    /// Generate qualified function name (Class_Method format)
    std::string getQualifiedName(const std::string& className, const std::string& methodName) const;

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
};

} // namespace Backends
} // namespace XXML
