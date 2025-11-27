#pragma once

#include "Backends/IR/Module.h"
#include <string>
#include <vector>
#include <set>
#include <functional>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// VerifierError - Detailed error information
// ============================================================================

struct VerifierError {
    enum class Severity {
        Error,      // Must be fixed, IR is invalid
        Warning,    // Potentially problematic but IR is valid
        Note        // Additional information
    };

    enum class Category {
        Type,           // Type mismatch or invalid type
        SSA,            // SSA form violation
        Terminator,     // Missing or invalid terminator
        CFG,            // Control flow graph issues
        Dominance,      // Dominance property violation
        Operand,        // Invalid operand
        Function,       // Function-level issues
        Module,         // Module-level issues
        Instruction,    // Invalid instruction
        Memory          // Memory access issues
    };

    Severity severity;
    Category category;
    std::string message;

    // Location information
    const Function* function = nullptr;
    const BasicBlock* block = nullptr;
    const Instruction* instruction = nullptr;
    const Value* value = nullptr;

    VerifierError(Severity sev, Category cat, const std::string& msg)
        : severity(sev), category(cat), message(msg) {}

    std::string toString() const;
};

// ============================================================================
// Verifier - Comprehensive IR validation
// ============================================================================

class Verifier {
public:
    explicit Verifier(const Module& module);

    // Run all verification passes
    bool verify();

    // Run specific passes
    bool verifyModule();
    bool verifyFunction(const Function* func);
    bool verifyBasicBlock(const BasicBlock* bb, const Function* func);
    bool verifyInstruction(const Instruction* inst, const BasicBlock* bb);

    // Get errors
    const std::vector<VerifierError>& getErrors() const { return errors_; }
    bool hasErrors() const;
    bool hasWarnings() const;

    // Error output
    std::string getErrorReport() const;
    void printErrors() const;

    // Configuration
    void setStrictMode(bool strict) { strictMode_ = strict; }
    void setCheckDominance(bool check) { checkDominance_ = check; }
    void setCheckUseDefChains(bool check) { checkUseDefChains_ = check; }

private:
    // Error reporting
    void addError(VerifierError::Severity sev, VerifierError::Category cat,
                  const std::string& msg);
    void addError(VerifierError::Severity sev, VerifierError::Category cat,
                  const std::string& msg, const Function* func);
    void addError(VerifierError::Severity sev, VerifierError::Category cat,
                  const std::string& msg, const Function* func, const BasicBlock* bb);
    void addError(VerifierError::Severity sev, VerifierError::Category cat,
                  const std::string& msg, const Function* func, const BasicBlock* bb,
                  const Instruction* inst);

    // Module-level verification
    bool verifyGlobalVariables();
    bool verifyStructTypes();
    bool verifyFunctionDeclarations();

    // Function-level verification
    bool verifyFunctionSignature(const Function* func);
    bool verifyFunctionBody(const Function* func);
    bool verifyEntryBlock(const Function* func);
    bool verifyReturnPaths(const Function* func);

    // Block-level verification
    bool verifyBlockTerminator(const BasicBlock* bb, const Function* func);
    bool verifyBlockPredecessors(const BasicBlock* bb, const Function* func);
    bool verifyBlockSuccessors(const BasicBlock* bb, const Function* func);
    bool verifyPhiNodes(const BasicBlock* bb, const Function* func);

    // Instruction verification
    bool verifyInstructionOperands(const Instruction* inst);
    bool verifyInstructionType(const Instruction* inst);

    // Specific instruction verification
    bool verifyAlloca(const AllocaInst* inst);
    bool verifyLoad(const LoadInst* inst);
    bool verifyStore(const StoreInst* inst);
    bool verifyGEP(const GetElementPtrInst* inst);
    bool verifyBinaryOp(const BinaryOperator* inst);
    bool verifyICmp(const ICmpInst* inst);
    bool verifyFCmp(const FCmpInst* inst);
    bool verifyCast(const CastInst* inst);
    bool verifyBranch(const BranchInst* inst);
    bool verifySwitch(const SwitchInst* inst);
    bool verifyReturn(const ReturnInst* inst);
    bool verifyPHI(const PHINode* inst, const BasicBlock* bb);
    bool verifyCall(const CallInst* inst);
    bool verifySelect(const SelectInst* inst);

    // SSA verification
    bool verifySSAProperty(const Function* func);
    bool verifyValueDominance(const Function* func);
    bool verifySingleDefinition(const Function* func);

    // Use-def chain verification
    bool verifyUseDefChains(const Function* func);

    // Type helpers
    bool isIntegerType(const Type* ty) const;
    bool isFloatingPointType(const Type* ty) const;
    bool isPointerType(const Type* ty) const;
    bool isAggregateType(const Type* ty) const;
    bool typesMatch(const Type* a, const Type* b) const;
    std::string typeToString(const Type* ty) const;

    const Module& module_;
    std::vector<VerifierError> errors_;

    // Current context for error reporting
    const Function* currentFunction_ = nullptr;
    const BasicBlock* currentBlock_ = nullptr;

    // Configuration
    bool strictMode_ = true;
    bool checkDominance_ = true;
    bool checkUseDefChains_ = true;

    // Tracking for verification
    std::set<const Value*> definedValues_;
    std::set<const BasicBlock*> visitedBlocks_;
};

// ============================================================================
// Utility functions
// ============================================================================

// Quick verification (returns true if valid)
bool verifyModule(const Module& module);
bool verifyModule(const Module& module, std::string* errorMessage);

// Verify and throw on error
void verifyModuleOrThrow(const Module& module);

} // namespace IR
} // namespace Backends
} // namespace XXML
