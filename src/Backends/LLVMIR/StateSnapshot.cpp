#include "Backends/LLVMIR/StateSnapshot.h"
#include <sstream>
#include <algorithm>
#include <chrono>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// =============================================================================
// InstructionSnapshot
// =============================================================================

bool InstructionSnapshot::operator==(const InstructionSnapshot& other) const {
    return opcode == other.opcode &&
           name == other.name &&
           typeName == other.typeName &&
           operandNames == other.operandNames;
}

std::string InstructionSnapshot::toString() const {
    std::ostringstream os;
    os << "  [" << positionInBlock << "] ";
    if (!name.empty()) {
        os << "%" << name << " = ";
    }
    os << "opcode:" << static_cast<int>(opcode);
    if (!typeName.empty()) {
        os << " " << typeName;
    }
    if (!operandNames.empty()) {
        os << " (";
        for (size_t i = 0; i < operandNames.size(); i++) {
            if (i > 0) os << ", ";
            os << operandNames[i];
        }
        os << ")";
    }
    return os.str();
}

// =============================================================================
// BasicBlockSnapshot
// =============================================================================

bool BasicBlockSnapshot::operator==(const BasicBlockSnapshot& other) const {
    return name == other.name &&
           hasTerminator == other.hasTerminator &&
           instructions == other.instructions;
}

std::string BasicBlockSnapshot::toString() const {
    std::ostringstream os;
    os << "%" << name << ": (" << instructionCount << " instructions";
    if (hasTerminator) os << ", terminated";
    os << ")\n";
    for (const auto& inst : instructions) {
        os << inst.toString() << "\n";
    }
    return os.str();
}

// =============================================================================
// FunctionSnapshot
// =============================================================================

bool FunctionSnapshot::operator==(const FunctionSnapshot& other) const {
    return name == other.name &&
           returnType == other.returnType &&
           paramTypes == other.paramTypes &&
           isDeclaration == other.isDeclaration &&
           blocks == other.blocks;
}

std::string FunctionSnapshot::toString() const {
    std::ostringstream os;
    os << "@" << name << "(";
    for (size_t i = 0; i < paramTypes.size(); i++) {
        if (i > 0) os << ", ";
        os << paramTypes[i];
    }
    os << ") -> " << returnType;
    if (isDeclaration) {
        os << " [declaration]";
    }
    os << "\n";
    for (const auto& bb : blocks) {
        os << bb.toString();
    }
    return os.str();
}

// =============================================================================
// SnapshotDiff
// =============================================================================

bool SnapshotDiff::isEmpty() const {
    return addedFunctions.empty() &&
           removedFunctions.empty() &&
           modifiedFunctions.empty() &&
           addedBlocks.empty() &&
           removedBlocks.empty() &&
           modifiedBlocks.empty() &&
           addedInstructions.empty() &&
           removedInstructions.empty() &&
           modifiedInstructions.empty();
}

std::string SnapshotDiff::toString() const {
    std::ostringstream os;

    if (isEmpty()) {
        os << "No changes detected.\n";
        return os.str();
    }

    if (!addedFunctions.empty()) {
        os << "Added functions:\n";
        for (const auto& f : addedFunctions) {
            os << "  + @" << f << "\n";
        }
    }

    if (!removedFunctions.empty()) {
        os << "Removed functions:\n";
        for (const auto& f : removedFunctions) {
            os << "  - @" << f << "\n";
        }
    }

    if (!modifiedFunctions.empty()) {
        os << "Modified functions:\n";
        for (const auto& f : modifiedFunctions) {
            os << "  ~ @" << f << "\n";

            auto blocksIt = addedBlocks.find(f);
            if (blocksIt != addedBlocks.end()) {
                for (const auto& bb : blocksIt->second) {
                    os << "    + %" << bb << "\n";
                }
            }

            blocksIt = removedBlocks.find(f);
            if (blocksIt != removedBlocks.end()) {
                for (const auto& bb : blocksIt->second) {
                    os << "    - %" << bb << "\n";
                }
            }

            blocksIt = modifiedBlocks.find(f);
            if (blocksIt != modifiedBlocks.end()) {
                for (const auto& bb : blocksIt->second) {
                    os << "    ~ %" << bb << "\n";
                }
            }
        }
    }

    return os.str();
}

// =============================================================================
// StateSnapshot - Capture Methods
// =============================================================================

std::string StateSnapshot::formatType(const Type* type) {
    if (!type) return "<null>";
    if (type->isVoid()) return "void";
    if (type->isInteger()) {
        auto* intTy = static_cast<const IntegerType*>(type);
        return "i" + std::to_string(intTy->getBitWidth());
    }
    if (type->isFloat()) {
        auto* floatTy = static_cast<const FloatType*>(type);
        return floatTy->isDouble() ? "double" : "float";
    }
    if (type->isPointer()) return "ptr";
    if (type->isArray()) return "array";
    if (type->isStruct()) {
        auto* structTy = static_cast<const StructType*>(type);
        return std::string(structTy->getName().empty() ? "struct" : structTy->getName());
    }
    if (type->isFunction()) return "fn";
    return "<type>";
}

