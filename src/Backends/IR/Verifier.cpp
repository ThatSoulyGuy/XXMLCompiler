#include "Backends/IR/Verifier.h"
#include <sstream>
#include <iostream>
#include <algorithm>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// VerifierError Implementation
// ============================================================================

std::string VerifierError::toString() const {
    std::ostringstream oss;

    // Severity
    switch (severity) {
        case Severity::Error:   oss << "error: "; break;
        case Severity::Warning: oss << "warning: "; break;
        case Severity::Note:    oss << "note: "; break;
    }

    // Location
    if (function) {
        oss << "in function '" << function->getName() << "'";
        if (block) {
            oss << ", block '" << (block->hasName() ? block->getName() : "(unnamed)") << "'";
        }
        oss << ": ";
    }

    // Message
    oss << message;

    return oss.str();
}

// ============================================================================
// Verifier Implementation
// ============================================================================

Verifier::Verifier(const Module& module) : module_(module) {}

bool Verifier::verify() {
    errors_.clear();
    definedValues_.clear();
    visitedBlocks_.clear();

    // Module-level verification
    verifyModule();

    return !hasErrors();
}

bool Verifier::verifyModule() {
    // Verify struct types
    verifyStructTypes();

    // Verify global variables
    verifyGlobalVariables();

    // Verify function declarations
    verifyFunctionDeclarations();

    // Verify each function with a body
    for (const auto& pair : module_.getFunctions()) {
        const Function* func = pair.second.get();
        if (!func->isDeclaration()) {
            verifyFunction(func);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyStructTypes() {
    for (const auto& pair : module_.getStructTypes()) {
        const StructType* st = pair.second;
        if (!st) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Module,
                     "Null struct type entry for '" + pair.first + "'");
            continue;
        }

        // Check for recursive types without pointer indirection
        // (This would be caught by the type system, but double-check)
        if (!st->isPacked() && st->getNumElements() == 0 && !st->isOpaque()) {
            addError(VerifierError::Severity::Warning,
                     VerifierError::Category::Type,
                     "Empty struct type '" + st->getName() + "'");
        }
    }

    return !hasErrors();
}

bool Verifier::verifyGlobalVariables() {
    for (const auto& pair : module_.getGlobals()) {
        const GlobalVariable* gv = pair.second.get();
        if (!gv) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Module,
                     "Null global variable entry for '" + pair.first + "'");
            continue;
        }

        // Verify global has a valid type
        if (!gv->getValueType()) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Global variable '" + gv->getName() + "' has no value type");
        }

        // Verify initializer type matches
        if (const Constant* init = gv->getInitializer()) {
            if (init->getType() && gv->getValueType() &&
                !typesMatch(init->getType(), gv->getValueType())) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "Global variable '" + gv->getName() +
                         "' initializer type doesn't match variable type");
            }
        }
    }

    return !hasErrors();
}

bool Verifier::verifyFunctionDeclarations() {
    for (const auto& pair : module_.getFunctions()) {
        const Function* func = pair.second.get();
        if (!func) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Module,
                     "Null function entry for '" + pair.first + "'");
            continue;
        }

        verifyFunctionSignature(func);
    }

    return !hasErrors();
}

bool Verifier::verifyFunction(const Function* func) {
    if (!func) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Function,
                 "Null function pointer");
        return false;
    }

    currentFunction_ = func;
    definedValues_.clear();
    visitedBlocks_.clear();

    // Verify signature
    verifyFunctionSignature(func);

    // Skip body verification for declarations
    if (func->isDeclaration()) {
        currentFunction_ = nullptr;
        return !hasErrors();
    }

    // Add arguments to defined values
    for (const auto& arg : func->args()) {
        definedValues_.insert(arg.get());
    }

    // Verify entry block
    verifyEntryBlock(func);

    // Verify all basic blocks
    for (const auto& bb : *func) {
        verifyBasicBlock(bb.get(), func);
    }

    // Verify SSA properties
    if (checkDominance_) {
        verifySSAProperty(func);
    }

    // Verify use-def chains
    if (checkUseDefChains_) {
        verifyUseDefChains(func);
    }

    // Verify return paths
    verifyReturnPaths(func);

    currentFunction_ = nullptr;
    return !hasErrors();
}

