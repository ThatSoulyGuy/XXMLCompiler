#pragma once

#include "TypedModule.h"
#include "TypedInstructions.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iostream>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// Forward declarations
class ValueTracker;

/**
 * Verification error kinds - categorized by level
 */
enum class VerificationErrorKind {
    // Instruction-level errors
    NullOperand,
    TypeMismatch,
    InstructionAfterTerminator,
    InvalidOpcode,

    // Function-level errors
    MissingTerminator,
    MultipleTerminators,
    UnreachableBlock,
    UndefinedValue,
    DominanceViolation,
    AllocaNotInEntry,
    PHIMissingPredecessor,
    PHITypeMismatch,
    ReturnTypeMismatch,

    // Module-level errors
    DuplicateFunctionName,
    DuplicateGlobalName,
    UnresolvedCall,
    TypeInconsistency
};

/**
 * Represents a single verification error with context
 */
struct VerificationError {
    VerificationErrorKind kind;
    std::string message;
    const Instruction* instruction = nullptr;
    const BasicBlock* block = nullptr;
    const Function* function = nullptr;
    const Value* relatedValue = nullptr;

    std::string toString() const;
};

/**
 * IRVerifier - Comprehensive LLVM IR verification engine
 *
 * Provides three levels of verification:
 * 1. Per-instruction: Called immediately after each instruction is created
 * 2. Per-function: Called after function codegen completes
 * 3. Per-module: Called before emission
 *
 * On verification failure with abortOnFailure=true, prints detailed
 * diagnostics and calls std::abort().
 */
class IRVerifier {
public:
    explicit IRVerifier(Module& module);
    ~IRVerifier() = default;

    // Non-copyable, movable
    IRVerifier(const IRVerifier&) = delete;
    IRVerifier& operator=(const IRVerifier&) = delete;
    IRVerifier(IRVerifier&&) = default;
    IRVerifier& operator=(IRVerifier&&) = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    void setAbortOnFailure(bool abort) { abortOnFailure_ = abort; }
    bool getAbortOnFailure() const { return abortOnFailure_; }

    void setValueTracker(ValueTracker* tracker) { valueTracker_ = tracker; }

    // =========================================================================
    // Per-Instruction Verification
    // Called immediately after instruction creation in IRBuilder::createInst()
    // =========================================================================

    void verifyInstruction(const Instruction* inst);

    // =========================================================================
    // Per-Function Verification
    // Called after function codegen completes
    // =========================================================================

    void verifyFunction(const Function* func);

    // =========================================================================
    // Per-Module Verification
    // Called before emission
    // =========================================================================

    void verifyModule();

    // =========================================================================
    // Error Access
    // =========================================================================

    bool hasErrors() const { return !errors_.empty(); }
    const std::vector<VerificationError>& errors() const { return errors_; }
    void clearErrors() { errors_.clear(); }

    // Print all errors to stream
    void dumpErrors(std::ostream& os = std::cerr) const;

    // =========================================================================
    // Hard Abort
    // =========================================================================

    [[noreturn]] void abort(const std::string& message);

    // =========================================================================
    // Dominance Information (for external use)
    // =========================================================================

    /// Compute and return the immediate dominator map for a function
    /// Maps each basic block to its immediate dominator (entry block has no entry)
    std::map<const BasicBlock*, const BasicBlock*> computeDominators(const Function* func);

    /// Convert error kind to human-readable string
    static const char* errorKindToString(VerificationErrorKind kind);

private:
    // =========================================================================
    // Instruction-Level Checks
    // =========================================================================

    void checkNullOperands(const Instruction* inst);
    void checkOperandTypes(const Instruction* inst);
    void checkTerminatorPlacement(const Instruction* inst);

    // =========================================================================
    // Function-Level Checks
    // =========================================================================

    void checkBasicBlockTerminators(const Function* func);
    void checkSSAForm(const Function* func);
    void checkDominance(const Function* func);
    void checkAllocaPlacement(const Function* func);
    void checkPHINodes(const Function* func);
    void checkReachability(const Function* func);
    void checkReturnTypes(const Function* func);

    // =========================================================================
    // Module-Level Checks
    // =========================================================================

    void checkFunctionNameUniqueness();
    void checkGlobalNameUniqueness();
    void checkCallTargets();
    void checkTypeConsistency();

    // =========================================================================
    // Dominance Helpers (use public computeDominators())
    // =========================================================================

    bool dominates(const BasicBlock* dominator, const BasicBlock* dominated) const;
    bool strictlyDominates(const BasicBlock* dominator, const BasicBlock* dominated) const;
    bool dominates(const Instruction* def, const Instruction* use) const;

    // =========================================================================
    // Helper Methods
    // =========================================================================

    void reportError(VerificationErrorKind kind,
                     const std::string& message,
                     const Instruction* inst = nullptr,
                     const BasicBlock* block = nullptr,
                     const Function* func = nullptr,
                     const Value* related = nullptr);

    std::string formatInstruction(const Instruction* inst) const;
    std::string formatBasicBlock(const BasicBlock* bb) const;
    std::string formatFunction(const Function* func) const;
    std::string formatValue(const Value* val) const;
    std::string formatType(const Type* type) const;

    // =========================================================================
    // State
    // =========================================================================

    Module& module_;
    bool abortOnFailure_ = true;
    std::vector<VerificationError> errors_;
    ValueTracker* valueTracker_ = nullptr;

    // Dominance tree (computed per-function)
    // Maps each block to its immediate dominator
    std::map<const BasicBlock*, const BasicBlock*> idom_;

    // Dominator tree children (inverse of idom_)
    std::map<const BasicBlock*, std::set<const BasicBlock*>> domTree_;

    // Current function being verified (for context in error messages)
    const Function* currentFunction_ = nullptr;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
