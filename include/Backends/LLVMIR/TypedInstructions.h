#pragma once

#include "Backends/LLVMIR/TypedValue.h"
#include <vector>
#include <memory>
#include <string>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// Forward declarations
class BasicBlock;
class Function;

// ============================================================================
// Opcode Enumeration
// ============================================================================

enum class Opcode {
    // Memory operations
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // Integer arithmetic
    Add,
    Sub,
    Mul,
    SDiv,
    UDiv,
    SRem,
    URem,

    // Float arithmetic
    FAdd,
    FSub,
    FMul,
    FDiv,
    FRem,
    FNeg,

    // Bitwise operations
    Shl,
    LShr,
    AShr,
    And,
    Or,
    Xor,

    // Comparisons
    ICmp,
    FCmp,

    // Conversions
    Trunc,
    ZExt,
    SExt,
    FPToUI,
    FPToSI,
    UIToFP,
    SIToFP,
    FPTrunc,
    FPExt,
    PtrToInt,
    IntToPtr,
    Bitcast,

    // Control flow
    Br,
    CondBr,
    Switch,
    Ret,
    Unreachable,

    // Other
    Call,
    IndirectCall,  // Call through function pointer (FFI)
    PHI,
    Select
};

const char* getOpcodeName(Opcode op);

// ============================================================================
// ICmp Predicates
// ============================================================================

enum class ICmpPredicate {
    EQ,   // equal
    NE,   // not equal
    UGT,  // unsigned greater than
    UGE,  // unsigned greater or equal
    ULT,  // unsigned less than
    ULE,  // unsigned less or equal
    SGT,  // signed greater than
    SGE,  // signed greater or equal
    SLT,  // signed less than
    SLE   // signed less or equal
};

const char* getICmpPredicateName(ICmpPredicate pred);

// ============================================================================
// FCmp Predicates
// ============================================================================

enum class FCmpPredicate {
    OEQ,  // ordered and equal
    OGT,  // ordered and greater than
    OGE,  // ordered and greater or equal
    OLT,  // ordered and less than
    OLE,  // ordered and less or equal
    ONE,  // ordered and not equal
    ORD,  // ordered (neither is NaN)
    UEQ,  // unordered or equal
    UGT,  // unordered or greater than
    UGE,  // unordered or greater or equal
    ULT,  // unordered or less than
    ULE,  // unordered or less or equal
    UNE,  // unordered or not equal
    UNO   // unordered (either is NaN)
};

const char* getFCmpPredicateName(FCmpPredicate pred);

// ============================================================================
// Instruction Base Class
// ============================================================================

class Instruction : public Value {
public:
    Opcode getOpcode() const { return opcode_; }
    const char* getOpcodeName() const { return LLVMIR::getOpcodeName(opcode_); }

    BasicBlock* getParent() const { return parent_; }
    void setParent(BasicBlock* bb) { parent_ = bb; }

    // Instruction navigation
    Instruction* getNext() const { return next_; }
    Instruction* getPrev() const { return prev_; }
    void setNext(Instruction* n) { next_ = n; }
    void setPrev(Instruction* p) { prev_ = p; }

    // Classification
    bool isTerminator() const;
    bool isBinaryOp() const;
    bool isMemoryOp() const;

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Instruction;
    }

protected:
    Instruction(Opcode op, Type* type, std::string_view name = "")
        : Value(ValueKind::Instruction, type, name), opcode_(op) {}

private:
    Opcode opcode_;
    BasicBlock* parent_ = nullptr;
    Instruction* next_ = nullptr;
    Instruction* prev_ = nullptr;

    friend class BasicBlock;
};

// ============================================================================
// Typed Binary Operations - Integer
// ============================================================================

class IntBinaryOp final : public Instruction {
public:
    IntBinaryOp(Opcode op, IntValue lhs, IntValue rhs, std::string_view name = "")
        : Instruction(op, lhs.type(), name), lhs_(lhs), rhs_(rhs) {}