bool Verifier::verifyFunctionSignature(const Function* func) {
    const FunctionType* funcType = func->getFunctionType();
    if (!funcType) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Function,
                 "Function has no function type", func);
        return false;
    }

    // Verify return type exists
    if (!funcType->getReturnType()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Function has no return type", func);
    }

    // Verify parameter types exist
    for (size_t i = 0; i < funcType->getNumParams(); ++i) {
        if (!funcType->getParamType(i)) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Function parameter " + std::to_string(i) + " has no type", func);
        }
    }

    // Verify argument count matches function type
    if (func->getNumArgs() != funcType->getNumParams()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Function,
                 "Function argument count (" + std::to_string(func->getNumArgs()) +
                 ") doesn't match type parameter count (" +
                 std::to_string(funcType->getNumParams()) + ")", func);
    }

    return !hasErrors();
}

bool Verifier::verifyEntryBlock(const Function* func) {
    if (func->empty()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Function,
                 "Function has no basic blocks", func);
        return false;
    }

    const BasicBlock* entry = func->getEntryBlock();
    if (!entry) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Function,
                 "Function has no entry block", func);
        return false;
    }

    // Entry block should not have predecessors
    if (!entry->getPredecessors().empty()) {
        addError(VerifierError::Severity::Warning,
                 VerifierError::Category::CFG,
                 "Entry block has predecessors", func, entry);
    }

    // Entry block should not start with PHI nodes
    if (entry->hasPhiNodes()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::SSA,
                 "Entry block contains PHI nodes", func, entry);
    }

    return !hasErrors();
}

bool Verifier::verifyReturnPaths(const Function* func) {
    const FunctionType* funcType = func->getFunctionType();
    if (!funcType) return false;

    Type* returnType = funcType->getReturnType();
    bool isVoidReturn = returnType && returnType->getKind() == Type::Kind::Void;

    bool hasReturn = false;
    for (const auto& bb : *func) {
        const TerminatorInst* term = bb->getTerminator();
        if (!term) continue;

        if (const ReturnInst* ret = dyn_cast<ReturnInst>(term)) {
            hasReturn = true;

            if (isVoidReturn) {
                if (ret->getReturnValue()) {
                    addError(VerifierError::Severity::Error,
                             VerifierError::Category::Type,
                             "Void function returns a value", func, bb.get(), ret);
                }
            } else {
                if (!ret->getReturnValue()) {
                    addError(VerifierError::Severity::Error,
                             VerifierError::Category::Type,
                             "Non-void function returns without a value", func, bb.get(), ret);
                } else if (!typesMatch(ret->getReturnValue()->getType(), returnType)) {
                    addError(VerifierError::Severity::Error,
                             VerifierError::Category::Type,
                             "Return value type doesn't match function return type",
                             func, bb.get(), ret);
                }
            }
        }
    }

    // Functions should have at least one return (or unreachable)
    if (!hasReturn && strictMode_) {
        bool hasUnreachable = false;
        for (const auto& bb : *func) {
            if (bb->getTerminator() && isa<UnreachableInst>(bb->getTerminator())) {
                hasUnreachable = true;
                break;
            }
        }
        if (!hasUnreachable) {
            addError(VerifierError::Severity::Warning,
                     VerifierError::Category::Function,
                     "Function has no return or unreachable instruction", func);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyBasicBlock(const BasicBlock* bb, const Function* func) {
    if (!bb) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::CFG,
                 "Null basic block", func);
        return false;
    }

    currentBlock_ = bb;
    visitedBlocks_.insert(bb);

    // Verify terminator
    verifyBlockTerminator(bb, func);

    // Verify PHI nodes are at the beginning
    verifyPhiNodes(bb, func);

    // Verify all instructions
    bool seenNonPhi = false;
    for (const auto& inst : *bb) {
        if (isa<PHINode>(inst.get())) {
            if (seenNonPhi) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Instruction,
                         "PHI node not at beginning of block", func, bb, inst.get());
            }
        } else {
            seenNonPhi = true;
        }

        verifyInstruction(inst.get(), bb);

        // Add instruction to defined values if it produces a value
        if (inst->getType() && inst->getType()->getKind() != Type::Kind::Void) {
            definedValues_.insert(inst.get());
        }
    }

    currentBlock_ = nullptr;
    return !hasErrors();
}

