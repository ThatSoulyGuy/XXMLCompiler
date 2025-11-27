#pragma once

#include "Backends/IR/Instructions.h"
#include <list>
#include <memory>
#include <vector>
#include <string>

namespace XXML {
namespace Backends {
namespace IR {

// Forward declarations
class Function;

// ============================================================================
// BasicBlock - A sequence of instructions ending with a terminator
// ============================================================================

class BasicBlock : public Value {
public:
    using InstListType = std::list<std::unique_ptr<Instruction>>;
    using iterator = InstListType::iterator;
    using const_iterator = InstListType::const_iterator;
    using reverse_iterator = InstListType::reverse_iterator;
    using const_reverse_iterator = InstListType::const_reverse_iterator;

    // Create a basic block
    BasicBlock(const std::string& name = "", Function* parent = nullptr);
    ~BasicBlock();

    // Parent function
    Function* getParent() const { return parent_; }
    void setParent(Function* f) { parent_ = f; }

    // ========== Instruction List ==========

    iterator begin() { return instructions_.begin(); }
    iterator end() { return instructions_.end(); }
    const_iterator begin() const { return instructions_.begin(); }
    const_iterator end() const { return instructions_.end(); }

    reverse_iterator rbegin() { return instructions_.rbegin(); }
    reverse_iterator rend() { return instructions_.rend(); }

    bool empty() const { return instructions_.empty(); }
    size_t size() const { return instructions_.size(); }

    Instruction& front() { return *instructions_.front(); }
    Instruction& back() { return *instructions_.back(); }
    const Instruction& front() const { return *instructions_.front(); }
    const Instruction& back() const { return *instructions_.back(); }

    // Add instruction at end
    void push_back(std::unique_ptr<Instruction> inst);

    // Add instruction at specific position
    iterator insert(iterator pos, std::unique_ptr<Instruction> inst);

    // Remove instruction (returns iterator to next)
    iterator erase(iterator pos);
    iterator erase(Instruction* inst);

    // ========== Terminator ==========

    // Get the terminator (last instruction, must be a terminator)
    TerminatorInst* getTerminator();
    const TerminatorInst* getTerminator() const;

    bool hasTerminator() const;

    // ========== Control Flow Graph ==========

    // Predecessors (blocks that branch to this block)
    size_t getNumPredecessors() const { return predecessors_.size(); }
    BasicBlock* getPredecessor(size_t i) const {
        return i < predecessors_.size() ? predecessors_[i] : nullptr;
    }
    const std::vector<BasicBlock*>& getPredecessors() const { return predecessors_; }

    // Single predecessor (returns nullptr if 0 or >1 predecessors)
    BasicBlock* getSinglePredecessor() const {
        return predecessors_.size() == 1 ? predecessors_[0] : nullptr;
    }

    // Successors (blocks this block branches to)
    size_t getNumSuccessors() const { return successors_.size(); }
    BasicBlock* getSuccessor(size_t i) const {
        return i < successors_.size() ? successors_[i] : nullptr;
    }
    const std::vector<BasicBlock*>& getSuccessors() const { return successors_; }

    // Single successor (returns nullptr if 0 or >1 successors)
    BasicBlock* getSingleSuccessor() const {
        return successors_.size() == 1 ? successors_[0] : nullptr;
    }

    // Update CFG edges based on terminator
    void updateCFG();

    // Add/remove predecessor (used by CFG update)
    void addPredecessor(BasicBlock* pred);
    void removePredecessor(BasicBlock* pred);

    // ========== PHI Nodes ==========

    // PHI nodes must be at the beginning of the block
    bool hasPhiNodes() const;
    PHINode* getFirstPHI();

    // Iterate over PHI nodes
    class phi_iterator {
    public:
        phi_iterator(iterator it, iterator end) : it_(it), end_(end) {
            advanceToNextPHI();
        }
        PHINode& operator*() const { return *cast<PHINode>(it_->get()); }
        PHINode* operator->() const { return cast<PHINode>(it_->get()); }
        phi_iterator& operator++() {
            ++it_;
            advanceToNextPHI();
            return *this;
        }
        bool operator!=(const phi_iterator& other) const { return it_ != other.it_; }
    private:
        void advanceToNextPHI() {
            while (it_ != end_ && !isa<PHINode>(it_->get())) {
                it_ = end_;  // No more PHIs
            }
        }
        iterator it_;
        iterator end_;
    };

    phi_iterator phi_begin() { return phi_iterator(begin(), end()); }
    phi_iterator phi_end() { return phi_iterator(end(), end()); }

    // ========== Utility ==========

    // Split block at given instruction (instruction becomes first of new block)
    BasicBlock* splitAt(iterator pos, const std::string& newName);

    // Remove this block from parent and delete
    void eraseFromParent();

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::BasicBlock;
    }

private:
    Function* parent_;
    InstListType instructions_;

    // CFG edges
    std::vector<BasicBlock*> predecessors_;
    std::vector<BasicBlock*> successors_;

    // Maintain instruction linked list pointers
    void updateInstructionLinks();
};

} // namespace IR
} // namespace Backends
} // namespace XXML
