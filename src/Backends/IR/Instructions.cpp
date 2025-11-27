#include "Backends/IR/Instructions.h"
#include "Backends/IR/BasicBlock.h"
#include "Backends/IR/Function.h"

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Opcode Name Table
// ============================================================================

const char* getOpcodeName(Opcode op) {
    switch (op) {
        case Opcode::Alloca:       return "alloca";
        case Opcode::Load:         return "load";
        case Opcode::Store:        return "store";
        case Opcode::GetElementPtr: return "getelementptr";
        case Opcode::Add:          return "add";
        case Opcode::Sub:          return "sub";
        case Opcode::Mul:          return "mul";
        case Opcode::SDiv:         return "sdiv";
        case Opcode::UDiv:         return "udiv";
        case Opcode::SRem:         return "srem";
        case Opcode::URem:         return "urem";
        case Opcode::FAdd:         return "fadd";
        case Opcode::FSub:         return "fsub";
        case Opcode::FMul:         return "fmul";
        case Opcode::FDiv:         return "fdiv";
        case Opcode::FRem:         return "frem";
        case Opcode::FNeg:         return "fneg";
        case Opcode::Shl:          return "shl";
        case Opcode::LShr:         return "lshr";
        case Opcode::AShr:         return "ashr";
        case Opcode::And:          return "and";
        case Opcode::Or:           return "or";
        case Opcode::Xor:          return "xor";
        case Opcode::ICmp:         return "icmp";
        case Opcode::FCmp:         return "fcmp";
        case Opcode::Trunc:        return "trunc";
        case Opcode::ZExt:         return "zext";
        case Opcode::SExt:         return "sext";
        case Opcode::FPToUI:       return "fptoui";
        case Opcode::FPToSI:       return "fptosi";
        case Opcode::UIToFP:       return "uitofp";
        case Opcode::SIToFP:       return "sitofp";
        case Opcode::FPTrunc:      return "fptrunc";
        case Opcode::FPExt:        return "fpext";
        case Opcode::PtrToInt:     return "ptrtoint";
        case Opcode::IntToPtr:     return "inttoptr";
        case Opcode::Bitcast:      return "bitcast";
        case Opcode::Br:           return "br";
        case Opcode::CondBr:       return "br";
        case Opcode::Switch:       return "switch";
        case Opcode::Ret:          return "ret";
        case Opcode::Unreachable:  return "unreachable";
        case Opcode::Call:         return "call";
        case Opcode::PHI:          return "phi";
        case Opcode::Select:       return "select";
    }
    return "unknown";
}

// ============================================================================
// Instruction Implementation
// ============================================================================

bool Instruction::isTerminator() const {
    return opcode_ == Opcode::Br || opcode_ == Opcode::CondBr ||
           opcode_ == Opcode::Switch || opcode_ == Opcode::Ret ||
           opcode_ == Opcode::Unreachable;
}

bool Instruction::isBinaryOp() const {
    return (opcode_ >= Opcode::Add && opcode_ <= Opcode::URem) ||
           (opcode_ >= Opcode::FAdd && opcode_ <= Opcode::FRem) ||
           (opcode_ >= Opcode::Shl && opcode_ <= Opcode::Xor);
}

bool Instruction::isUnaryOp() const {
    return opcode_ == Opcode::FNeg;
}

bool Instruction::isCast() const {
    return opcode_ >= Opcode::Trunc && opcode_ <= Opcode::Bitcast;
}

bool Instruction::isComparison() const {
    return opcode_ == Opcode::ICmp || opcode_ == Opcode::FCmp;
}

bool Instruction::mayHaveSideEffects() const {
    switch (opcode_) {
        case Opcode::Store:
        case Opcode::Call:
            return true;
        default:
            return false;
    }
}

bool Instruction::mayReadFromMemory() const {
    switch (opcode_) {
        case Opcode::Load:
        case Opcode::Call:
            return true;
        default:
            return false;
    }
}

bool Instruction::mayWriteToMemory() const {
    switch (opcode_) {
        case Opcode::Store:
        case Opcode::Call:
            return true;
        default:
            return false;
    }
}