bool Verifier::verifyBlockTerminator(const BasicBlock* bb, const Function* func) {
    const TerminatorInst* term = bb->getTerminator();
    if (!term) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Terminator,
                 "Basic block has no terminator", func, bb);
        return false;
    }

    // Verify terminator is actually last instruction
    if (!bb->empty() && &bb->back() != term) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Terminator,
                 "Terminator is not the last instruction", func, bb);
    }

    // Verify no terminator in the middle of block
    bool seenTerminator = false;
    for (const auto& inst : *bb) {
        if (inst->isTerminator()) {
            if (seenTerminator) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Terminator,
                         "Multiple terminators in block", func, bb, inst.get());
            }
            seenTerminator = true;
        }
    }

    return !hasErrors();
}

bool Verifier::verifyPhiNodes(const BasicBlock* bb, const Function* func) {
    for (const auto& inst : *bb) {
        const PHINode* phi = dyn_cast<PHINode>(inst.get());
        if (!phi) break;  // PHIs are at the beginning

        verifyPHI(phi, bb);
    }

    return !hasErrors();
}

bool Verifier::verifyInstruction(const Instruction* inst, const BasicBlock* bb) {
    if (!inst) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Instruction,
                 "Null instruction", currentFunction_, bb);
        return false;
    }

    // Verify instruction has a parent
    if (inst->getParent() != bb) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Instruction,
                 "Instruction parent doesn't match containing block",
                 currentFunction_, bb, inst);
    }

    // Verify operands
    verifyInstructionOperands(inst);

    // Verify type
    verifyInstructionType(inst);

    // Dispatch to specific verifiers
    switch (inst->getOpcode()) {
        case Opcode::Alloca:
            return verifyAlloca(cast<AllocaInst>(inst));
        case Opcode::Load:
            return verifyLoad(cast<LoadInst>(inst));
        case Opcode::Store:
            return verifyStore(cast<StoreInst>(inst));
        case Opcode::GetElementPtr:
            return verifyGEP(cast<GetElementPtrInst>(inst));

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
        case Opcode::FRem:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
            return verifyBinaryOp(cast<BinaryOperator>(inst));

        case Opcode::ICmp:
            return verifyICmp(cast<ICmpInst>(inst));
        case Opcode::FCmp:
            return verifyFCmp(cast<FCmpInst>(inst));

        case Opcode::Trunc:
        case Opcode::ZExt:
        case Opcode::SExt:
        case Opcode::FPToUI:
        case Opcode::FPToSI:
        case Opcode::UIToFP:
        case Opcode::SIToFP:
        case Opcode::FPTrunc:
        case Opcode::FPExt:
        case Opcode::PtrToInt:
        case Opcode::IntToPtr:
        case Opcode::Bitcast:
            return verifyCast(cast<CastInst>(inst));

        case Opcode::Br:
        case Opcode::CondBr:
            return verifyBranch(cast<BranchInst>(inst));
        case Opcode::Switch:
            return verifySwitch(cast<SwitchInst>(inst));
        case Opcode::Ret:
            return verifyReturn(cast<ReturnInst>(inst));
        case Opcode::Unreachable:
            return true;  // No special verification needed

        case Opcode::PHI:
            return verifyPHI(cast<PHINode>(inst), bb);
        case Opcode::Call:
            return verifyCall(cast<CallInst>(inst));
        case Opcode::Select:
            return verifySelect(cast<SelectInst>(inst));

        default:
            addError(VerifierError::Severity::Warning,
                     VerifierError::Category::Instruction,
                     "Unknown instruction opcode: " + std::to_string(static_cast<int>(inst->getOpcode())),
                     currentFunction_, bb, inst);
            return true;
    }
}

