#include "Backends/LLVMIR/IRVerifier.h"
#include <algorithm>
#include <queue>
#include <sstream>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// =============================================================================
// VerificationError
// =============================================================================

std::string VerificationError::toString() const {
    std::ostringstream os;
    os << "[" << IRVerifier::errorKindToString(kind) << "] " << message;

    if (function) {
        os << "\n  Function: @" << function->getName();
    }
    if (block) {
        os << "\n  Block: %" << block->getName();
    }
    if (instruction) {
        os << "\n  Instruction: (opcode " << static_cast<int>(instruction->getOpcode()) << ")";
    }
    if (relatedValue) {
        os << "\n  Related value: " << relatedValue;
    }

    return os.str();
}

// =============================================================================
// IRVerifier - Construction
// =============================================================================

IRVerifier::IRVerifier(Module& module)
    : module_(module) {}

// =============================================================================
// Error Kind String Conversion
// =============================================================================

const char* IRVerifier::errorKindToString(VerificationErrorKind kind) {
    switch (kind) {
        case VerificationErrorKind::NullOperand:
            return "NULL_OPERAND";
        case VerificationErrorKind::TypeMismatch:
            return "TYPE_MISMATCH";
        case VerificationErrorKind::InstructionAfterTerminator:
            return "INSTRUCTION_AFTER_TERMINATOR";
        case VerificationErrorKind::InvalidOpcode:
            return "INVALID_OPCODE";
        case VerificationErrorKind::MissingTerminator:
            return "MISSING_TERMINATOR";
        case VerificationErrorKind::MultipleTerminators:
            return "MULTIPLE_TERMINATORS";
        case VerificationErrorKind::UnreachableBlock:
            return "UNREACHABLE_BLOCK";
        case VerificationErrorKind::UndefinedValue:
            return "UNDEFINED_VALUE";
        case VerificationErrorKind::DominanceViolation:
            return "DOMINANCE_VIOLATION";
        case VerificationErrorKind::AllocaNotInEntry:
            return "ALLOCA_NOT_IN_ENTRY";
        case VerificationErrorKind::PHIMissingPredecessor:
            return "PHI_MISSING_PREDECESSOR";
        case VerificationErrorKind::PHITypeMismatch:
            return "PHI_TYPE_MISMATCH";
        case VerificationErrorKind::ReturnTypeMismatch:
            return "RETURN_TYPE_MISMATCH";
        case VerificationErrorKind::DuplicateFunctionName:
            return "DUPLICATE_FUNCTION_NAME";
        case VerificationErrorKind::DuplicateGlobalName:
            return "DUPLICATE_GLOBAL_NAME";
        case VerificationErrorKind::UnresolvedCall:
            return "UNRESOLVED_CALL";
        case VerificationErrorKind::TypeInconsistency:
            return "TYPE_INCONSISTENCY";
        default:
            return "UNKNOWN_ERROR";
    }
}

// =============================================================================
// Error Reporting
// =============================================================================

void IRVerifier::reportError(VerificationErrorKind kind,
                              const std::string& message,
                              const Instruction* inst,
                              const BasicBlock* block,
                              const Function* func,
                              const Value* related) {
    VerificationError error;
    error.kind = kind;
    error.message = message;
    error.instruction = inst;
    error.block = block ? block : (inst ? inst->getParent() : nullptr);
    error.function = func ? func : currentFunction_;
    error.relatedValue = related;

    errors_.push_back(error);

    if (abortOnFailure_) {
        dumpErrors();
        abort(message);
    }
}

void IRVerifier::dumpErrors(std::ostream& os) const {
    if (errors_.empty()) {
        return;
    }

    os << "\n";
    os << "=============================================================================\n";
    os << "                    LLVM IR VERIFICATION FAILED\n";
    os << "=============================================================================\n\n";
    os << "Found " << errors_.size() << " error(s):\n\n";

    for (size_t i = 0; i < errors_.size(); ++i) {
        os << "--- Error " << (i + 1) << " ---\n";
        os << errors_[i].toString() << "\n\n";
    }

    os << "=============================================================================\n";
    os << "                         ABORTING COMPILATION\n";
    os << "=============================================================================\n\n";
}

