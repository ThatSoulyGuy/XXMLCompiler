#include "Backends/LLVMIR/TypedInstructions.h"
#include "Backends/LLVMIR/TypedModule.h"

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// CallInst Implementation
// ============================================================================

CallInst::CallInst(Function* callee, std::vector<Value*> args, std::string_view name)
    : Instruction(Opcode::Call, callee->getReturnType(), name),
      callee_(callee), args_(std::move(args)) {}

// ============================================================================
// IndirectCallInst Implementation
// ============================================================================

IndirectCallInst::IndirectCallInst(PtrValue funcPtr, FunctionType* funcType,
                                   std::vector<Value*> args, std::string_view name,
                                   CallingConv cc)
    : Instruction(Opcode::IndirectCall, funcType->getReturnType(), name),
      funcPtr_(funcPtr), funcType_(funcType), args_(std::move(args)),
      callingConv_(cc) {}

// ============================================================================
// Opcode Names
// ============================================================================

const char* getOpcodeName(Opcode op) {
    switch (op) {
        case Opcode::Alloca: return "alloca";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::GetElementPtr: return "getelementptr";

        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::SDiv: return "sdiv";
        case Opcode::UDiv: return "udiv";
        case Opcode::SRem: return "srem";
        case Opcode::URem: return "urem";

        case Opcode::FAdd: return "fadd";
        case Opcode::FSub: return "fsub";
        case Opcode::FMul: return "fmul";
        case Opcode::FDiv: return "fdiv";
        case Opcode::FRem: return "frem";
        case Opcode::FNeg: return "fneg";

        case Opcode::Shl: return "shl";
        case Opcode::LShr: return "lshr";
        case Opcode::AShr: return "ashr";
        case Opcode::And: return "and";
        case Opcode::Or: return "or";
        case Opcode::Xor: return "xor";

        case Opcode::ICmp: return "icmp";
        case Opcode::FCmp: return "fcmp";

        case Opcode::Trunc: return "trunc";
        case Opcode::ZExt: return "zext";
        case Opcode::SExt: return "sext";
        case Opcode::FPToUI: return "fptoui";
        case Opcode::FPToSI: return "fptosi";
        case Opcode::UIToFP: return "uitofp";
        case Opcode::SIToFP: return "sitofp";
        case Opcode::FPTrunc: return "fptrunc";
        case Opcode::FPExt: return "fpext";
        case Opcode::PtrToInt: return "ptrtoint";
        case Opcode::IntToPtr: return "inttoptr";
        case Opcode::Bitcast: return "bitcast";

        case Opcode::Br: return "br";
        case Opcode::CondBr: return "br";
        case Opcode::Switch: return "switch";
        case Opcode::Ret: return "ret";
        case Opcode::Unreachable: return "unreachable";

        case Opcode::Call: return "call";
        case Opcode::IndirectCall: return "call";  // Same opcode name in IR
        case Opcode::PHI: return "phi";
        case Opcode::Select: return "select";
    }
    return "unknown";
}

// ============================================================================
// ICmp Predicate Names
// ============================================================================

const char* getICmpPredicateName(ICmpPredicate pred) {
    switch (pred) {
        case ICmpPredicate::EQ: return "eq";
        case ICmpPredicate::NE: return "ne";
        case ICmpPredicate::UGT: return "ugt";
        case ICmpPredicate::UGE: return "uge";
        case ICmpPredicate::ULT: return "ult";
        case ICmpPredicate::ULE: return "ule";
        case ICmpPredicate::SGT: return "sgt";
        case ICmpPredicate::SGE: return "sge";
        case ICmpPredicate::SLT: return "slt";
        case ICmpPredicate::SLE: return "sle";
    }
    return "unknown";
}

// ============================================================================
// FCmp Predicate Names
// ============================================================================

const char* getFCmpPredicateName(FCmpPredicate pred) {
    switch (pred) {
        case FCmpPredicate::OEQ: return "oeq";
        case FCmpPredicate::OGT: return "ogt";
        case FCmpPredicate::OGE: return "oge";
        case FCmpPredicate::OLT: return "olt";
        case FCmpPredicate::OLE: return "ole";
        case FCmpPredicate::ONE: return "one";
        case FCmpPredicate::ORD: return "ord";
        case FCmpPredicate::UEQ: return "ueq";
        case FCmpPredicate::UGT: return "ugt";
        case FCmpPredicate::UGE: return "uge";
        case FCmpPredicate::ULT: return "ult";
        case FCmpPredicate::ULE: return "ule";
        case FCmpPredicate::UNE: return "une";
        case FCmpPredicate::UNO: return "uno";
    }
    return "unknown";
}

// ============================================================================
// Instruction Classification
// ============================================================================

bool Instruction::isTerminator() const {
    switch (opcode_) {
        case Opcode::Br:
        case Opcode::CondBr:
        case Opcode::Switch:
        case Opcode::Ret:
        case Opcode::Unreachable:
            return true;
        default:
            return false;
    }
}

bool Instruction::isBinaryOp() const {
    return (opcode_ >= Opcode::Add && opcode_ <= Opcode::URem) ||
           (opcode_ >= Opcode::FAdd && opcode_ <= Opcode::FNeg) ||
           (opcode_ >= Opcode::Shl && opcode_ <= Opcode::Xor);
}

bool Instruction::isMemoryOp() const {
    return opcode_ == Opcode::Alloca ||
           opcode_ == Opcode::Load ||
           opcode_ == Opcode::Store ||
           opcode_ == Opcode::GetElementPtr;
}

// ============================================================================
// TypedPHINode result() specializations
// ============================================================================

template<>
IntValue TypedPHINode<IntValue>::result() const {
    return IntValue(const_cast<TypedPHINode<IntValue>*>(this));
}

template<>
FloatValue TypedPHINode<FloatValue>::result() const {
    return FloatValue(const_cast<TypedPHINode<FloatValue>*>(this));
}

template<>
PtrValue TypedPHINode<PtrValue>::result() const {
    return PtrValue(const_cast<TypedPHINode<PtrValue>*>(this));
}

// ============================================================================
// TypedSelectInst result() specializations
// ============================================================================

template<>
IntValue TypedSelectInst<IntValue>::result() const {
    return IntValue(const_cast<TypedSelectInst<IntValue>*>(this));
}

template<>
FloatValue TypedSelectInst<FloatValue>::result() const {
    return FloatValue(const_cast<TypedSelectInst<FloatValue>*>(this));
}

template<>
PtrValue TypedSelectInst<PtrValue>::result() const {
    return PtrValue(const_cast<TypedSelectInst<PtrValue>*>(this));
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