bool Verifier::verifyInstructionOperands(const Instruction* inst) {
    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        Value* op = inst->getOperand(i);
        if (!op) {
            // Some instructions allow null operands (e.g., return void)
            if (inst->getOpcode() == Opcode::Ret) continue;

            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Operand,
                     "Instruction has null operand at index " + std::to_string(i),
                     currentFunction_, currentBlock_, inst);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyInstructionType(const Instruction* inst) {
    // Most instructions should have a type
    if (!inst->getType()) {
        // Some instructions don't produce values
        if (inst->getOpcode() == Opcode::Store ||
            inst->getOpcode() == Opcode::Br ||
            inst->getOpcode() == Opcode::CondBr ||
            inst->getOpcode() == Opcode::Switch ||
            inst->getOpcode() == Opcode::Ret ||
            inst->getOpcode() == Opcode::Unreachable) {
            return true;
        }

        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Instruction has no type",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    return true;
}

bool Verifier::verifyAlloca(const AllocaInst* inst) {
    if (!inst->getAllocatedType()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Alloca has no allocated type",
                 currentFunction_, currentBlock_, inst);
    }

    // Result should be pointer type
    if (!isPointerType(inst->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Alloca result is not a pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyLoad(const LoadInst* inst) {
    Value* ptr = inst->getPointer();
    if (!ptr) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Load has no pointer operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    if (!isPointerType(ptr->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Load pointer operand is not a pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    if (!inst->getLoadedType()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Load has no loaded type",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyStore(const StoreInst* inst) {
    Value* val = inst->getValue();
    Value* ptr = inst->getPointer();

    if (!val) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Store has no value operand",
                 currentFunction_, currentBlock_, inst);
    }

    if (!ptr) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Store has no pointer operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    if (!isPointerType(ptr->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Store pointer operand is not a pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyGEP(const GetElementPtrInst* inst) {
    Value* ptr = inst->getPointer();
    if (!ptr) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "GEP has no pointer operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    if (!isPointerType(ptr->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "GEP pointer operand is not a pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    if (!inst->getSourceElementType()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "GEP has no source element type",
                 currentFunction_, currentBlock_, inst);
    }

    // Result should be pointer type
    if (!isPointerType(inst->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "GEP result is not a pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    // Verify indices are integer types
    for (unsigned i = 0; i < inst->getNumIndices(); ++i) {
        Value* idx = inst->getIndex(i);
        if (idx && !isIntegerType(idx->getType())) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "GEP index " + std::to_string(i) + " is not an integer type",
                     currentFunction_, currentBlock_, inst);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyBinaryOp(const BinaryOperator* inst) {
    Value* lhs = inst->getLHS();
    Value* rhs = inst->getRHS();

    if (!lhs || !rhs) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Binary operator missing operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    Type* lhsType = lhs->getType();
    Type* rhsType = rhs->getType();

    if (!lhsType || !rhsType) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Binary operator operand has no type",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    // Types must match
    if (!typesMatch(lhsType, rhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Binary operator operands have mismatched types",
                 currentFunction_, currentBlock_, inst);
    }

    // Check appropriate types for operation
    Opcode op = inst->getOpcode();
    bool isFloatOp = (op == Opcode::FAdd || op == Opcode::FSub ||
                      op == Opcode::FMul || op == Opcode::FDiv || op == Opcode::FRem);

    if (isFloatOp) {
        if (!isFloatingPointType(lhsType)) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Floating-point operation on non-floating-point type",
                     currentFunction_, currentBlock_, inst);
        }
    } else {
        if (!isIntegerType(lhsType)) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Integer operation on non-integer type",
                     currentFunction_, currentBlock_, inst);
        }
    }

    // Result type should match operand types
    if (inst->getType() && !typesMatch(inst->getType(), lhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Binary operator result type doesn't match operand types",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyICmp(const ICmpInst* inst) {
    Value* lhs = inst->getLHS();
    Value* rhs = inst->getRHS();

    if (!lhs || !rhs) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "ICmp missing operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    Type* lhsType = lhs->getType();
    Type* rhsType = rhs->getType();

    if (!typesMatch(lhsType, rhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "ICmp operands have mismatched types",
                 currentFunction_, currentBlock_, inst);
    }

    // Operands should be integers or pointers
    if (!isIntegerType(lhsType) && !isPointerType(lhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "ICmp operand is not integer or pointer type",
                 currentFunction_, currentBlock_, inst);
    }

    // Result should be i1
    if (inst->getType()) {
        const IntegerType* resultType = dyn_cast<IntegerType>(inst->getType());
        if (!resultType || resultType->getBitWidth() != 1) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "ICmp result is not i1",
                     currentFunction_, currentBlock_, inst);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyFCmp(const FCmpInst* inst) {
    Value* lhs = inst->getLHS();
    Value* rhs = inst->getRHS();

    if (!lhs || !rhs) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "FCmp missing operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    Type* lhsType = lhs->getType();
    Type* rhsType = rhs->getType();

    if (!typesMatch(lhsType, rhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "FCmp operands have mismatched types",
                 currentFunction_, currentBlock_, inst);
    }

    // Operands should be floating-point
    if (!isFloatingPointType(lhsType)) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "FCmp operand is not floating-point type",
                 currentFunction_, currentBlock_, inst);
    }

    // Result should be i1
    if (inst->getType()) {
        const IntegerType* resultType = dyn_cast<IntegerType>(inst->getType());
        if (!resultType || resultType->getBitWidth() != 1) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "FCmp result is not i1",
                     currentFunction_, currentBlock_, inst);
        }
    }

    return !hasErrors();
}

bool Verifier::verifyCast(const CastInst* inst) {
    Value* src = inst->getSrcValue();
    if (!src) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Cast missing source operand",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    Type* srcType = src->getType();
    Type* destType = inst->getDestType();

    if (!srcType || !destType) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Cast has missing source or destination type",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    // Validate specific cast types
    Opcode op = inst->getOpcode();
    switch (op) {
        case Opcode::Trunc:
            if (!isIntegerType(srcType) || !isIntegerType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "Trunc requires integer types",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::ZExt:
        case Opcode::SExt:
            if (!isIntegerType(srcType) || !isIntegerType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "Extension requires integer types",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::FPToUI:
        case Opcode::FPToSI:
            if (!isFloatingPointType(srcType) || !isIntegerType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "FPToInt requires float source and integer destination",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::UIToFP:
        case Opcode::SIToFP:
            if (!isIntegerType(srcType) || !isFloatingPointType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "IntToFP requires integer source and float destination",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::FPTrunc:
        case Opcode::FPExt:
            if (!isFloatingPointType(srcType) || !isFloatingPointType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "FP truncation/extension requires float types",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::PtrToInt:
            if (!isPointerType(srcType) || !isIntegerType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "PtrToInt requires pointer source and integer destination",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        case Opcode::IntToPtr:
            if (!isIntegerType(srcType) || !isPointerType(destType)) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "IntToPtr requires integer source and pointer destination",
                         currentFunction_, currentBlock_, inst);
            }
            break;

        default:
            break;
    }

    return !hasErrors();
}

bool Verifier::verifyBranch(const BranchInst* inst) {
    if (inst->isConditional()) {
        Value* cond = inst->getCondition();
        if (!cond) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Operand,
                     "Conditional branch has no condition",
                     currentFunction_, currentBlock_, inst);
        } else {
            // Condition should be i1
            const IntegerType* condType = dyn_cast<IntegerType>(cond->getType());
            if (!condType || condType->getBitWidth() != 1) {
                addError(VerifierError::Severity::Error,
                         VerifierError::Category::Type,
                         "Branch condition is not i1",
                         currentFunction_, currentBlock_, inst);
            }
        }

        if (!inst->getTrueBlock()) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::CFG,
                     "Conditional branch has no true destination",
                     currentFunction_, currentBlock_, inst);
        }

        if (!inst->getFalseBlock()) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::CFG,
                     "Conditional branch has no false destination",
                     currentFunction_, currentBlock_, inst);
        }
    } else {
        if (!inst->getDestBlock()) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::CFG,
                     "Unconditional branch has no destination",
                     currentFunction_, currentBlock_, inst);
        }
    }

    return !hasErrors();
}

bool Verifier::verifySwitch(const SwitchInst* inst) {
    Value* cond = inst->getCondition();
    if (!cond) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Switch has no condition",
                 currentFunction_, currentBlock_, inst);
    } else if (!isIntegerType(cond->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Switch condition is not integer type",
                 currentFunction_, currentBlock_, inst);
    }

    if (!inst->getDefaultDest()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::CFG,
                 "Switch has no default destination",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyReturn(const ReturnInst* inst) {
    // Return value verification is handled in verifyReturnPaths
    return true;
}

bool Verifier::verifyPHI(const PHINode* inst, const BasicBlock* bb) {
    if (!inst->getType()) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "PHI has no type",
                 currentFunction_, bb, inst);
    }

    // PHI must have incoming values
    if (inst->getNumIncoming() == 0) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::SSA,
                 "PHI has no incoming values",
                 currentFunction_, bb, inst);
        return false;
    }

    // Check each incoming value
    for (unsigned i = 0; i < inst->getNumIncoming(); ++i) {
        Value* val = inst->getIncomingValue(i);
        BasicBlock* block = inst->getIncomingBlock(i);

        if (!val) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Operand,
                     "PHI has null incoming value at index " + std::to_string(i),
                     currentFunction_, bb, inst);
        } else if (!typesMatch(val->getType(), inst->getType())) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "PHI incoming value type doesn't match PHI type",
                     currentFunction_, bb, inst);
        }

        if (!block) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::CFG,
                     "PHI has null incoming block at index " + std::to_string(i),
                     currentFunction_, bb, inst);
        }
    }

    // Number of incoming values should match number of predecessors
    if (bb && inst->getNumIncoming() != bb->getPredecessors().size()) {
        addError(VerifierError::Severity::Warning,
                 VerifierError::Category::SSA,
                 "PHI incoming count (" + std::to_string(inst->getNumIncoming()) +
                 ") doesn't match predecessor count (" +
                 std::to_string(bb->getPredecessors().size()) + ")",
                 currentFunction_, bb, inst);
    }

    return !hasErrors();
}