void Instruction::insertBefore(Instruction* other) {
    assert(other && "Cannot insert before null");
    assert(other->parent_ && "Target instruction must be in a block");
    parent_ = other->parent_;
    // Actual insertion handled by BasicBlock
}

void Instruction::insertAfter(Instruction* other) {
    assert(other && "Cannot insert after null");
    assert(other->parent_ && "Target instruction must be in a block");
    parent_ = other->parent_;
    // Actual insertion handled by BasicBlock
}

void Instruction::removeFromParent() {
    if (parent_) {
        // Remove from linked list
        if (prev_) prev_->next_ = next_;
        if (next_) next_->prev_ = prev_;
        parent_ = nullptr;
        next_ = nullptr;
        prev_ = nullptr;
    }
}

void Instruction::eraseFromParent() {
    removeFromParent();
    // Note: Caller is responsible for deleting
}

// ============================================================================
// GetElementPtrInst Implementation
// ============================================================================

bool GetElementPtrInst::isStructFieldAccess() const {
    if (getNumIndices() != 2) return false;
    // First index should be constant 0
    if (auto* idx0 = dyn_cast<ConstantInt>(getIndex(0))) {
        if (idx0->getZExtValue() != 0) return false;
    } else {
        return false;
    }
    // Second index should be constant
    return isa<ConstantInt>(getIndex(1));
}

// ============================================================================
// BinaryOperator Implementation
// ============================================================================

BinaryOperator* BinaryOperator::Create(Opcode op, Value* lhs, Value* rhs, const std::string& name) {
    return new BinaryOperator(op, lhs, rhs, name);
}

bool BinaryOperator::isIntegerOp() const {
    return opcode_ >= Opcode::Add && opcode_ <= Opcode::URem;
}

bool BinaryOperator::isFloatingPointOp() const {
    return opcode_ >= Opcode::FAdd && opcode_ <= Opcode::FNeg;
}

bool BinaryOperator::isBitwiseOp() const {
    return opcode_ >= Opcode::Shl && opcode_ <= Opcode::Xor;
}

// ============================================================================
// ICmpInst Implementation
// ============================================================================

bool ICmpInst::isSigned() const {
    switch (predicate_) {
        case Predicate::SGT:
        case Predicate::SGE:
        case Predicate::SLT:
        case Predicate::SLE:
            return true;
        default:
            return false;
    }
}

bool ICmpInst::isUnsigned() const {
    switch (predicate_) {
        case Predicate::UGT:
        case Predicate::UGE:
        case Predicate::ULT:
        case Predicate::ULE:
            return true;
        default:
            return false;
    }
}

const char* ICmpInst::getPredicateName(Predicate p) {
    switch (p) {
        case Predicate::EQ:  return "eq";
        case Predicate::NE:  return "ne";
        case Predicate::UGT: return "ugt";
        case Predicate::UGE: return "uge";
        case Predicate::ULT: return "ult";
        case Predicate::ULE: return "ule";
        case Predicate::SGT: return "sgt";
        case Predicate::SGE: return "sge";
        case Predicate::SLT: return "slt";
        case Predicate::SLE: return "sle";
    }
    return "unknown";
}

ICmpInst::Predicate ICmpInst::getInversePredicate(Predicate p) {
    switch (p) {
        case Predicate::EQ:  return Predicate::NE;
        case Predicate::NE:  return Predicate::EQ;
        case Predicate::UGT: return Predicate::ULE;
        case Predicate::UGE: return Predicate::ULT;
        case Predicate::ULT: return Predicate::UGE;
        case Predicate::ULE: return Predicate::UGT;
        case Predicate::SGT: return Predicate::SLE;
        case Predicate::SGE: return Predicate::SLT;
        case Predicate::SLT: return Predicate::SGE;
        case Predicate::SLE: return Predicate::SGT;
    }
    return p;
}

