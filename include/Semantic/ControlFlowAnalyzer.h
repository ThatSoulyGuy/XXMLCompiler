#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <functional>
#include "../Parser/AST.h"
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// CONTROL FLOW ANALYZER
//
// Builds and analyzes Control Flow Graphs (CFGs) for method bodies to verify:
// 1. All code paths return a value (for non-void methods)
// 2. No unreachable code exists
// 3. Break/continue statements are inside loops
//==============================================================================

/// Represents a basic block in the control flow graph
struct BasicBlock {
    int id;
    std::string label;
    std::vector<Parser::Statement*> statements;
    std::vector<BasicBlock*> successors;
    std::vector<BasicBlock*> predecessors;
    bool terminates = false;      // Has return/throw/exit
    bool isUnreachable = false;   // Cannot be reached from entry
    bool isLoopHeader = false;    // Start of a loop
    int loopDepth = 0;            // Nesting depth in loops

    BasicBlock(int blockId, const std::string& lbl)
        : id(blockId), label(lbl) {}

    void addSuccessor(BasicBlock* succ) {
        successors.push_back(succ);
        succ->predecessors.push_back(this);
    }
};

/// Control Flow Graph for a method body
class ControlFlowGraph {
public:
    /// Build CFG from a method body
    explicit ControlFlowGraph(const std::vector<std::unique_ptr<Parser::Statement>>& body);

    /// Check if all paths from entry block reach a return statement
    bool allPathsReturn() const;

    /// Get list of break/continue statements outside loops
    std::vector<std::string> findInvalidBreakContinue() const;

    /// Get unreachable code warnings
    std::vector<std::string> findUnreachableCode() const;

    /// Get entry block
    BasicBlock* getEntry() const { return entry_; }

    /// Get exit block
    BasicBlock* getExit() const { return exit_; }

    /// Get all blocks
    const std::vector<std::unique_ptr<BasicBlock>>& getBlocks() const { return blocks_; }

private:
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    BasicBlock* entry_ = nullptr;
    BasicBlock* exit_ = nullptr;       // Virtual exit block (reached by returns)
    BasicBlock* fallthrough_ = nullptr; // Block for falling off end without return

    // Track invalid break/continue during construction
    std::vector<std::string> invalidBreakContinue_;

    // Unique block ID counter
    int nextBlockId_ = 0;

    /// Create a new basic block
    BasicBlock* createBlock(const std::string& label);

    /// Build CFG recursively
    /// @param stmts Statements to process
    /// @param current Current basic block
    /// @param loopDepth Current nesting depth in loops
    /// @param loopHeader Header block of innermost loop (for continue)
    /// @param loopExit Exit block of innermost loop (for break)
    /// @returns The block where control flows after processing stmts
    BasicBlock* buildCFG(
        const std::vector<std::unique_ptr<Parser::Statement>>& stmts,
        BasicBlock* current,
        int loopDepth,
        BasicBlock* loopHeader = nullptr,
        BasicBlock* loopExit = nullptr);

    /// Mark unreachable blocks using DFS from entry
    void computeReachability();
};

/// High-level control flow verifier for methods
class ControlFlowVerifier {
public:
    struct VerificationResult {
        bool success = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        void addError(const std::string& msg) {
            success = false;
            errors.push_back(msg);
        }

        void addWarning(const std::string& msg) {
            warnings.push_back(msg);
        }
    };

    /// Verify that a method returns on all code paths
    static VerificationResult verifyMethodReturns(
        const std::string& className,
        const std::string& methodName,
        const std::string& returnType,
        const std::vector<std::unique_ptr<Parser::Statement>>& body);

    /// Verify break/continue are inside loops
    static VerificationResult verifyBreakContinue(
        const std::string& className,
        const std::string& methodName,
        const std::vector<std::unique_ptr<Parser::Statement>>& body);
};

} // namespace Semantic
} // namespace XXML
