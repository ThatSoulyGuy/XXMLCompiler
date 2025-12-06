#include "Semantic/ControlFlowAnalyzer.h"
#include <algorithm>
#include <stack>

namespace XXML {
namespace Semantic {

//==============================================================================
// ControlFlowGraph Implementation
//==============================================================================

ControlFlowGraph::ControlFlowGraph(
    const std::vector<std::unique_ptr<Parser::Statement>>& body) {

    // Create entry block
    entry_ = createBlock("entry");

    // Create exit block (reached by returns)
    exit_ = createBlock("exit");
    exit_->terminates = true;

    // Create fallthrough block (reached by falling off end)
    fallthrough_ = createBlock("fallthrough");

    // Build the CFG
    BasicBlock* endBlock = buildCFG(body, entry_, 0, nullptr, nullptr);

    // If we reach the end without returning, connect to fallthrough
    if (endBlock && !endBlock->terminates) {
        endBlock->addSuccessor(fallthrough_);
    }

    // Compute reachability from entry
    computeReachability();
}

BasicBlock* ControlFlowGraph::createBlock(const std::string& label) {
    auto block = std::make_unique<BasicBlock>(nextBlockId_++, label);
    BasicBlock* ptr = block.get();
    blocks_.push_back(std::move(block));
    return ptr;
}

BasicBlock* ControlFlowGraph::buildCFG(
    const std::vector<std::unique_ptr<Parser::Statement>>& stmts,
    BasicBlock* current,
    int loopDepth,
    BasicBlock* loopHeader,
    BasicBlock* loopExit) {

    for (const auto& stmt : stmts) {
        if (!current || current->terminates) {
            // Code after a terminator is unreachable
            // Create a new block to hold it but mark it unreachable
            current = createBlock("unreachable");
            current->isUnreachable = true;
        }

        current->loopDepth = loopDepth;

        // Handle different statement types
        if (auto* ret = dynamic_cast<Parser::ReturnStmt*>(stmt.get())) {
            current->statements.push_back(stmt.get());
            current->terminates = true;
            current->addSuccessor(exit_);
        }
        else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt.get())) {
            current->statements.push_back(stmt.get());

            // Create then-block
            BasicBlock* thenBlock = createBlock("if.then");
            current->addSuccessor(thenBlock);
            BasicBlock* thenExit = buildCFG(ifStmt->thenBranch, thenBlock, loopDepth, loopHeader, loopExit);

            // Create else-block (or direct fall-through)
            BasicBlock* elseExit = nullptr;
            if (!ifStmt->elseBranch.empty()) {
                BasicBlock* elseBlock = createBlock("if.else");
                current->addSuccessor(elseBlock);
                elseExit = buildCFG(ifStmt->elseBranch, elseBlock, loopDepth, loopHeader, loopExit);
            }

            // Create join block where paths merge
            BasicBlock* joinBlock = createBlock("if.end");

            // Connect then path to join (if it doesn't terminate)
            if (thenExit && !thenExit->terminates) {
                thenExit->addSuccessor(joinBlock);
            }

            // Connect else path to join (if it doesn't terminate)
            if (elseExit && !elseExit->terminates) {
                elseExit->addSuccessor(joinBlock);
            } else if (!elseExit) {
                // No else branch - condition can be false and fall through
                current->addSuccessor(joinBlock);
            }

            current = joinBlock;
        }
        else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt.get())) {
            // Loop header (where condition is evaluated)
            BasicBlock* header = createBlock("while.cond");
            header->isLoopHeader = true;
            current->addSuccessor(header);

            // Loop body
            BasicBlock* body = createBlock("while.body");
            header->addSuccessor(body);

            // Loop exit (after loop)
            BasicBlock* loopEnd = createBlock("while.end");
            header->addSuccessor(loopEnd);  // Condition can be false

            // Build body with increased loop depth
            BasicBlock* bodyExit = buildCFG(
                whileStmt->body, body, loopDepth + 1, header, loopEnd);

            // Back edge from body exit to header
            if (bodyExit && !bodyExit->terminates) {
                bodyExit->addSuccessor(header);
            }

            current = loopEnd;
        }
        else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt.get())) {
            // XXML ForStmt can be range-based (For x = 0 .. 10) or C-style
            // Both have a body, and we treat them similarly for CFG

            // Initialization block (if any)
            BasicBlock* initBlock = createBlock("for.init");
            current->addSuccessor(initBlock);

            // Loop header (condition check)
            BasicBlock* header = createBlock("for.cond");
            header->isLoopHeader = true;
            initBlock->addSuccessor(header);

            // Loop body
            BasicBlock* body = createBlock("for.body");
            header->addSuccessor(body);

            // Loop exit
            BasicBlock* loopEnd = createBlock("for.end");
            header->addSuccessor(loopEnd);  // Condition can be false

            // Increment block (after body, before back edge)
            BasicBlock* incBlock = createBlock("for.inc");

            // Build body
            BasicBlock* bodyExit = buildCFG(
                forStmt->body, body, loopDepth + 1, header, loopEnd);

            // Connect body exit to increment
            if (bodyExit && !bodyExit->terminates) {
                bodyExit->addSuccessor(incBlock);
            }

            // Back edge from increment to header
            incBlock->addSuccessor(header);

            current = loopEnd;
        }
        else if (auto* breakStmt = dynamic_cast<Parser::BreakStmt*>(stmt.get())) {
            current->statements.push_back(stmt.get());

            if (loopDepth == 0 || !loopExit) {
                // Break outside loop
                invalidBreakContinue_.push_back(
                    "break statement outside of loop at line " +
                    std::to_string(stmt->location.line));
            } else {
                // Connect to loop exit
                current->addSuccessor(loopExit);
            }
            current->terminates = true;  // Effectively terminates this path
        }
        else if (auto* continueStmt = dynamic_cast<Parser::ContinueStmt*>(stmt.get())) {
            current->statements.push_back(stmt.get());

            if (loopDepth == 0 || !loopHeader) {
                // Continue outside loop
                invalidBreakContinue_.push_back(
                    "continue statement outside of loop at line " +
                    std::to_string(stmt->location.line));
            } else {
                // Connect to loop header
                current->addSuccessor(loopHeader);
            }
            current->terminates = true;  // Effectively terminates this path
        }
        else {
            // All other statements just add to current block
            current->statements.push_back(stmt.get());
        }
    }

    return current;
}

