#pragma once

#include "TypedModule.h"
#include "TypedInstructions.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>

namespace XXML {
namespace Backends {
namespace LLVMIR {

/**
 * Lightweight snapshot of an instruction for comparison
 */
struct InstructionSnapshot {
    Opcode opcode = Opcode::Alloca;
    std::string name;
    std::string typeName;
    std::vector<std::string> operandNames;
    size_t positionInBlock = 0;

    bool operator==(const InstructionSnapshot& other) const;
    bool operator!=(const InstructionSnapshot& other) const { return !(*this == other); }

    std::string toString() const;
};

/**
 * Snapshot of a basic block
 */
struct BasicBlockSnapshot {
    std::string name;
    std::vector<InstructionSnapshot> instructions;
    bool hasTerminator = false;
    size_t instructionCount = 0;

    bool operator==(const BasicBlockSnapshot& other) const;
    bool operator!=(const BasicBlockSnapshot& other) const { return !(*this == other); }

    std::string toString() const;
};

/**
 * Snapshot of a function
 */
struct FunctionSnapshot {
    std::string name;
    std::string returnType;
    std::vector<std::string> paramTypes;
    std::vector<BasicBlockSnapshot> blocks;
    bool isDeclaration = false;

    bool operator==(const FunctionSnapshot& other) const;
    bool operator!=(const FunctionSnapshot& other) const { return !(*this == other); }

    std::string toString() const;
};

/**
 * Diff result between two snapshots
 */
struct SnapshotDiff {
    // Function-level changes
    std::vector<std::string> addedFunctions;
    std::vector<std::string> removedFunctions;
    std::vector<std::string> modifiedFunctions;

    // Basic block changes (per function)
    std::map<std::string, std::vector<std::string>> addedBlocks;
    std::map<std::string, std::vector<std::string>> removedBlocks;
    std::map<std::string, std::vector<std::string>> modifiedBlocks;

    // Instruction-level changes (per block)
    std::map<std::string, std::vector<std::string>> addedInstructions;
    std::map<std::string, std::vector<std::string>> removedInstructions;
    std::map<std::string, std::vector<std::string>> modifiedInstructions;

    bool isEmpty() const;
    std::string toString() const;
};

/**
 * StateSnapshot - Captures the state of the IR for comparison and undo
 *
 * This class provides:
 * - Capture: Take a snapshot of current IR state
 * - Compare: Diff two snapshots to see what changed
 * - toString: Human-readable representation for debugging
 *
 * Note: This captures a lightweight representation, not the full IR.
 * Full undo would require more sophisticated memory management.
 */
class StateSnapshot {
public:
    StateSnapshot() = default;

    // =========================================================================
    // Capture Methods
    // =========================================================================

    /**
     * Capture the current state of a function.
     */
    static StateSnapshot captureFunction(const Function* func);

    /**
     * Capture the current state of a module.
     */
    static StateSnapshot captureModule(const Module& module);

    /**
     * Capture the current state of a basic block.
     */
    static BasicBlockSnapshot captureBasicBlock(const BasicBlock* bb);

    /**
     * Capture the current state of an instruction.
     */
    static InstructionSnapshot captureInstruction(const Instruction* inst);

    // =========================================================================
    // Comparison
    // =========================================================================

    /**
     * Compare this snapshot with another.
     * Returns a diff describing what changed.
     */
    SnapshotDiff compare(const StateSnapshot& other) const;

    // =========================================================================
    // Accessors
    // =========================================================================

    const std::vector<FunctionSnapshot>& functions() const { return functions_; }
    uint64_t getTimestamp() const { return timestamp_; }
    bool isModuleLevel() const { return isModuleLevel_; }

    // =========================================================================
    // Serialization
    // =========================================================================

    /**
     * Human-readable string representation.
     */
    std::string toString() const;

private:
    std::vector<FunctionSnapshot> functions_;
    std::map<std::string, std::string> globals_;
    uint64_t timestamp_ = 0;
    bool isModuleLevel_ = false;

    static std::string formatType(const Type* type);
    static std::string formatValue(const Value* value);
};

/**
 * Checkpoint - A named snapshot point for potential rollback
 */
struct Checkpoint {
    std::string name;
    StateSnapshot snapshot;
    uint64_t creationTime;

    Checkpoint(const std::string& n, StateSnapshot&& s)
        : name(n), snapshot(std::move(s)),
          creationTime(std::chrono::steady_clock::now().time_since_epoch().count()) {}
};

/**
 * CheckpointManager - Manages checkpoints for undo capability
 *
 * Usage:
 *   manager.createCheckpoint("before_loop_codegen");
 *   // ... generate IR ...
 *   if (error) {
 *       auto diff = manager.getDiffFromCheckpoint("before_loop_codegen");
 *       // Report what was added since checkpoint
 *   }
 */
class CheckpointManager {
public:
    explicit CheckpointManager(Module& module);

    /**
     * Create a checkpoint with the given name.
     * If a checkpoint with this name exists, it's replaced.
     */
    void createCheckpoint(const std::string& name);

    /**
     * Create an anonymous checkpoint and return its ID.
     */
    size_t createCheckpoint();

    /**
     * Get the diff from a named checkpoint to current state.
     */
    SnapshotDiff getDiffFromCheckpoint(const std::string& name) const;

    /**
     * Get the diff from a checkpoint ID to current state.
     */
    SnapshotDiff getDiffFromCheckpoint(size_t id) const;

    /**
     * Check if a named checkpoint exists.
     */
    bool hasCheckpoint(const std::string& name) const;

    /**
     * Remove a named checkpoint.
     */
    void removeCheckpoint(const std::string& name);

    /**
     * Clear all checkpoints.
     */
    void clearCheckpoints();

    /**
     * Get checkpoint count.
     */
    size_t getCheckpointCount() const { return namedCheckpoints_.size() + anonymousCheckpoints_.size(); }

private:
    Module& module_;
    std::map<std::string, Checkpoint> namedCheckpoints_;
    std::vector<Checkpoint> anonymousCheckpoints_;
    size_t nextAnonymousId_ = 0;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
