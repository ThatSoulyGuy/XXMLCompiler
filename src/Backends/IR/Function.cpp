#include "Backends/IR/Function.h"
#include "Backends/IR/Module.h"
#include <algorithm>
#include <queue>
#include <set>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// DominanceInfo Implementation
// ============================================================================

void DominanceInfo::compute(Function* func) {
    if (!func || func->empty()) {
        valid_ = false;
        return;
    }

    // Clear old data
    idom_.clear();
    dominated_.clear();

    // Simple dominance computation using data flow
    // A dominates B if every path from entry to B goes through A

    BasicBlock* entry = func->getEntryBlock();

    // Initialize: entry dominates only itself
    std::map<BasicBlock*, std::set<BasicBlock*>> domSets;
    std::set<BasicBlock*> allBlocks;

    for (auto& bb : *func) {
        allBlocks.insert(bb.get());
    }

    // Entry is dominated only by itself
    domSets[entry].insert(entry);

    // All other blocks are initially dominated by all blocks
    for (auto& bb : *func) {
        if (bb.get() != entry) {
            domSets[bb.get()] = allBlocks;
        }
    }

    // Iterate until fixed point
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& bb : *func) {
            BasicBlock* block = bb.get();
            if (block == entry) continue;

            std::set<BasicBlock*> newDom = allBlocks;

            // Intersection of all predecessors' dominator sets
            bool first = true;
            for (BasicBlock* pred : block->getPredecessors()) {
                if (first) {
                    newDom = domSets[pred];
                    first = false;
                } else {
                    std::set<BasicBlock*> intersection;
                    std::set_intersection(
                        newDom.begin(), newDom.end(),
                        domSets[pred].begin(), domSets[pred].end(),
                        std::inserter(intersection, intersection.begin())
                    );
                    newDom = std::move(intersection);
                }
            }

            // Add self
            newDom.insert(block);

            if (newDom != domSets[block]) {
                domSets[block] = std::move(newDom);
                changed = true;
            }
        }
    }

    // Compute immediate dominators
    for (auto& bb : *func) {
        BasicBlock* block = bb.get();
        if (block == entry) {
            idom_[block] = nullptr;
            continue;
        }

        // Find the immediate dominator (closest dominator that isn't self)
        BasicBlock* idominator = nullptr;
        for (BasicBlock* dom : domSets[block]) {
            if (dom == block) continue;

            // Check if this is the immediate dominator
            bool isImmediate = true;
            for (BasicBlock* other : domSets[block]) {
                if (other == block || other == dom) continue;
                // If dom dominates other and other dominates block,
                // then dom is not the immediate dominator
                if (domSets[other].count(dom) && domSets[block].count(other)) {
                    isImmediate = false;
                    break;
                }
            }

            if (isImmediate) {
                idominator = dom;
                break;
            }
        }
        idom_[block] = idominator;
    }

    // Build dominated sets
    for (auto& pair : idom_) {
        if (pair.second) {
            dominated_[pair.second].push_back(pair.first);
        }
    }

    valid_ = true;
}

bool DominanceInfo::dominates(BasicBlock* a, BasicBlock* b) const {
    if (!valid_ || !a || !b) return false;
    if (a == b) return true;

    // Walk up the dominator tree from b
    BasicBlock* current = b;
    while (current) {
        auto it = idom_.find(current);
        if (it == idom_.end()) break;
        if (it->second == a) return true;
        current = it->second;
    }
    return false;
}

bool DominanceInfo::dominates(Instruction* a, Instruction* b) const {
    if (!valid_ || !a || !b) return false;

    BasicBlock* blockA = a->getParent();
    BasicBlock* blockB = b->getParent();

    if (!blockA || !blockB) return false;

    if (blockA == blockB) {
        // Same block: check instruction order
        for (auto& inst : *blockA) {
            if (inst.get() == a) return true;
            if (inst.get() == b) return false;
        }
        return false;
    }

    // Different blocks: block dominance
    return dominates(blockA, blockB);
}

BasicBlock* DominanceInfo::getImmediateDominator(BasicBlock* bb) const {
    auto it = idom_.find(bb);
    return it != idom_.end() ? it->second : nullptr;
}

const std::vector<BasicBlock*>& DominanceInfo::getDominatedBlocks(BasicBlock* bb) const {
    static std::vector<BasicBlock*> empty;
    auto it = dominated_.find(bb);
    return it != dominated_.end() ? it->second : empty;
}

// ============================================================================
// Function Implementation
// ============================================================================

Function::Function(FunctionType* funcType, const std::string& name, Module* parent,
                   Linkage linkage)
    : GlobalValue(Kind::Function, parent ? parent->getContext().getPtrTy() : nullptr,
                  name, linkage),
      parent_(parent), funcType_(funcType) {
    createArguments();
    isDeclaration_ = true;  // No basic blocks yet
}

Function::~Function() {
    // Blocks and args will be deleted by unique_ptr
}

void Function::createArguments() {
    args_.clear();
    for (size_t i = 0; i < funcType_->getNumParams(); ++i) {
        Type* paramType = funcType_->getParamType(i);
        std::string argName = "arg" + std::to_string(i);
        args_.push_back(std::make_unique<Argument>(paramType, argName, this, i));
    }
}

void Function::push_back(std::unique_ptr<BasicBlock> bb) {
    bb->setParent(this);
    blocks_.push_back(std::move(bb));
    isDeclaration_ = false;
    domInfo_.invalidate();
}

Function::iterator Function::insert(iterator pos, std::unique_ptr<BasicBlock> bb) {
    bb->setParent(this);
    auto result = blocks_.insert(pos, std::move(bb));
    isDeclaration_ = false;
    domInfo_.invalidate();
    return result;
}

Function::iterator Function::erase(iterator pos) {
    if (pos == blocks_.end()) return pos;
    (*pos)->setParent(nullptr);
    auto result = blocks_.erase(pos);
    if (blocks_.empty()) {
        isDeclaration_ = true;
    }
    domInfo_.invalidate();
    return result;
}

Function::iterator Function::erase(BasicBlock* bb) {
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        if (it->get() == bb) {
            return erase(it);
        }
    }
    return blocks_.end();
}

BasicBlock* Function::createBasicBlock(const std::string& name) {
    auto bb = std::make_unique<BasicBlock>(name, this);
    BasicBlock* result = bb.get();
    push_back(std::move(bb));
    return result;
}

void Function::computeDominanceInfo() {
    domInfo_.compute(this);
}

bool Function::dominates(BasicBlock* a, BasicBlock* b) {
    if (!domInfo_.isValid()) {
        computeDominanceInfo();
    }
    return domInfo_.dominates(a, b);
}

bool Function::dominates(Instruction* a, Instruction* b) {
    if (!domInfo_.isValid()) {
        computeDominanceInfo();
    }
    return domInfo_.dominates(a, b);
}

BasicBlock* Function::getImmediateDominator(BasicBlock* bb) {
    if (!domInfo_.isValid()) {
        computeDominanceInfo();
    }
    return domInfo_.getImmediateDominator(bb);
}

void Function::viewCFG() const {
    // Simple text-based CFG view
    for (const auto& bb : blocks_) {
        std::string label = bb->hasName() ? bb->getName() : "(unnamed)";
        // Print block and successors
        // This would be used for debugging
    }
}

} // namespace IR
} // namespace Backends
} // namespace XXML
