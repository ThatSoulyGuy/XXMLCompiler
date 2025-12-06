#include "Backends/LLVMIR/ValueTracker.h"
#include <algorithm>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// =============================================================================
// Construction
// =============================================================================

ValueTracker::ValueTracker(const Function* func)
    : function_(func) {}

// =============================================================================
// Value Registration
// =============================================================================

void ValueTracker::registerDefinition(const Value* value,
                                       const BasicBlock* block,
                                       const Instruction* definingInst) {
    if (!value) return;

    // Calculate position in block
    size_t position = 0;
    if (block && definingInst) {
        for (auto it = block->begin(); it != block->end(); ++it) {
            if (*it == definingInst) break;
            position++;
        }
    }

    ValueDefinition def;
    def.value = value;
    def.block = block;
    def.definingInst = definingInst;
    def.positionInBlock = position;
    def.creationOrder = creationCounter_++;

    definitions_[value] = def;
}

void ValueTracker::registerUse(const Value* value,
                               const Instruction* user,
                               size_t operandIndex) {
    if (!value || !user) return;

    ValueUse use;
    use.user = user;
    use.operandIndex = operandIndex;
    use.block = user->getParent();
    use.useOrder = useCounter_++;

    uses_[value].push_back(use);
}

// =============================================================================
// Queries
// =============================================================================

const ValueDefinition* ValueTracker::getDefinition(const Value* value) const {
    auto it = definitions_.find(value);
    if (it != definitions_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<ValueUse> ValueTracker::getUses(const Value* value) const {
    auto it = uses_.find(value);
    if (it != uses_.end()) {
        return it->second;
    }
    return {};
}

bool ValueTracker::isDefined(const Value* value) const {
    return definitions_.find(value) != definitions_.end();
}

bool ValueTracker::isDefinedBefore(const Value* def, const Value* use) const {
    auto defIt = definitions_.find(def);
    auto useIt = definitions_.find(use);

    if (defIt == definitions_.end() || useIt == definitions_.end()) {
        return false;
    }

    return defIt->second.creationOrder < useIt->second.creationOrder;
}

// =============================================================================
// SSA Verification
// =============================================================================

std::vector<const Value*> ValueTracker::findUndefinedUses() const {
    std::vector<const Value*> undefined;

    for (const auto& [value, useList] : uses_) {
        // Check if this value has a definition
        if (definitions_.find(value) == definitions_.end()) {
            // Not in our definitions - could be:
            // 1. A constant (OK)
            // 2. A global (OK)
            // 3. An undefined value (ERROR)

            // Skip constants
            if (dynamic_cast<const Constant*>(value)) {
                continue;
            }

            // Skip globals
            if (dynamic_cast<const GlobalVariable*>(value)) {
                continue;
            }

            // This is a truly undefined use
            undefined.push_back(value);
        }
    }

    return undefined;
}

std::vector<const Value*> ValueTracker::findDeadValues() const {
    std::vector<const Value*> dead;

    for (const auto& [value, def] : definitions_) {
        // Check if this value has any uses
        auto usesIt = uses_.find(value);
        if (usesIt == uses_.end() || usesIt->second.empty()) {
            // No uses - dead value

            // Skip void-typed instructions (they don't produce values)
            if (def.definingInst && def.definingInst->getType() &&
                def.definingInst->getType()->isVoid()) {
                continue;
            }

            // Skip terminators and side-effecting instructions
            if (def.definingInst) {
                switch (def.definingInst->getOpcode()) {
                    case Opcode::Store:
                    case Opcode::Call:
                    case Opcode::Br:
                    case Opcode::CondBr:
                    case Opcode::Ret:
                    case Opcode::Unreachable:
                        continue;
                    default:
                        break;
                }
            }

            dead.push_back(value);
        }
    }

    return dead;
}

std::vector<const Value*> ValueTracker::findMultipleDefinitions() const {
    // In our tracking model, each value maps to one definition
    // Multiple definitions would be caught at registration time
    // This is mostly for detecting PHI-like patterns or reassignment

    // For now, return empty - proper SSA doesn't allow multiple definitions
    return {};
}

// =============================================================================
// Dominance Integration
// =============================================================================

void ValueTracker::setDominanceInfo(
    const std::map<const BasicBlock*, const BasicBlock*>& idom) {
    idom_ = idom;
}

bool ValueTracker::blockDominates(const BasicBlock* dominator,
                                   const BasicBlock* dominated) const {
    if (dominator == dominated) return true;
    if (idom_.empty()) return false;

    const BasicBlock* current = dominated;
    while (current) {
        auto it = idom_.find(current);
        if (it == idom_.end()) break;
        current = it->second;
        if (current == dominator) return true;
    }

    return false;
}

bool ValueTracker::instructionPrecedes(const Instruction* a,
                                        const Instruction* b,
                                        const BasicBlock* block) const {
    if (!a || !b || !block) return false;

    for (auto it = block->begin(); it != block->end(); ++it) {
        if (*it == a) return true;
        if (*it == b) return false;
    }

    return false;
}

bool ValueTracker::checkDominance(const Value* def, const Instruction* use) const {
    if (!def || !use) return false;

    auto defIt = definitions_.find(def);
    if (defIt == definitions_.end()) {
        // Not tracked - assume OK for constants/globals
        if (dynamic_cast<const Constant*>(def) ||
            dynamic_cast<const GlobalVariable*>(def)) {
            return true;
        }
        return false;
    }

    const ValueDefinition& defInfo = defIt->second;
    const BasicBlock* defBlock = defInfo.block;
    const BasicBlock* useBlock = use->getParent();

    if (!defBlock || !useBlock) return false;

    if (defBlock == useBlock) {
        // Same block: definition must come before use
        if (defInfo.definingInst) {
            return instructionPrecedes(defInfo.definingInst, use, defBlock);
        }
        // Argument definition - always available at start of function
        return true;
    }

    // Different blocks: defBlock must dominate useBlock
    return blockDominates(defBlock, useBlock);
}

std::vector<std::pair<const Value*, ValueUse>>
ValueTracker::findDominanceViolations() const {
    std::vector<std::pair<const Value*, ValueUse>> violations;

    for (const auto& [value, useList] : uses_) {
        for (const ValueUse& use : useList) {
            if (!checkDominance(value, use.user)) {
                violations.push_back({value, use});
            }
        }
    }

    return violations;
}

// =============================================================================
// Lifecycle
// =============================================================================

void ValueTracker::clear() {
    definitions_.clear();
    uses_.clear();
    idom_.clear();
    creationCounter_ = 0;
    useCounter_ = 0;
}

void ValueTracker::setFunction(const Function* func) {
    function_ = func;
    clear();
}

// =============================================================================
// Statistics
// =============================================================================

size_t ValueTracker::getUseCount() const {
    return uses_.size();
}

size_t ValueTracker::getTotalUses() const {
    size_t total = 0;
    for (const auto& [value, useList] : uses_) {
        total += useList.size();
    }
    return total;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