[[noreturn]] void IRVerifier::abort(const std::string& message) {
    std::cerr << "\n*** IR Verification Failed ***\n";
    std::cerr << message << "\n";
    std::cerr << "Aborting...\n\n";
    std::abort();
}

// =============================================================================
// Formatting Helpers
// =============================================================================

std::string IRVerifier::formatInstruction(const Instruction* inst) const {
    if (!inst) return "<null>";

    std::ostringstream os;
    os << "opcode=" << static_cast<int>(inst->getOpcode());
    if (inst->getType()) {
        os << " type=" << formatType(inst->getType());
    }
    return os.str();
}

std::string IRVerifier::formatBasicBlock(const BasicBlock* bb) const {
    if (!bb) return "<null>";
    return "%" + std::string(bb->getName());
}

std::string IRVerifier::formatFunction(const Function* func) const {
    if (!func) return "<null>";
    return "@" + std::string(func->getName());
}

std::string IRVerifier::formatValue(const Value* val) const {
    if (!val) return "<null>";

    // Try to get a meaningful representation
    if (auto* inst = dynamic_cast<const Instruction*>(val)) {
        return "instruction:" + std::to_string(static_cast<int>(inst->getOpcode()));
    }
    if (auto* arg = dynamic_cast<const Argument*>(val)) {
        return "arg:" + std::string(arg->getName());
    }
    if (auto* constInt = dynamic_cast<const ConstantInt*>(val)) {
        return "const:" + std::to_string(constInt->getValue());
    }

    return "<value>";
}

std::string IRVerifier::formatType(const Type* type) const {
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
        return structTy->getName().empty() ? "struct" : structTy->getName();
    }
    if (type->isFunction()) return "fn";

    return "<type>";
}

// =============================================================================
// Per-Instruction Verification
// =============================================================================

void IRVerifier::verifyInstruction(const Instruction* inst) {
    if (!inst) {
        reportError(VerificationErrorKind::NullOperand,
                    "Null instruction passed to verifier");
        return;
    }

    checkNullOperands(inst);
    checkOperandTypes(inst);
    checkTerminatorPlacement(inst);
}

void IRVerifier::checkNullOperands(const Instruction* inst) {
    // Check based on instruction type
    switch (inst->getOpcode()) {
        case Opcode::Store: {
            auto* store = static_cast<const StoreInst*>(inst);
            if (!store->getPointer().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Store instruction has null pointer operand",
                            inst);
            }
            if (!store->getValue().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Store instruction has null value operand",
                            inst);
            }
            break;
        }

        case Opcode::Load: {
            auto* load = static_cast<const LoadInst*>(inst);
            if (!load->getPointer().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Load instruction has null pointer operand",
                            inst);
            }
            break;
        }

        // Note: IntBinaryOp doesn't have a single opcode, it uses Add, Sub, Mul, etc.
        // These are checked via the typed system already

        // Note: FloatBinaryOp doesn't have a single opcode, it uses FAdd, FSub, FMul, etc.
        // These are checked via the typed system already

        case Opcode::CondBr: {
            auto* condBr = static_cast<const CondBranchInst*>(inst);
            if (!condBr->getCondition().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Conditional branch has null condition",
                            inst);
            }
            if (!condBr->getTrueBranch()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Conditional branch has null true destination",
                            inst);
            }
            if (!condBr->getFalseBranch()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Conditional branch has null false destination",
                            inst);
            }
            break;
        }

        case Opcode::Br: {
            auto* br = static_cast<const BranchInst*>(inst);
            if (!br->getDestination()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Unconditional branch has null target",
                            inst);
            }
            break;
        }

        case Opcode::Call: {
            auto* call = static_cast<const CallInst*>(inst);
            if (!call->getCallee()) {
                reportError(VerificationErrorKind::NullOperand,
                            "Call instruction has null callee",
                            inst);
            }
            break;
        }

        case Opcode::ICmp: {
            auto* icmp = static_cast<const ICmpInst*>(inst);
            if (!icmp->getLHS().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "ICmp has null LHS operand",
                            inst);
            }
            if (!icmp->getRHS().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "ICmp has null RHS operand",
                            inst);
            }
            break;
        }

        case Opcode::FCmp: {
            auto* fcmp = static_cast<const FCmpInst*>(inst);
            if (!fcmp->getLHS().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "FCmp has null LHS operand",
                            inst);
            }
            if (!fcmp->getRHS().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "FCmp has null RHS operand",
                            inst);
            }
            break;
        }

        case Opcode::Ret: {
            // Could be ReturnVoidInst or ReturnValueInst - both use Opcode::Ret
            if (auto* retVal = dynamic_cast<const ReturnValueInst*>(inst)) {
                if (!retVal->getValue().raw()) {
                    reportError(VerificationErrorKind::NullOperand,
                                "ReturnValue instruction has null value",
                                inst);
                }
            }
            // ReturnVoidInst has no value to check
            break;
        }

        case Opcode::GetElementPtr: {
            auto* gep = static_cast<const GetElementPtrInst*>(inst);
            if (!gep->getBasePointer().raw()) {
                reportError(VerificationErrorKind::NullOperand,
                            "GEP has null base pointer",
                            inst);
            }
            break;
        }

        default:
            // Other instructions - basic null check handled by typed system
            break;
    }
}