ICmpInst::Predicate ICmpInst::getSwappedPredicate(Predicate p) {
    switch (p) {
        case Predicate::EQ:  return Predicate::EQ;
        case Predicate::NE:  return Predicate::NE;
        case Predicate::UGT: return Predicate::ULT;
        case Predicate::UGE: return Predicate::ULE;
        case Predicate::ULT: return Predicate::UGT;
        case Predicate::ULE: return Predicate::UGE;
        case Predicate::SGT: return Predicate::SLT;
        case Predicate::SGE: return Predicate::SLE;
        case Predicate::SLT: return Predicate::SGT;
        case Predicate::SLE: return Predicate::SGE;
    }
    return p;
}

// ============================================================================
// FCmpInst Implementation
// ============================================================================

bool FCmpInst::isOrdered() const {
    switch (predicate_) {
        case Predicate::OEQ:
        case Predicate::OGT:
        case Predicate::OGE:
        case Predicate::OLT:
        case Predicate::OLE:
        case Predicate::ONE:
        case Predicate::ORD:
            return true;
        default:
            return false;
    }
}

bool FCmpInst::isUnordered() const {
    return !isOrdered();
}

const char* FCmpInst::getPredicateName(Predicate p) {
    switch (p) {
        case Predicate::OEQ: return "oeq";
        case Predicate::OGT: return "ogt";
        case Predicate::OGE: return "oge";
        case Predicate::OLT: return "olt";
        case Predicate::OLE: return "ole";
        case Predicate::ONE: return "one";
        case Predicate::ORD: return "ord";
        case Predicate::UEQ: return "ueq";
        case Predicate::UGT: return "ugt";
        case Predicate::UGE: return "uge";
        case Predicate::ULT: return "ult";
        case Predicate::ULE: return "ule";
        case Predicate::UNE: return "une";
        case Predicate::UNO: return "uno";
    }
    return "unknown";
}

// ============================================================================
// CastInst Implementation
// ============================================================================

CastInst* CastInst::Create(Opcode op, Value* value, Type* destTy, const std::string& name) {
    switch (op) {
        case Opcode::Trunc:    return new TruncInst(value, destTy, name);
        case Opcode::ZExt:     return new ZExtInst(value, destTy, name);
        case Opcode::SExt:     return new SExtInst(value, destTy, name);
        case Opcode::PtrToInt: return new PtrToIntInst(value, destTy, name);
        case Opcode::IntToPtr: return new IntToPtrInst(value, destTy, name);
        case Opcode::Bitcast:  return new BitCastInst(value, destTy, name);
        default:
            return new CastInst(op, value, destTy, name);
    }
}

bool CastInst::castIsValid(Opcode op, Type* srcTy, Type* destTy) {
    switch (op) {
        case Opcode::Trunc:
            return srcTy->isInteger() && destTy->isInteger() &&
                   srcTy->getSizeInBits() > destTy->getSizeInBits();

        case Opcode::ZExt:
        case Opcode::SExt:
            return srcTy->isInteger() && destTy->isInteger() &&
                   srcTy->getSizeInBits() < destTy->getSizeInBits();

        case Opcode::PtrToInt:
            return srcTy->isPointer() && destTy->isInteger();

        case Opcode::IntToPtr:
            return srcTy->isInteger() && destTy->isPointer();

        case Opcode::Bitcast:
            return srcTy->getSizeInBits() == destTy->getSizeInBits();

        case Opcode::FPToUI:
        case Opcode::FPToSI:
            return srcTy->isFloatingPoint() && destTy->isInteger();

        case Opcode::UIToFP:
        case Opcode::SIToFP:
            return srcTy->isInteger() && destTy->isFloatingPoint();

        case Opcode::FPTrunc:
            return srcTy->isFloatingPoint() && destTy->isFloatingPoint() &&
                   srcTy->getSizeInBits() > destTy->getSizeInBits();

        case Opcode::FPExt:
            return srcTy->isFloatingPoint() && destTy->isFloatingPoint() &&
                   srcTy->getSizeInBits() < destTy->getSizeInBits();

        default:
            return false;
    }
}

// ============================================================================
// TerminatorInst Implementation
// ============================================================================

BasicBlock* TerminatorInst::getSuccessor(unsigned i) const {
    // Subclasses override this
    return nullptr;
}