    IntValue getLHS() const { return lhs_; }
    IntValue getRHS() const { return rhs_; }
    IntValue result() const { return IntValue(const_cast<IntBinaryOp*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        auto op = static_cast<const Instruction*>(v)->getOpcode();
        return op >= Opcode::Add && op <= Opcode::URem;
    }

private:
    IntValue lhs_;
    IntValue rhs_;
};

// ============================================================================
// Typed Binary Operations - Float
// ============================================================================

class FloatBinaryOp final : public Instruction {
public:
    FloatBinaryOp(Opcode op, FloatValue lhs, FloatValue rhs, std::string_view name = "")
        : Instruction(op, lhs.type(), name), lhs_(lhs), rhs_(rhs) {}

    FloatValue getLHS() const { return lhs_; }
    FloatValue getRHS() const { return rhs_; }
    FloatValue result() const { return FloatValue(const_cast<FloatBinaryOp*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        auto op = static_cast<const Instruction*>(v)->getOpcode();
        return op >= Opcode::FAdd && op <= Opcode::FNeg;
    }

private:
    FloatValue lhs_;
    FloatValue rhs_;
};

// ============================================================================
// Bitwise Operations (Integer only)
// ============================================================================

class BitwiseOp final : public Instruction {
public:
    BitwiseOp(Opcode op, IntValue lhs, IntValue rhs, std::string_view name = "")
        : Instruction(op, lhs.type(), name), lhs_(lhs), rhs_(rhs) {}

    IntValue getLHS() const { return lhs_; }
    IntValue getRHS() const { return rhs_; }
    IntValue result() const { return IntValue(const_cast<BitwiseOp*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        auto op = static_cast<const Instruction*>(v)->getOpcode();
        return op >= Opcode::Shl && op <= Opcode::Xor;
    }

private:
    IntValue lhs_;
    IntValue rhs_;
};

// ============================================================================
// Integer Comparison - Returns BoolValue (i1)
// ============================================================================

class ICmpInst final : public Instruction {
public:
    ICmpInst(ICmpPredicate pred, IntValue lhs, IntValue rhs,
             IntegerType* i1Type, std::string_view name = "")
        : Instruction(Opcode::ICmp, i1Type, name),
          predicate_(pred), lhs_(lhs), rhs_(rhs) {}

    ICmpPredicate getPredicate() const { return predicate_; }
    IntValue getLHS() const { return lhs_; }
    IntValue getRHS() const { return rhs_; }

    // Result is always i1 (bool)
    BoolValue result() const { return BoolValue(const_cast<ICmpInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::ICmp;
    }

private:
    ICmpPredicate predicate_;
    IntValue lhs_;
    IntValue rhs_;
};

// ============================================================================
// Float Comparison - Returns BoolValue (i1)
// ============================================================================

class FCmpInst final : public Instruction {
public:
    FCmpInst(FCmpPredicate pred, FloatValue lhs, FloatValue rhs,
             IntegerType* i1Type, std::string_view name = "")
        : Instruction(Opcode::FCmp, i1Type, name),
          predicate_(pred), lhs_(lhs), rhs_(rhs) {}

    FCmpPredicate getPredicate() const { return predicate_; }
    FloatValue getLHS() const { return lhs_; }
    FloatValue getRHS() const { return rhs_; }

    // Result is always i1 (bool)
    BoolValue result() const { return BoolValue(const_cast<FCmpInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::FCmp;
    }

private:
    FCmpPredicate predicate_;
    FloatValue lhs_;
    FloatValue rhs_;
};

// ============================================================================
// Pointer Comparison - Returns BoolValue (i1)
// ============================================================================

class PtrCmpInst final : public Instruction {
public:
    PtrCmpInst(ICmpPredicate pred, PtrValue lhs, PtrValue rhs,
               IntegerType* i1Type, std::string_view name = "")
        : Instruction(Opcode::ICmp, i1Type, name),
          predicate_(pred), lhs_(lhs), rhs_(rhs) {}