void IRVerifier::checkOperandTypes(const Instruction* inst) {
    // Type checking based on instruction kind
    // Note: Integer and float binary operations (Add, Sub, FAdd, FSub, etc.)
    // are already type-checked by the typed builder system via IntValue/FloatValue wrappers.
    // We only need to check instructions that can have type mismatches at the IR level.

    switch (inst->getOpcode()) {
        case Opcode::Store: {
            auto* store = static_cast<const StoreInst*>(inst);
            auto ptrVal = store->getPointer();
            if (ptrVal.raw() && ptrVal.raw()->getType()) {
                if (!ptrVal.raw()->getType()->isPointer()) {
                    reportError(VerificationErrorKind::TypeMismatch,
                                "Store destination is not a pointer type",
                                inst);
                }
            }
            break;
        }

        case Opcode::Load: {
            auto* load = static_cast<const LoadInst*>(inst);
            auto ptrVal = load->getPointer();
            if (ptrVal.raw() && ptrVal.raw()->getType()) {
                if (!ptrVal.raw()->getType()->isPointer()) {
                    reportError(VerificationErrorKind::TypeMismatch,
                                "Load source is not a pointer type",
                                inst);
                }
            }
            break;
        }

        case Opcode::CondBr: {
            auto* condBr = static_cast<const CondBranchInst*>(inst);
            auto condVal = condBr->getCondition();
            if (condVal.raw() && condVal.raw()->getType()) {
                auto* condType = condVal.raw()->getType();
                // Condition must be i1
                if (!condType->isInteger()) {
                    reportError(VerificationErrorKind::TypeMismatch,
                                "Conditional branch condition is not an integer type",
                                inst);
                } else {
                    auto* intType = static_cast<const IntegerType*>(condType);
                    if (intType->getBitWidth() != 1) {
                        reportError(VerificationErrorKind::TypeMismatch,
                                    "Conditional branch condition is not i1 (is i" +
                                    std::to_string(intType->getBitWidth()) + ")",
                                    inst);
                    }
                }
            }
            break;
        }

        default:
            break;
    }
}

void IRVerifier::checkTerminatorPlacement(const Instruction* inst) {
    const BasicBlock* block = inst->getParent();
    if (!block) {
        return; // Orphaned instruction, can't check
    }

    // If this instruction is a terminator, it must be the last instruction
    if (inst->isTerminator()) {
        // Check if there are instructions after this one
        bool foundSelf = false;
        for (auto it = block->begin(); it != block->end(); ++it) {
            if (*it == inst) {
                foundSelf = true;
                continue;
            }
            if (foundSelf) {
                reportError(VerificationErrorKind::InstructionAfterTerminator,
                            "Found instruction after terminator in basic block",
                            *it, block);
            }
        }
    } else {
        // Non-terminator instruction should not appear after a terminator
        bool afterTerminator = false;
        for (auto it = block->begin(); it != block->end(); ++it) {
            if ((*it)->isTerminator() && *it != inst) {
                afterTerminator = true;
            }
            if (afterTerminator && *it == inst) {
                reportError(VerificationErrorKind::InstructionAfterTerminator,
                            "Non-terminator instruction appears after terminator",
                            inst, block);
            }
        }
    }
}