void TerminatorInst::setSuccessor(unsigned i, BasicBlock* bb) {
    // Subclasses override this
}

// ============================================================================
// BranchInst Implementation
// ============================================================================

BranchInst* BranchInst::Create(BasicBlock* dest) {
    return new BranchInst(dest);
}

BranchInst* BranchInst::Create(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse) {
    return new BranchInst(cond, ifTrue, ifFalse);
}

BranchInst::BranchInst(BasicBlock* dest)
    : TerminatorInst(Opcode::Br, nullptr, 0, 1), isConditional_(false) {
    successors_[0] = dest;
    successors_[1] = nullptr;
}

BranchInst::BranchInst(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse)
    : TerminatorInst(Opcode::CondBr, nullptr, 1, 2), isConditional_(true) {
    setOperand(0, cond);
    successors_[0] = ifTrue;
    successors_[1] = ifFalse;
}

BasicBlock* BranchInst::getTrueSuccessor() const {
    return successors_[0];
}

BasicBlock* BranchInst::getFalseSuccessor() const {
    return isConditional_ ? successors_[1] : nullptr;
}

// ============================================================================
// SwitchInst Implementation
// ============================================================================

SwitchInst::SwitchInst(Value* value, BasicBlock* defaultDest)
    : TerminatorInst(Opcode::Switch, nullptr, 1, 1), defaultDest_(defaultDest) {
    setOperand(0, value);
}

void SwitchInst::addCase(ConstantInt* value, BasicBlock* dest) {
    cases_.push_back({value, dest});
    numSuccessors_ = 1 + cases_.size();
}

// ============================================================================
// ReturnInst Implementation
// ============================================================================

ReturnInst* ReturnInst::Create() {
    return new ReturnInst();
}

ReturnInst* ReturnInst::Create(Value* value) {
    return new ReturnInst(value);
}

ReturnInst::ReturnInst()
    : TerminatorInst(Opcode::Ret, nullptr, 0, 0) {}

ReturnInst::ReturnInst(Value* value)
    : TerminatorInst(Opcode::Ret, nullptr, 1, 0) {
    setOperand(0, value);
}

// ============================================================================
// UnreachableInst Implementation
// ============================================================================

UnreachableInst* UnreachableInst::Create() {
    return new UnreachableInst();
}

UnreachableInst::UnreachableInst()
    : TerminatorInst(Opcode::Unreachable, nullptr, 0, 0) {}

// ============================================================================
// PHINode Implementation
// ============================================================================

void PHINode::addIncoming(Value* value, BasicBlock* block) {
    assert(value->getType()->equals(*getType()) && "PHI value type mismatch");
    incomingValues_.push_back(value);
    incomingBlocks_.push_back(block);
}

void PHINode::removeIncoming(unsigned i) {
    if (i < incomingValues_.size()) {
        incomingValues_.erase(incomingValues_.begin() + i);
        incomingBlocks_.erase(incomingBlocks_.begin() + i);
    }
}

Value* PHINode::getIncomingValueForBlock(BasicBlock* bb) const {
    for (unsigned i = 0; i < incomingBlocks_.size(); ++i) {
        if (incomingBlocks_[i] == bb) {
            return incomingValues_[i];
        }
    }
    return nullptr;
}

int PHINode::getBasicBlockIndex(BasicBlock* bb) const {
    for (unsigned i = 0; i < incomingBlocks_.size(); ++i) {
        if (incomingBlocks_[i] == bb) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// CallInst Implementation
// ============================================================================

CallInst::CallInst(FunctionType* funcTy, Value* callee, std::vector<Value*> args,
                   const std::string& name)
    : Instruction(Opcode::Call, funcTy->getReturnType(), 1 + args.size(), name),
      funcType_(funcTy), tailCall_(false) {
    setOperand(0, callee);
    for (size_t i = 0; i < args.size(); ++i) {
        setOperand(i + 1, args[i]);
    }
}

Function* CallInst::getCalledFunction() const {
    return dyn_cast<Function>(getCalledOperand());
}

} // namespace IR
} // namespace Backends
} // namespace XXML