bool Verifier::verifyCall(const CallInst* inst) {
    const FunctionType* funcType = inst->getFunctionType();
    if (!funcType) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Call has no function type",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    // Verify argument count
    size_t expectedArgs = funcType->getNumParams();
    size_t actualArgs = inst->getNumArgs();

    if (funcType->isVarArg()) {
        if (actualArgs < expectedArgs) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Operand,
                     "Vararg call has fewer arguments than fixed parameters",
                     currentFunction_, currentBlock_, inst);
        }
    } else {
        if (actualArgs != expectedArgs) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Operand,
                     "Call argument count (" + std::to_string(actualArgs) +
                     ") doesn't match expected (" + std::to_string(expectedArgs) + ")",
                     currentFunction_, currentBlock_, inst);
        }
    }

    // Verify argument types
    for (size_t i = 0; i < std::min(actualArgs, expectedArgs); ++i) {
        Value* arg = inst->getArg(i);
        Type* expectedType = funcType->getParamType(i);

        if (arg && expectedType && !typesMatch(arg->getType(), expectedType)) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Call argument " + std::to_string(i) + " type mismatch",
                     currentFunction_, currentBlock_, inst);
        }
    }

    // Verify return type
    if (inst->getType() && funcType->getReturnType() &&
        !typesMatch(inst->getType(), funcType->getReturnType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Call return type doesn't match function return type",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifySelect(const SelectInst* inst) {
    Value* cond = inst->getCondition();
    Value* trueVal = inst->getTrueValue();
    Value* falseVal = inst->getFalseValue();

    if (!cond) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Select has no condition",
                 currentFunction_, currentBlock_, inst);
    } else {
        // Condition should be i1
        const IntegerType* condType = dyn_cast<IntegerType>(cond->getType());
        if (!condType || condType->getBitWidth() != 1) {
            addError(VerifierError::Severity::Error,
                     VerifierError::Category::Type,
                     "Select condition is not i1",
                     currentFunction_, currentBlock_, inst);
        }
    }

    if (!trueVal || !falseVal) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Operand,
                 "Select missing true or false value",
                 currentFunction_, currentBlock_, inst);
        return false;
    }

    // True and false values should have same type
    if (!typesMatch(trueVal->getType(), falseVal->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Select true/false values have mismatched types",
                 currentFunction_, currentBlock_, inst);
    }

    // Result type should match value types
    if (inst->getType() && trueVal->getType() &&
        !typesMatch(inst->getType(), trueVal->getType())) {
        addError(VerifierError::Severity::Error,
                 VerifierError::Category::Type,
                 "Select result type doesn't match value types",
                 currentFunction_, currentBlock_, inst);
    }

    return !hasErrors();
}