std::string StateSnapshot::formatValue(const Value* value) {
    if (!value) return "<null>";

    if (auto* constInt = dynamic_cast<const ConstantInt*>(value)) {
        return std::to_string(constInt->getValue());
    }
    if (auto* constFP = dynamic_cast<const ConstantFP*>(value)) {
        return std::to_string(constFP->getValue());
    }
    if (auto* arg = dynamic_cast<const Argument*>(value)) {
        return "%" + std::string(arg->getName());
    }
    if (auto* inst = dynamic_cast<const Instruction*>(value)) {
        return "%inst_" + std::to_string(reinterpret_cast<uintptr_t>(inst));
    }
    if (auto* bb = dynamic_cast<const BasicBlock*>(value)) {
        return "%" + std::string(bb->getName());
    }
    if (auto* func = dynamic_cast<const Function*>(value)) {
        return "@" + std::string(func->getName());
    }
    if (auto* global = dynamic_cast<const GlobalVariable*>(value)) {
        return "@" + std::string(global->getName());
    }

    return "<value>";
}

InstructionSnapshot StateSnapshot::captureInstruction(const Instruction* inst) {
    InstructionSnapshot snapshot;
    if (!inst) return snapshot;

    snapshot.opcode = inst->getOpcode();
    snapshot.typeName = formatType(inst->getType());

    // Simplified capture - just opcode and type for now
    // Detailed operand capture would require extensive API knowledge
    // The main purpose is state change detection, not detailed formatting

    return snapshot;
}

BasicBlockSnapshot StateSnapshot::captureBasicBlock(const BasicBlock* bb) {
    BasicBlockSnapshot snapshot;
    if (!bb) return snapshot;

    snapshot.name = std::string(bb->getName());
    snapshot.hasTerminator = (bb->getTerminator() != nullptr);

    size_t position = 0;
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        auto instSnap = captureInstruction(*it);  // *it returns Instruction*
        instSnap.positionInBlock = position++;
        snapshot.instructions.push_back(std::move(instSnap));
    }

    snapshot.instructionCount = snapshot.instructions.size();
    return snapshot;
}

StateSnapshot StateSnapshot::captureFunction(const Function* func) {
    StateSnapshot snapshot;
    if (!func) return snapshot;

    snapshot.timestamp_ = std::chrono::steady_clock::now().time_since_epoch().count();
    snapshot.isModuleLevel_ = false;

    FunctionSnapshot funcSnap;
    funcSnap.name = std::string(func->getName());
    funcSnap.isDeclaration = func->isDeclaration();

    if (auto* funcType = func->getFunctionType()) {
        funcSnap.returnType = formatType(funcType->getReturnType());
        for (size_t i = 0; i < funcType->getNumParams(); i++) {
            funcSnap.paramTypes.push_back(formatType(funcType->getParamType(i)));
        }
    }

    for (const auto& bb : func->getBasicBlocks()) {
        funcSnap.blocks.push_back(captureBasicBlock(bb.get()));
    }

    snapshot.functions_.push_back(std::move(funcSnap));
    return snapshot;
}

StateSnapshot StateSnapshot::captureModule(const Module& module) {
    StateSnapshot snapshot;
    snapshot.timestamp_ = std::chrono::steady_clock::now().time_since_epoch().count();
    snapshot.isModuleLevel_ = true;

    for (const auto& [name, func] : module.getFunctions()) {
        FunctionSnapshot funcSnap;
        funcSnap.name = std::string(func->getName());
        funcSnap.isDeclaration = func->isDeclaration();

        if (auto* funcType = func->getFunctionType()) {
            funcSnap.returnType = formatType(funcType->getReturnType());
            for (size_t i = 0; i < funcType->getNumParams(); i++) {
                funcSnap.paramTypes.push_back(formatType(funcType->getParamType(i)));
            }
        }

        for (const auto& bb : func->getBasicBlocks()) {
            funcSnap.blocks.push_back(captureBasicBlock(bb.get()));
        }

        snapshot.functions_.push_back(std::move(funcSnap));
    }

    for (const auto& [name, global] : module.getGlobals()) {
        snapshot.globals_[std::string(global->getName())] = formatType(global->getType());
    }

    return snapshot;
}

// =============================================================================
// StateSnapshot - Comparison
// =============================================================================