    ICmpPredicate getPredicate() const { return predicate_; }
    PtrValue getLHS() const { return lhs_; }
    PtrValue getRHS() const { return rhs_; }

    // Result is always i1 (bool)
    BoolValue result() const { return BoolValue(const_cast<PtrCmpInst*>(this)); }

private:
    ICmpPredicate predicate_;
    PtrValue lhs_;
    PtrValue rhs_;
};

// ============================================================================
// Float Negation
// ============================================================================

class FloatNegInst final : public Instruction {
public:
    FloatNegInst(FloatValue operand, std::string_view name = "")
        : Instruction(Opcode::FNeg, operand.type(), name), operand_(operand) {}

    FloatValue getOperand() const { return operand_; }
    FloatValue result() const { return FloatValue(const_cast<FloatNegInst*>(this)); }

private:
    FloatValue operand_;
};

// ============================================================================
// Memory Operations
// ============================================================================

class AllocaInst final : public Instruction {
public:
    AllocaInst(Type* allocatedType, PointerType* ptrType, std::string_view name = "")
        : Instruction(Opcode::Alloca, ptrType, name), allocatedType_(allocatedType) {}

    AllocaInst(Type* allocatedType, PointerType* ptrType, Value* arraySize,
               std::string_view name = "")
        : Instruction(Opcode::Alloca, ptrType, name),
          allocatedType_(allocatedType), arraySize_(arraySize) {}

    Type* getAllocatedType() const { return allocatedType_; }
    bool hasArraySize() const { return arraySize_ != nullptr; }
    Value* getArraySize() const { return arraySize_; }

    // Result is always a pointer
    PtrValue result() const { return PtrValue(const_cast<AllocaInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Alloca;
    }

private:
    Type* allocatedType_;
    Value* arraySize_ = nullptr;
};

class LoadInst final : public Instruction {
public:
    LoadInst(Type* loadedType, PtrValue ptr, std::string_view name = "")
        : Instruction(Opcode::Load, loadedType, name), ptr_(ptr) {}

    PtrValue getPointer() const { return ptr_; }
    Type* getLoadedType() const { return getType(); }

    // Result is the loaded type - use AnyValue since type varies
    AnyValue result() const { return AnyValue(const_cast<LoadInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Load;
    }

private:
    PtrValue ptr_;
};

class StoreInst final : public Instruction {
public:
    StoreInst(AnyValue value, PtrValue ptr)
        : Instruction(Opcode::Store, nullptr), value_(value), ptr_(ptr) {}

    AnyValue getValue() const { return value_; }
    PtrValue getPointer() const { return ptr_; }

    // Store has no result
    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Store;
    }

private:
    AnyValue value_;
    PtrValue ptr_;
};

class GetElementPtrInst final : public Instruction {
public:
    GetElementPtrInst(Type* sourceElemType, PtrValue base,
                      std::vector<Value*> indices, PointerType* resultType,
                      std::string_view name = "")
        : Instruction(Opcode::GetElementPtr, resultType, name),
          sourceElementType_(sourceElemType), base_(base),
          indices_(std::move(indices)) {}

    Type* getSourceElementType() const { return sourceElementType_; }
    PtrValue getBasePointer() const { return base_; }
    const std::vector<Value*>& getIndices() const { return indices_; }
    size_t getNumIndices() const { return indices_.size(); }
    Value* getIndex(size_t i) const { return indices_[i]; }

    bool isInBounds() const { return inBounds_; }
    void setInBounds(bool b) { inBounds_ = b; }

