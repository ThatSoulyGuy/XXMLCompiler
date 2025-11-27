#include "Backends/IR/BasicBlock.h"
#include "Backends/IR/Function.h"
#include <algorithm>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// BasicBlock Implementation
// ============================================================================

BasicBlock::BasicBlock(const std::string& name, Function* parent)
    : Value(Kind::BasicBlock, nullptr, name), parent_(parent) {
    // BasicBlock has label type, but we don't set it here as TypeContext isn't available
}

BasicBlock::~BasicBlock() {
    // Instructions will be deleted by unique_ptr
}

void BasicBlock::push_back(std::unique_ptr<Instruction> inst) {
    inst->setParent(this);

    // Update linked list
    if (!instructions_.empty()) {
        Instruction* last = instructions_.back().get();
        last->next_ = inst.get();
        inst->prev_ = last;
    }
    inst->next_ = nullptr;

    instructions_.push_back(std::move(inst));
}

BasicBlock::iterator BasicBlock::insert(iterator pos, std::unique_ptr<Instruction> inst) {
    inst->setParent(this);

    // Update linked list pointers
    if (pos != instructions_.end()) {
        Instruction* current = pos->get();
        inst->next_ = current;
        inst->prev_ = current->prev_;
        if (current->prev_) {
            current->prev_->next_ = inst.get();
        }
        current->prev_ = inst.get();
    } else if (!instructions_.empty()) {
        Instruction* last = instructions_.back().get();
        last->next_ = inst.get();
        inst->prev_ = last;
        inst->next_ = nullptr;
    }

    return instructions_.insert(pos, std::move(inst));
}

BasicBlock::iterator BasicBlock::erase(iterator pos) {
    if (pos == instructions_.end()) return pos;

    Instruction* inst = pos->get();

    // Update linked list
    if (inst->prev_) {
        inst->prev_->next_ = inst->next_;
    }
    if (inst->next_) {
        inst->next_->prev_ = inst->prev_;
    }

    inst->setParent(nullptr);
    return instructions_.erase(pos);
}

BasicBlock::iterator BasicBlock::erase(Instruction* inst) {
    for (auto it = instructions_.begin(); it != instructions_.end(); ++it) {
        if (it->get() == inst) {
            return erase(it);
        }
    }
    return instructions_.end();
}

TerminatorInst* BasicBlock::getTerminator() {
    if (instructions_.empty()) return nullptr;
    Instruction* last = instructions_.back().get();
    return last->isTerminator() ? static_cast<TerminatorInst*>(last) : nullptr;
}

const TerminatorInst* BasicBlock::getTerminator() const {
    if (instructions_.empty()) return nullptr;
    const Instruction* last = instructions_.back().get();
    return last->isTerminator() ? static_cast<const TerminatorInst*>(last) : nullptr;
}

bool BasicBlock::hasTerminator() const {
    return getTerminator() != nullptr;
}

void BasicBlock::updateCFG() {
    // Clear old successor edges
    for (BasicBlock* succ : successors_) {
        succ->removePredecessor(this);
    }
    successors_.clear();

    // Get new successors from terminator
    if (TerminatorInst* term = getTerminator()) {
        for (unsigned i = 0; i < term->getNumSuccessors(); ++i) {
            if (BasicBlock* succ = term->getSuccessor(i)) {
                successors_.push_back(succ);
                succ->addPredecessor(this);
            }
        }
    }
}

void BasicBlock::addPredecessor(BasicBlock* pred) {
    // Avoid duplicates
    if (std::find(predecessors_.begin(), predecessors_.end(), pred) == predecessors_.end()) {
        predecessors_.push_back(pred);
    }
}

void BasicBlock::removePredecessor(BasicBlock* pred) {
    auto it = std::find(predecessors_.begin(), predecessors_.end(), pred);
    if (it != predecessors_.end()) {
        predecessors_.erase(it);
    }
}

bool BasicBlock::hasPhiNodes() const {
    if (instructions_.empty()) return false;
    return isa<PHINode>(instructions_.front().get());
}

PHINode* BasicBlock::getFirstPHI() {
    if (instructions_.empty()) return nullptr;
    return dyn_cast<PHINode>(instructions_.front().get());
}

BasicBlock* BasicBlock::splitAt(iterator pos, const std::string& newName) {
    if (pos == instructions_.end() || !parent_) return nullptr;

    // Create new block
    BasicBlock* newBB = new BasicBlock(newName, parent_);

    // Move instructions from pos to end into new block
    while (pos != instructions_.end()) {
        std::unique_ptr<Instruction> inst = std::move(*pos);
        pos = instructions_.erase(pos);
        newBB->push_back(std::move(inst));
    }

    // Update CFG
    newBB->successors_ = std::move(successors_);
    for (BasicBlock* succ : newBB->successors_) {
        // Update predecessor lists
        auto it = std::find(succ->predecessors_.begin(), succ->predecessors_.end(), this);
        if (it != succ->predecessors_.end()) {
            *it = newBB;
        }
    }

    // This block now has single successor: the new block
    successors_.clear();
    successors_.push_back(newBB);
    newBB->predecessors_.push_back(this);

    return newBB;
}

void BasicBlock::eraseFromParent() {
    if (parent_) {
        parent_->erase(this);
    }
}

void BasicBlock::updateInstructionLinks() {
    Instruction* prev = nullptr;
    for (auto& instPtr : instructions_) {
        Instruction* inst = instPtr.get();
        inst->prev_ = prev;
        if (prev) {
            prev->next_ = inst;
        }
        prev = inst;
    }
    if (prev) {
        prev->next_ = nullptr;
    }
}

} // namespace IR
} // namespace Backends
} // namespace XXML