SnapshotDiff StateSnapshot::compare(const StateSnapshot& other) const {
    SnapshotDiff diff;

    // Build maps for fast lookup
    std::map<std::string, const FunctionSnapshot*> thisFuncs;
    std::map<std::string, const FunctionSnapshot*> otherFuncs;

    for (const auto& f : functions_) {
        thisFuncs[f.name] = &f;
    }
    for (const auto& f : other.functions_) {
        otherFuncs[f.name] = &f;
    }

    // Find added/removed functions
    for (const auto& [name, _] : otherFuncs) {
        if (thisFuncs.find(name) == thisFuncs.end()) {
            diff.addedFunctions.push_back(name);
        }
    }
    for (const auto& [name, _] : thisFuncs) {
        if (otherFuncs.find(name) == otherFuncs.end()) {
            diff.removedFunctions.push_back(name);
        }
    }

    // Find modified functions
    for (const auto& [name, thisFunc] : thisFuncs) {
        auto otherIt = otherFuncs.find(name);
        if (otherIt == otherFuncs.end()) continue;

        const FunctionSnapshot* otherFunc = otherIt->second;
        if (*thisFunc != *otherFunc) {
            diff.modifiedFunctions.push_back(name);

            // Compare blocks
            std::map<std::string, const BasicBlockSnapshot*> thisBlocks;
            std::map<std::string, const BasicBlockSnapshot*> otherBlocks;

            for (const auto& bb : thisFunc->blocks) {
                thisBlocks[bb.name] = &bb;
            }
            for (const auto& bb : otherFunc->blocks) {
                otherBlocks[bb.name] = &bb;
            }

            for (const auto& [bbName, _] : otherBlocks) {
                if (thisBlocks.find(bbName) == thisBlocks.end()) {
                    diff.addedBlocks[name].push_back(bbName);
                }
            }
            for (const auto& [bbName, _] : thisBlocks) {
                if (otherBlocks.find(bbName) == otherBlocks.end()) {
                    diff.removedBlocks[name].push_back(bbName);
                }
            }
            for (const auto& [bbName, thisBB] : thisBlocks) {
                auto otherBBIt = otherBlocks.find(bbName);
                if (otherBBIt != otherBlocks.end() && *thisBB != *otherBBIt->second) {
                    diff.modifiedBlocks[name].push_back(bbName);
                }
            }
        }
    }

    return diff;
}

// =============================================================================
// StateSnapshot - Serialization
// =============================================================================

std::string StateSnapshot::toString() const {
    std::ostringstream os;
    os << "=== StateSnapshot (timestamp: " << timestamp_ << ") ===\n";
    os << "Module level: " << (isModuleLevel_ ? "yes" : "no") << "\n";
    os << "Functions: " << functions_.size() << "\n\n";

    for (const auto& func : functions_) {
        os << func.toString() << "\n";
    }

    if (!globals_.empty()) {
        os << "Globals:\n";
        for (const auto& [name, type] : globals_) {
            os << "  @" << name << ": " << type << "\n";
        }
    }

    return os.str();
}

// =============================================================================
// CheckpointManager
// =============================================================================

CheckpointManager::CheckpointManager(Module& module)
    : module_(module) {}

void CheckpointManager::createCheckpoint(const std::string& name) {
    auto snapshot = StateSnapshot::captureModule(module_);
    namedCheckpoints_.erase(name);
    namedCheckpoints_.emplace(name, Checkpoint(name, std::move(snapshot)));
}

size_t CheckpointManager::createCheckpoint() {
    auto snapshot = StateSnapshot::captureModule(module_);
    size_t id = nextAnonymousId_++;
    anonymousCheckpoints_.emplace_back(
        "anon_" + std::to_string(id), std::move(snapshot));
    return id;
}

SnapshotDiff CheckpointManager::getDiffFromCheckpoint(const std::string& name) const {
    auto it = namedCheckpoints_.find(name);
    if (it == namedCheckpoints_.end()) {
        return SnapshotDiff(); // Empty diff
    }

    auto current = StateSnapshot::captureModule(module_);
    return it->second.snapshot.compare(current);
}

SnapshotDiff CheckpointManager::getDiffFromCheckpoint(size_t id) const {
    if (id >= anonymousCheckpoints_.size()) {
        return SnapshotDiff();
    }

    auto current = StateSnapshot::captureModule(module_);
    return anonymousCheckpoints_[id].snapshot.compare(current);
}

bool CheckpointManager::hasCheckpoint(const std::string& name) const {
    return namedCheckpoints_.find(name) != namedCheckpoints_.end();
}

void CheckpointManager::removeCheckpoint(const std::string& name) {
    namedCheckpoints_.erase(name);
}

void CheckpointManager::clearCheckpoints() {
    namedCheckpoints_.clear();
    anonymousCheckpoints_.clear();
    nextAnonymousId_ = 0;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