    // Result is always a pointer
    PtrValue result() const { return PtrValue(const_cast<GetElementPtrInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::GetElementPtr;
    }

private:
    Type* sourceElementType_;
    PtrValue base_;
    std::vector<Value*> indices_;
    bool inBounds_ = false;
};

// ============================================================================
// Conversion Instructions
// ============================================================================

// Integer truncation
class TruncInst final : public Instruction {
public:
    TruncInst(IntValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::Trunc, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<TruncInst*>(this)); }

private:
    IntValue src_;
    IntegerType* destType_;
};

// Zero extend
class ZExtInst final : public Instruction {
public:
    ZExtInst(IntValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::ZExt, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<ZExtInst*>(this)); }

private:
    IntValue src_;
    IntegerType* destType_;
};

// Sign extend
class SExtInst final : public Instruction {
public:
    SExtInst(IntValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::SExt, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<SExtInst*>(this)); }

private:
    IntValue src_;
    IntegerType* destType_;
};

// Float to signed int
class FPToSIInst final : public Instruction {
public:
    FPToSIInst(FloatValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::FPToSI, destType, name), src_(src), destType_(destType) {}

    FloatValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<FPToSIInst*>(this)); }

private:
    FloatValue src_;
    IntegerType* destType_;
};

// Signed int to float
class SIToFPInst final : public Instruction {
public:
    SIToFPInst(IntValue src, FloatType* destType, std::string_view name = "")
        : Instruction(Opcode::SIToFP, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    IntValue getSrc() const { return src_; }
    FloatType* getDestType() const { return destType_; }
    FloatValue result() const { return FloatValue(const_cast<SIToFPInst*>(this)); }

private:
    IntValue src_;
    FloatType* destType_;
};

// Unsigned int to float
class UIToFPInst final : public Instruction {
public:
    UIToFPInst(IntValue src, FloatType* destType, std::string_view name = "")
        : Instruction(Opcode::UIToFP, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    FloatType* getDestType() const { return destType_; }
    FloatValue result() const { return FloatValue(const_cast<UIToFPInst*>(this)); }

private:
    IntValue src_;
    FloatType* destType_;
};

// Float to unsigned int
class FPToUIInst final : public Instruction {
public:
    FPToUIInst(FloatValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::FPToUI, destType, name), src_(src), destType_(destType) {}

    FloatValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<FPToUIInst*>(this)); }

private:
    FloatValue src_;
    IntegerType* destType_;
};

// Float truncation (e.g., double to float)
class FPTruncInst final : public Instruction {
public:
    FPTruncInst(FloatValue src, FloatType* destType, std::string_view name = "")
        : Instruction(Opcode::FPTrunc, destType, name), src_(src), destType_(destType) {}

    FloatValue getSource() const { return src_; }
    FloatType* getDestType() const { return destType_; }
    FloatValue result() const { return FloatValue(const_cast<FPTruncInst*>(this)); }

private:
    FloatValue src_;
    FloatType* destType_;
};

// Float extension (e.g., float to double)
class FPExtInst final : public Instruction {
public:
    FPExtInst(FloatValue src, FloatType* destType, std::string_view name = "")
        : Instruction(Opcode::FPExt, destType, name), src_(src), destType_(destType) {}

    FloatValue getSource() const { return src_; }
    FloatType* getDestType() const { return destType_; }
    FloatValue result() const { return FloatValue(const_cast<FPExtInst*>(this)); }

private:
    FloatValue src_;
    FloatType* destType_;
};

// Pointer to int
class PtrToIntInst final : public Instruction {
public:
    PtrToIntInst(PtrValue src, IntegerType* destType, std::string_view name = "")
        : Instruction(Opcode::PtrToInt, destType, name), src_(src), destType_(destType) {}

    PtrValue getSource() const { return src_; }
    IntegerType* getDestType() const { return destType_; }
    IntValue result() const { return IntValue(const_cast<PtrToIntInst*>(this)); }

private:
    PtrValue src_;
    IntegerType* destType_;
};

// Int to pointer
class IntToPtrInst final : public Instruction {
public:
    IntToPtrInst(IntValue src, PointerType* destType, std::string_view name = "")
        : Instruction(Opcode::IntToPtr, destType, name), src_(src), destType_(destType) {}