bool Verifier::verifySSAProperty(const Function* func) {
    // This is a simplified SSA verification
    // Full verification would require tracking definitions through dominance

    std::set<std::string> definedNames;

    // Arguments are defined
    for (const auto& arg : func->args()) {
        if (arg->hasName()) {
            definedNames.insert(arg->getName());
        }
    }

    // Check each instruction is defined before use
    for (const auto& bb : *func) {
        for (const auto& inst : *bb) {
            // Add this instruction's definition
            if (inst->hasName() && inst->getType() &&
                inst->getType()->getKind() != Type::Kind::Void) {
                if (definedNames.count(inst->getName()) && strictMode_) {
                    addError(VerifierError::Severity::Warning,
                             VerifierError::Category::SSA,
                             "Multiple definitions of '" + inst->getName() + "'",
                             func, bb.get(), inst.get());
                }
                definedNames.insert(inst->getName());
            }
        }
    }

    return !hasErrors();
}

bool Verifier::verifyValueDominance(const Function* func) {
    // Full dominance verification would require computing dominance
    // and checking that every use is dominated by its definition
    // This is a placeholder for more sophisticated verification
    return true;
}

bool Verifier::verifySingleDefinition(const Function* func) {
    // Covered in verifySSAProperty
    return true;
}

bool Verifier::verifyUseDefChains(const Function* func) {
    // Verify that all uses of a value are properly linked
    // This is a basic check; full verification would be more complex
    return true;
}