// =============================================================================
// Per-Function Verification
// =============================================================================

void IRVerifier::verifyFunction(const Function* func) {
    if (!func) {
        reportError(VerificationErrorKind::NullOperand,
                    "Null function passed to verifier");
        return;
    }

    currentFunction_ = func;

    // Skip declarations (no body)
    if (func->isDeclaration()) {
        currentFunction_ = nullptr;
        return;
    }

    // Compute dominators for SSA checks
    computeDominators(func);

    // Run all function-level checks
    checkBasicBlockTerminators(func);
    checkAllocaPlacement(func);
    checkPHINodes(func);
    checkReturnTypes(func);
    checkSSAForm(func);
    checkDominance(func);
    checkReachability(func);

    currentFunction_ = nullptr;
}

void IRVerifier::checkBasicBlockTerminators(const Function* func) {
    for (const auto& bb : func->getBasicBlocks()) {
        bool hasTerminator = false;
        int terminatorCount = 0;

        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->isTerminator()) {
                hasTerminator = true;
                terminatorCount++;
            }
        }

        if (!hasTerminator) {
            reportError(VerificationErrorKind::MissingTerminator,
                        "Basic block has no terminator instruction",
                        nullptr, bb.get(), func);
        }

        if (terminatorCount > 1) {
            reportError(VerificationErrorKind::MultipleTerminators,
                        "Basic block has multiple terminator instructions (" +
                        std::to_string(terminatorCount) + ")",
                        nullptr, bb.get(), func);
        }
    }
}

void IRVerifier::checkAllocaPlacement(const Function* func) {
    if (func->getBasicBlocks().empty()) return;

    const BasicBlock* entryBlock = func->getBasicBlocks().front().get();

    for (const auto& bb : func->getBasicBlocks()) {
        if (bb.get() == entryBlock) continue; // Allocas OK in entry

        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->getOpcode() == Opcode::Alloca) {
                reportError(VerificationErrorKind::AllocaNotInEntry,
                            "Alloca instruction found outside entry block",
                            *it, bb.get(), func);
            }
        }
    }
}

void IRVerifier::checkPHINodes(const Function* func) {
    // PHI nodes should be at the beginning of blocks and have correct types
    for (const auto& bb : func->getBasicBlocks()) {
        bool pastPHIs = false;

        for (auto it = bb->begin(); it != bb->end(); ++it) {
            bool isPHI = ((*it)->getOpcode() == Opcode::PHI);

            if (isPHI && pastPHIs) {
                reportError(VerificationErrorKind::PHITypeMismatch,
                            "PHI node found after non-PHI instruction",
                            *it, bb.get(), func);
            }

            if (!isPHI && !pastPHIs) {
                pastPHIs = true;
            }

            // TODO: Check PHI incoming blocks match predecessors
            // TODO: Check all PHI incoming values have same type
        }
    }
}

void IRVerifier::checkReturnTypes(const Function* func) {
    auto* funcType = func->getFunctionType();
    if (!funcType) return;

    auto* returnType = funcType->getReturnType();

    // Check return instructions match declared return type
    for (const auto& bb : func->getBasicBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->getOpcode() == Opcode::Ret) {
                // Try to cast to ReturnValueInst first
                if (auto* retVal = dynamic_cast<const ReturnValueInst*>(*it)) {
                    // This is a return with value
                    if (returnType && returnType->isVoid()) {
                        reportError(VerificationErrorKind::ReturnTypeMismatch,
                                    "Return with value in void function",
                                    *it, bb.get(), func);
                    }
                } else {
                    // This is a void return
                    if (returnType && !returnType->isVoid()) {
                        reportError(VerificationErrorKind::ReturnTypeMismatch,
                                    "Void return in non-void function",
                                    *it, bb.get(), func);
                    }
                }
            }
        }
    }
}

void IRVerifier::checkSSAForm(const Function* func) {
    // TODO: Implement with ValueTracker integration
    // For now, basic definition tracking
}

void IRVerifier::checkDominance(const Function* func) {
    // TODO: Implement dominance checking
    // Requires ValueTracker to know where values are defined and used
}