    IntValue getSource() const { return src_; }
    PointerType* getDestType() const { return destType_; }
    PtrValue result() const { return PtrValue(const_cast<IntToPtrInst*>(this)); }

private:
    IntValue src_;
    PointerType* destType_;
};

// ============================================================================
// Control Flow - Terminators
// ============================================================================

class TerminatorInst : public Instruction {
public:
    virtual size_t getNumSuccessors() const = 0;
    virtual BasicBlock* getSuccessor(size_t i) const = 0;

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->isTerminator();
    }

protected:
    TerminatorInst(Opcode op, Type* type) : Instruction(op, type) {}
};

// Unconditional branch
class BranchInst final : public TerminatorInst {
public:
    explicit BranchInst(BasicBlock* dest)
        : TerminatorInst(Opcode::Br, nullptr), dest_(dest) {}

    BasicBlock* getDestination() const { return dest_; }
    BasicBlock* getDest() const { return dest_; }

    size_t getNumSuccessors() const override { return 1; }
    BasicBlock* getSuccessor(size_t i) const override { return i == 0 ? dest_ : nullptr; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        auto* inst = static_cast<const Instruction*>(v);
        return inst->getOpcode() == Opcode::Br;
    }

private:
    BasicBlock* dest_;
};

// Conditional branch - REQUIRES BoolValue condition
class CondBranchInst final : public TerminatorInst {
public:
    CondBranchInst(BoolValue cond, BasicBlock* ifTrue, BasicBlock* ifFalse)
        : TerminatorInst(Opcode::CondBr, nullptr),
          condition_(cond), ifTrue_(ifTrue), ifFalse_(ifFalse) {}

    BoolValue getCondition() const { return condition_; }
    BasicBlock* getTrueBranch() const { return ifTrue_; }
    BasicBlock* getFalseBranch() const { return ifFalse_; }
    BasicBlock* getTrueBlock() const { return ifTrue_; }
    BasicBlock* getFalseBlock() const { return ifFalse_; }

    size_t getNumSuccessors() const override { return 2; }
    BasicBlock* getSuccessor(size_t i) const override {
        return i == 0 ? ifTrue_ : (i == 1 ? ifFalse_ : nullptr);
    }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::CondBr;
    }

private:
    BoolValue condition_;
    BasicBlock* ifTrue_;
    BasicBlock* ifFalse_;
};

// Return - void
class ReturnVoidInst final : public TerminatorInst {
public:
    ReturnVoidInst() : TerminatorInst(Opcode::Ret, nullptr) {}

    size_t getNumSuccessors() const override { return 0; }
    BasicBlock* getSuccessor(size_t) const override { return nullptr; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        auto* inst = static_cast<const Instruction*>(v);
        return inst->getOpcode() == Opcode::Ret && inst->getType() == nullptr;
    }
};

// Return - with value
class ReturnValueInst final : public TerminatorInst {
public:
    explicit ReturnValueInst(AnyValue value)
        : TerminatorInst(Opcode::Ret, value.type()), value_(value) {}

    AnyValue getValue() const { return value_; }

    size_t getNumSuccessors() const override { return 0; }
    BasicBlock* getSuccessor(size_t) const override { return nullptr; }

private:
    AnyValue value_;
};

// Unreachable
class UnreachableInst final : public TerminatorInst {
public:
    UnreachableInst() : TerminatorInst(Opcode::Unreachable, nullptr) {}

    size_t getNumSuccessors() const override { return 0; }
    BasicBlock* getSuccessor(size_t) const override { return nullptr; }
};

// ============================================================================
// Call Instruction
// ============================================================================