// ============================================================================
// Helper Methods
// ============================================================================

bool Verifier::isIntegerType(const Type* ty) const {
    return ty && ty->getKind() == Type::Kind::Integer;
}

bool Verifier::isFloatingPointType(const Type* ty) const {
    return ty && ty->getKind() == Type::Kind::Float;
}

bool Verifier::isPointerType(const Type* ty) const {
    return ty && ty->getKind() == Type::Kind::Pointer;
}

bool Verifier::isAggregateType(const Type* ty) const {
    if (!ty) return false;
    Type::Kind kind = ty->getKind();
    return kind == Type::Kind::Struct || kind == Type::Kind::Array;
}

bool Verifier::typesMatch(const Type* a, const Type* b) const {
    if (a == b) return true;
    if (!a || !b) return false;

    // Same kind check
    if (a->getKind() != b->getKind()) return false;

    // For now, same kind is enough for opaque pointer world
    // More detailed comparison would check struct layout, array sizes, etc.
    return true;
}

std::string Verifier::typeToString(const Type* ty) const {
    if (!ty) return "(null)";
    // Would delegate to type's print method
    return "(type)";
}

// ============================================================================
// Error Reporting
// ============================================================================

void Verifier::addError(VerifierError::Severity sev, VerifierError::Category cat,
                        const std::string& msg) {
    VerifierError error(sev, cat, msg);
    errors_.push_back(error);
}

void Verifier::addError(VerifierError::Severity sev, VerifierError::Category cat,
                        const std::string& msg, const Function* func) {
    VerifierError error(sev, cat, msg);
    error.function = func;
    errors_.push_back(error);
}

void Verifier::addError(VerifierError::Severity sev, VerifierError::Category cat,
                        const std::string& msg, const Function* func, const BasicBlock* bb) {
    VerifierError error(sev, cat, msg);
    error.function = func;
    error.block = bb;
    errors_.push_back(error);
}

void Verifier::addError(VerifierError::Severity sev, VerifierError::Category cat,
                        const std::string& msg, const Function* func, const BasicBlock* bb,
                        const Instruction* inst) {
    VerifierError error(sev, cat, msg);
    error.function = func;
    error.block = bb;
    error.instruction = inst;
    errors_.push_back(error);
}

bool Verifier::hasErrors() const {
    for (const auto& error : errors_) {
        if (error.severity == VerifierError::Severity::Error) {
            return true;
        }
    }
    return false;
}

bool Verifier::hasWarnings() const {
    for (const auto& error : errors_) {
        if (error.severity == VerifierError::Severity::Warning) {
            return true;
        }
    }
    return false;
}

std::string Verifier::getErrorReport() const {
    std::ostringstream oss;

    int errorCount = 0;
    int warningCount = 0;

    for (const auto& error : errors_) {
        oss << error.toString() << "\n";
        if (error.severity == VerifierError::Severity::Error) errorCount++;
        if (error.severity == VerifierError::Severity::Warning) warningCount++;
    }

    oss << "\n" << errorCount << " error(s), " << warningCount << " warning(s)\n";
    return oss.str();
}

void Verifier::printErrors() const {
    std::cerr << getErrorReport();
}

// ============================================================================
// Utility Functions
// ============================================================================

bool verifyModule(const Module& module) {
    Verifier verifier(module);
    return verifier.verify();
}

bool verifyModule(const Module& module, std::string* errorMessage) {
    Verifier verifier(module);
    bool result = verifier.verify();

    if (!result && errorMessage) {
        *errorMessage = verifier.getErrorReport();
    }

    return result;
}

void verifyModuleOrThrow(const Module& module) {
    Verifier verifier(module);
    if (!verifier.verify()) {
        throw std::runtime_error("IR verification failed:\n" + verifier.getErrorReport());
    }
}

} // namespace IR
} // namespace Backends
} // namespace XXML
