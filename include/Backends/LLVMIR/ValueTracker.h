#pragma once

#include "TypedModule.h"
#include "TypedInstructions.h"
#include <map>
#include <vector>
#include <set>
#include <cstdint>

namespace XXML {
namespace Backends {
namespace LLVMIR {

/**
 * Information about where a value is defined
 */
struct ValueDefinition {
    const Value* value = nullptr;
    const BasicBlock* block = nullptr;
    const Instruction* definingInst = nullptr;  // nullptr for arguments
    size_t positionInBlock = 0;
    uint64_t creationOrder = 0;

    bool isArgument() const { return definingInst == nullptr; }
};

/**
 * Information about where a value is used
 */
struct ValueUse {
    const Instruction* user = nullptr;
    size_t operandIndex = 0;
    const BasicBlock* block = nullptr;
    uint64_t useOrder = 0;
};

/**
 * ValueTracker - Tracks value definitions and uses for SSA verification
 *
 * This class maintains a registry of all values created during IR generation,
 * recording where each value is defined and where it is used. This information
 * is essential for:
 * - Verifying SSA form (values defined before use)
 * - Detecting dominance violations
 * - Finding dead (unused) values
 * - Finding undefined uses
 */
class ValueTracker {
public:
    ValueTracker() = default;
    explicit ValueTracker(const Function* func);

    // =========================================================================
    // Value Registration
    // =========================================================================

    /**
     * Register a value definition.
     * Call this when a value-producing instruction is created.
     *
     * @param value The value being defined
     * @param block The basic block containing the definition
     * @param definingInst The instruction that defines the value (nullptr for arguments)
     */
    void registerDefinition(const Value* value,
                           const BasicBlock* block,
                           const Instruction* definingInst = nullptr);

    /**
     * Register a value use.
     * Call this when a value is used as an operand.
     *
     * @param value The value being used
     * @param user The instruction using the value
     * @param operandIndex Which operand position (0-indexed)
     */
    void registerUse(const Value* value,
                    const Instruction* user,
                    size_t operandIndex);

    // =========================================================================
    // Queries
    // =========================================================================

    /**
     * Get the definition information for a value.
     * @return Pointer to ValueDefinition, or nullptr if not tracked
     */
    const ValueDefinition* getDefinition(const Value* value) const;

    /**
     * Get all uses of a value.
     */
    std::vector<ValueUse> getUses(const Value* value) const;

    /**
     * Check if a value has been defined (registered).
     */
    bool isDefined(const Value* value) const;

    /**
     * Check if value 'def' is defined before value 'use' in program order.
     */
    bool isDefinedBefore(const Value* def, const Value* use) const;

    // =========================================================================
    // SSA Verification
    // =========================================================================

    /**
     * Find all values that are used but not defined.
     * These represent SSA violations.
     */
    std::vector<const Value*> findUndefinedUses() const;

    /**
     * Find all values that are defined but never used.
     * These represent dead code.
     */
    std::vector<const Value*> findDeadValues() const;

    /**
     * Find values with multiple definitions (SSA violation).
     * In proper SSA form, each value should have exactly one definition.
     */
    std::vector<const Value*> findMultipleDefinitions() const;

    // =========================================================================
    // Dominance Integration
    // =========================================================================

    /**
     * Set the dominance tree for dominance checking.
     * The map should map each block to its immediate dominator.
     */
    void setDominanceInfo(const std::map<const BasicBlock*, const BasicBlock*>& idom);

    /**
     * Check if the definition of 'def' dominates the use in 'use'.
     * Requires dominance info to be set via setDominanceInfo().
     *
     * @return true if def dominates use, false otherwise or if dominance info not set
     */
    bool checkDominance(const Value* def, const Instruction* use) const;

    /**
     * Find all uses that violate dominance (use not dominated by definition).
     */
    std::vector<std::pair<const Value*, ValueUse>> findDominanceViolations() const;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * Clear all tracked definitions and uses.
     */
    void clear();

    /**
     * Set the current function context.
     */
    void setFunction(const Function* func);

    /**
     * Get the current function context.
     */
    const Function* getFunction() const { return function_; }

    // =========================================================================
    // Statistics
    // =========================================================================

    size_t getDefinitionCount() const { return definitions_.size(); }
    size_t getUseCount() const;
    size_t getTotalUses() const;

private:
    // Helper to check if block 'a' dominates block 'b'
    bool blockDominates(const BasicBlock* dominator, const BasicBlock* dominated) const;

    // Helper to check if instruction 'a' comes before 'b' in the same block
    bool instructionPrecedes(const Instruction* a, const Instruction* b,
                            const BasicBlock* block) const;

    const Function* function_ = nullptr;
    std::map<const Value*, ValueDefinition> definitions_;
    std::map<const Value*, std::vector<ValueUse>> uses_;
    std::map<const BasicBlock*, const BasicBlock*> idom_;

    uint64_t creationCounter_ = 0;
    uint64_t useCounter_ = 0;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