class CallInst final : public Instruction {
public:
    CallInst(Function* callee, std::vector<Value*> args, std::string_view name = "");

    Function* getCallee() const { return callee_; }
    const std::vector<Value*>& getArguments() const { return args_; }
    const std::vector<Value*>& getArgs() const { return args_; }
    size_t getNumArgs() const { return args_.size(); }
    Value* getArg(size_t i) const { return args_[i]; }

    // Result type depends on function return type
    AnyValue result() const { return AnyValue(const_cast<CallInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Call;
    }

private:
    Function* callee_;
    std::vector<Value*> args_;
};

// ============================================================================
// Indirect Call Instruction - Call through function pointer (for FFI)
// ============================================================================

/// Calling convention for indirect calls
enum class CallingConv {
    CDecl,      // Standard C calling convention
    StdCall,    // Windows stdcall
    FastCall    // Fast calling convention
};

class IndirectCallInst final : public Instruction {
public:
    IndirectCallInst(PtrValue funcPtr, FunctionType* funcType,
                     std::vector<Value*> args, std::string_view name = "",
                     CallingConv cc = CallingConv::CDecl);

    PtrValue getFuncPtr() const { return funcPtr_; }
    FunctionType* getFuncType() const { return funcType_; }
    const std::vector<Value*>& getArguments() const { return args_; }
    const std::vector<Value*>& getArgs() const { return args_; }
    size_t getNumArgs() const { return args_.size(); }
    Value* getArg(size_t i) const { return args_[i]; }
    CallingConv getCallingConv() const { return callingConv_; }

    // Result type depends on function return type
    AnyValue result() const { return AnyValue(const_cast<IndirectCallInst*>(this)); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::IndirectCall;
    }

private:
    PtrValue funcPtr_;
    FunctionType* funcType_;
    std::vector<Value*> args_;
    CallingConv callingConv_;
};

// ============================================================================
// PHI Node - Type-safe template
// ============================================================================

template<typename T>
class TypedPHINode final : public Instruction {
public:
    TypedPHINode(Type* type, std::string_view name = "")
        : Instruction(Opcode::PHI, type, name) {}

    void addIncoming(T value, BasicBlock* block) {
        incoming_.push_back({value, block});
    }

    const std::vector<std::pair<T, BasicBlock*>>& getIncoming() const { return incoming_; }
    size_t getNumIncoming() const { return incoming_.size(); }
    T getIncomingValue(size_t i) const { return incoming_[i].first; }
    BasicBlock* getIncomingBlock(size_t i) const { return incoming_[i].second; }

    T result() const;  // Defined based on specialization

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::PHI;
    }

private:
    std::vector<std::pair<T, BasicBlock*>> incoming_;
};

// PHI node type aliases
using IntPHI = TypedPHINode<IntValue>;
using FloatPHI = TypedPHINode<FloatValue>;
using PtrPHI = TypedPHINode<PtrValue>;

// ============================================================================
// Select Instruction - Type-safe template
// ============================================================================

template<typename T>
class TypedSelectInst final : public Instruction {
public:
    TypedSelectInst(BoolValue cond, T trueVal, T falseVal, std::string_view name = "")
        : Instruction(Opcode::Select, trueVal.raw()->getType(), name),
          condition_(cond), trueVal_(trueVal), falseVal_(falseVal) {}

    BoolValue getCondition() const { return condition_; }
    T getTrueValue() const { return trueVal_; }
    T getFalseValue() const { return falseVal_; }

    T result() const;  // Defined based on specialization

    static bool classof(const Value* v) {
        if (v->getValueKind() != ValueKind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Select;
    }

private:
    BoolValue condition_;
    T trueVal_;
    T falseVal_;
};

// Select instruction type aliases
using IntSelect = TypedSelectInst<IntValue>;
using FloatSelect = TypedSelectInst<FloatValue>;
using PtrSelect = TypedSelectInst<PtrValue>;

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