void IRVerifier::checkReachability(const Function* func) {
    if (func->getBasicBlocks().empty()) return;

    // BFS from entry block
    std::set<const BasicBlock*> reachable;
    std::queue<const BasicBlock*> worklist;

    worklist.push(func->getBasicBlocks().front().get());
    reachable.insert(func->getBasicBlocks().front().get());

    while (!worklist.empty()) {
        const BasicBlock* bb = worklist.front();
        worklist.pop();

        // Get successors from terminator
        const Instruction* term = bb->getTerminator();
        if (!term) continue;

        std::vector<const BasicBlock*> successors;
        if (term->getOpcode() == Opcode::Br) {
            auto* br = static_cast<const BranchInst*>(term);
            if (br->getDestination()) successors.push_back(br->getDestination());
        } else if (term->getOpcode() == Opcode::CondBr) {
            auto* condBr = static_cast<const CondBranchInst*>(term);
            if (condBr->getTrueBranch()) successors.push_back(condBr->getTrueBranch());
            if (condBr->getFalseBranch()) successors.push_back(condBr->getFalseBranch());
        }

        for (const BasicBlock* succ : successors) {
            if (reachable.find(succ) == reachable.end()) {
                reachable.insert(succ);
                worklist.push(succ);
            }
        }
    }

    // Report unreachable blocks (as warning, not error by default)
    for (const auto& bb : func->getBasicBlocks()) {
        if (reachable.find(bb.get()) == reachable.end()) {
            // Note: Not using reportError to avoid abort on unreachable blocks
            // This is more of a warning
            std::cerr << "[WARNING] Unreachable block: %" << std::string(bb->getName())
                      << " in function @" << std::string(func->getName()) << "\n";
        }
    }
}

// =============================================================================
// Per-Module Verification
// =============================================================================

void IRVerifier::verifyModule() {
    checkFunctionNameUniqueness();
    checkGlobalNameUniqueness();
    checkCallTargets();
    checkTypeConsistency();

    // Verify all functions
    for (const auto& [name, func] : module_.getFunctions()) {
        verifyFunction(func.get());
    }
}

void IRVerifier::checkFunctionNameUniqueness() {
    std::set<std::string> names;

    for (const auto& [funcName, func] : module_.getFunctions()) {
        std::string name(func->getName());
        if (names.find(name) != names.end()) {
            reportError(VerificationErrorKind::DuplicateFunctionName,
                        "Duplicate function name: @" + name,
                        nullptr, nullptr, func.get());
        }
        names.insert(name);
    }
}

void IRVerifier::checkGlobalNameUniqueness() {
    std::set<std::string> names;

    for (const auto& [globalName, global] : module_.getGlobals()) {
        std::string name(global->getName());
        if (names.find(name) != names.end()) {
            reportError(VerificationErrorKind::DuplicateGlobalName,
                        "Duplicate global variable name: @" + name);
        }
        names.insert(name);
    }
}

void IRVerifier::checkCallTargets() {
    // Collect all defined function names
    std::set<std::string> definedFunctions;
    for (const auto& [fname, func] : module_.getFunctions()) {
        definedFunctions.insert(std::string(func->getName()));
    }

    // Check all call instructions
    for (const auto& [fname2, func] : module_.getFunctions()) {
        for (const auto& bb : func->getBasicBlocks()) {
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                if ((*it)->getOpcode() == Opcode::Call) {
                    auto* call = static_cast<const CallInst*>(*it);
                    auto* callee = call->getCallee();

                    if (auto* calleeFunc = dynamic_cast<const Function*>(callee)) {
                        std::string calleeName(calleeFunc->getName());
                        if (definedFunctions.find(calleeName) == definedFunctions.end()) {
                            reportError(VerificationErrorKind::UnresolvedCall,
                                        "Call to undefined function: @" + calleeName,
                                        *it, bb.get(), func.get());
                        }
                    }
                }
            }
        }
    }
}

void IRVerifier::checkTypeConsistency() {
    // TODO: Check struct types are used consistently across module
}

// =============================================================================
// Dominance Computation
// =============================================================================