void ControlFlowGraph::computeReachability() {
    // Mark all blocks as unreachable initially
    for (auto& block : blocks_) {
        block->isUnreachable = true;
    }

    // DFS from entry to mark reachable blocks
    std::stack<BasicBlock*> worklist;
    worklist.push(entry_);

    while (!worklist.empty()) {
        BasicBlock* block = worklist.top();
        worklist.pop();

        if (!block->isUnreachable) continue;  // Already visited
        block->isUnreachable = false;

        for (BasicBlock* succ : block->successors) {
            if (succ->isUnreachable) {
                worklist.push(succ);
            }
        }
    }
}

bool ControlFlowGraph::allPathsReturn() const {
    // If fallthrough block is reachable, not all paths return
    return fallthrough_->isUnreachable;
}

std::vector<std::string> ControlFlowGraph::findInvalidBreakContinue() const {
    return invalidBreakContinue_;
}

std::vector<std::string> ControlFlowGraph::findUnreachableCode() const {
    std::vector<std::string> warnings;

    for (const auto& block : blocks_) {
        // Skip special blocks
        if (block.get() == entry_ || block.get() == exit_ || block.get() == fallthrough_) {
            continue;
        }

        // Check for unreachable blocks with actual statements
        if (block->isUnreachable && !block->statements.empty()) {
            // Get location of first statement
            auto* firstStmt = block->statements.front();
            warnings.push_back(
                "unreachable code at line " + std::to_string(firstStmt->location.line));
        }
    }

    return warnings;
}

//==============================================================================
// ControlFlowVerifier Implementation
//==============================================================================

ControlFlowVerifier::VerificationResult ControlFlowVerifier::verifyMethodReturns(
    const std::string& className,
    const std::string& methodName,
    const std::string& returnType,
    const std::vector<std::unique_ptr<Parser::Statement>>& body) {

    VerificationResult result;

    // Skip void/None methods - they don't need to return
    if (returnType == "None" || returnType == "Void" || returnType.empty()) {
        return result;
    }

    // Build CFG
    ControlFlowGraph cfg(body);

    // Check if all paths return
    if (!cfg.allPathsReturn()) {
        result.addError("Method '" + className + "::" + methodName +
                       "' does not return a value on all code paths");
    }

    // Check for invalid break/continue
    for (const auto& err : cfg.findInvalidBreakContinue()) {
        result.addError("In method '" + className + "::" + methodName + "': " + err);
    }

    // Warn about unreachable code
    for (const auto& warn : cfg.findUnreachableCode()) {
        result.addWarning("In method '" + className + "::" + methodName + "': " + warn);
    }

    return result;
}

ControlFlowVerifier::VerificationResult ControlFlowVerifier::verifyBreakContinue(
    const std::string& className,
    const std::string& methodName,
    const std::vector<std::unique_ptr<Parser::Statement>>& body) {

    VerificationResult result;

    // Build CFG (which detects invalid break/continue)
    ControlFlowGraph cfg(body);

    // Report any invalid break/continue
    for (const auto& err : cfg.findInvalidBreakContinue()) {
        result.addError("In method '" + className + "::" + methodName + "': " + err);
    }

    return result;
}

} // namespace Semantic
} // namespace XXML