std::map<const BasicBlock*, const BasicBlock*> IRVerifier::computeDominators(const Function* func) {
    idom_.clear();
    domTree_.clear();

    if (func->getBasicBlocks().empty()) return idom_;

    const BasicBlock* entry = func->getBasicBlocks().front().get();

    // Initialize: entry dominates only itself
    // All other blocks start with unknown dominator

    // Collect all blocks
    std::vector<const BasicBlock*> blocks;
    for (const auto& bb : func->getBasicBlocks()) {
        blocks.push_back(bb.get());
    }

    // Simple iterative dominance algorithm
    // Initialize all blocks except entry as dominated by all
    std::map<const BasicBlock*, std::set<const BasicBlock*>> dom;

    for (const BasicBlock* bb : blocks) {
        if (bb == entry) {
            dom[bb].insert(bb);
        } else {
            // Initially, all blocks dominate this block
            for (const BasicBlock* other : blocks) {
                dom[bb].insert(other);
            }
        }
    }

    // Iterate until fixed point
    bool changed = true;
    while (changed) {
        changed = false;

        for (const BasicBlock* bb : blocks) {
            if (bb == entry) continue;

            // Get predecessors
            std::vector<const BasicBlock*> preds;
            // TODO: Need predecessor information - for now skip

            if (preds.empty()) continue;

            // New dom(bb) = {bb} union (intersection of dom(pred) for all preds)
            std::set<const BasicBlock*> newDom;
            newDom.insert(bb);

            // Intersection of predecessors' dominators
            bool first = true;
            for (const BasicBlock* pred : preds) {
                if (first) {
                    newDom.insert(dom[pred].begin(), dom[pred].end());
                    first = false;
                } else {
                    std::set<const BasicBlock*> intersection;
                    std::set_intersection(
                        newDom.begin(), newDom.end(),
                        dom[pred].begin(), dom[pred].end(),
                        std::inserter(intersection, intersection.begin())
                    );
                    newDom = std::move(intersection);
                    newDom.insert(bb);
                }
            }

            if (newDom != dom[bb]) {
                dom[bb] = std::move(newDom);
                changed = true;
            }
        }
    }

    // Extract immediate dominators from dominance sets
    for (const BasicBlock* bb : blocks) {
        if (bb == entry) {
            idom_[bb] = nullptr; // Entry has no immediate dominator
            continue;
        }

        // idom(bb) = the closest dominator that is not bb itself
        const BasicBlock* idomCandidate = nullptr;
        for (const BasicBlock* d : dom[bb]) {
            if (d == bb) continue;

            // Check if d is a candidate for idom
            bool isIdom = true;
            for (const BasicBlock* other : dom[bb]) {
                if (other == bb || other == d) continue;
                // If other dominates d, then d is not the immediate dominator
                if (dom[d].find(other) != dom[d].end()) {
                    isIdom = false;
                    break;
                }
            }

            if (isIdom) {
                idomCandidate = d;
                break;
            }
        }

        idom_[bb] = idomCandidate;

        // Build dominator tree
        if (idomCandidate) {
            domTree_[idomCandidate].insert(bb);
        }
    }

    return idom_;
}

bool IRVerifier::dominates(const BasicBlock* dominator, const BasicBlock* dominated) const {
    if (dominator == dominated) return true;

    const BasicBlock* current = dominated;
    while (current) {
        auto it = idom_.find(current);
        if (it == idom_.end()) break;
        current = it->second;
        if (current == dominator) return true;
    }

    return false;
}

bool IRVerifier::strictlyDominates(const BasicBlock* dominator, const BasicBlock* dominated) const {
    return dominator != dominated && dominates(dominator, dominated);
}

bool IRVerifier::dominates(const Instruction* def, const Instruction* use) const {
    if (!def || !use) return false;

    const BasicBlock* defBlock = def->getParent();
    const BasicBlock* useBlock = use->getParent();

    if (!defBlock || !useBlock) return false;

    if (defBlock == useBlock) {
        // Same block: def must come before use
        for (auto it = defBlock->begin(); it != defBlock->end(); ++it) {
            if (*it == def) return true;
            if (*it == use) return false;
        }
        return false;
    }

    // Different blocks: defBlock must dominate useBlock
    return dominates(defBlock, useBlock);
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
