#pragma once

#include "Backends/IR/Values.h"
#include <string>
#include <vector>
#include <memory>
#include <cassert>

namespace XXML {
namespace Backends {
namespace IR {

// Forward declarations
class BasicBlock;
class Function;

// ============================================================================
// Opcode - Enumeration of all instruction types
// ============================================================================

enum class Opcode {
    // Memory operations
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // Arithmetic (integer)
    Add,
    Sub,
    Mul,
    SDiv,
    UDiv,
    SRem,
    URem,

    // Arithmetic (floating point)
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

    // Comparison
    ICmp,
    FCmp,

    // Conversion
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
    PHI,
    Select
};

// Get opcode name as string
const char* getOpcodeName(Opcode op);

// ============================================================================
// Instruction - Base class for all instructions
// ============================================================================

class Instruction : public User {
public:
    Opcode getOpcode() const { return opcode_; }
    const char* getOpcodeName() const { return IR::getOpcodeName(opcode_); }

    BasicBlock* getParent() const { return parent_; }
    void setParent(BasicBlock* bb) { parent_ = bb; }

    // Position in parent block
    Instruction* getNextNode() const { return next_; }
    Instruction* getPrevNode() const { return prev_; }

    void insertBefore(Instruction* other);
    void insertAfter(Instruction* other);
    void removeFromParent();
    void eraseFromParent();

    // Instruction classification
    bool isTerminator() const;
    bool isBinaryOp() const;
    bool isUnaryOp() const;
    bool isCast() const;
    bool isComparison() const;

    // Side effects
    bool mayHaveSideEffects() const;
    bool mayReadFromMemory() const;
    bool mayWriteToMemory() const;

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::Instruction;
    }

protected:
    Instruction(Opcode op, Type* type, unsigned numOperands, const std::string& name = "")
        : User(Kind::Instruction, type, numOperands, name), opcode_(op) {}

    Opcode opcode_;
    BasicBlock* parent_ = nullptr;

    // Linked list within basic block
    Instruction* next_ = nullptr;
    Instruction* prev_ = nullptr;

    friend class BasicBlock;
};

// ============================================================================
// Memory Instructions
// ============================================================================

class AllocaInst : public Instruction {
public:
    AllocaInst(Type* allocatedType, const std::string& name = "")
        : Instruction(Opcode::Alloca, nullptr, 0, name), allocatedType_(allocatedType),
          alignment_(allocatedType->getAlignmentInBytes()), arraySize_(nullptr) {}

    AllocaInst(Type* allocatedType, Value* arraySize, const std::string& name = "")
        : Instruction(Opcode::Alloca, nullptr, 1, name), allocatedType_(allocatedType),
          alignment_(allocatedType->getAlignmentInBytes()), arraySize_(arraySize) {
        if (arraySize) {
            setOperand(0, arraySize);
        }
    }

    Type* getAllocatedType() const { return allocatedType_; }
    Value* getArraySize() const { return arraySize_; }
    bool isArrayAllocation() const { return arraySize_ != nullptr; }

    unsigned getAlignment() const { return alignment_; }
    void setAlignment(unsigned a) { alignment_ = a; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Alloca;
    }

private:
    Type* allocatedType_;
    unsigned alignment_;
    Value* arraySize_;
};

class LoadInst : public Instruction {
public:
    LoadInst(Type* type, Value* ptr, const std::string& name = "")
        : Instruction(Opcode::Load, type, 1, name), volatile_(false), alignment_(0) {
        setOperand(0, ptr);
    }

    Value* getPointerOperand() const { return getOperand(0); }
    Value* getPointer() const { return getOperand(0); }  // Alias
    Type* getLoadedType() const { return getType(); }

    bool isVolatile() const { return volatile_; }
    void setVolatile(bool v) { volatile_ = v; }

    unsigned getAlignment() const { return alignment_; }
    void setAlignment(unsigned a) { alignment_ = a; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Load;
    }

private:
    bool volatile_;
    unsigned alignment_;
};

class StoreInst : public Instruction {
public:
    StoreInst(Value* value, Value* ptr)
        : Instruction(Opcode::Store, nullptr, 2), volatile_(false), alignment_(0) {
        // Store has no result, so type is nullptr
        setOperand(0, value);
        setOperand(1, ptr);
    }

    Value* getValueOperand() const { return getOperand(0); }
    Value* getValue() const { return getOperand(0); }  // Alias
    Value* getPointerOperand() const { return getOperand(1); }
    Value* getPointer() const { return getOperand(1); }  // Alias

    bool isVolatile() const { return volatile_; }
    void setVolatile(bool v) { volatile_ = v; }

    unsigned getAlignment() const { return alignment_; }
    void setAlignment(unsigned a) { alignment_ = a; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Store;
    }

private:
    bool volatile_;
    unsigned alignment_;
};

class GetElementPtrInst : public Instruction {
public:
    GetElementPtrInst(Type* pointeeType, Value* ptr, std::vector<Value*> indices,
                      const std::string& name = "")
        : Instruction(Opcode::GetElementPtr, nullptr, 1 + indices.size(), name),
          sourceElementType_(pointeeType), inBounds_(true) {
        setOperand(0, ptr);
        for (size_t i = 0; i < indices.size(); ++i) {
            setOperand(i + 1, indices[i]);
        }
    }

    Type* getSourceElementType() const { return sourceElementType_; }
    Value* getPointerOperand() const { return getOperand(0); }
    Value* getPointer() const { return getOperand(0); }  // Alias

    size_t getNumIndices() const { return getNumOperands() - 1; }
    Value* getIndex(size_t i) const { return getOperand(i + 1); }

    bool isInBounds() const { return inBounds_; }
    void setInBounds(bool ib) { inBounds_ = ib; }

    // Check if this is a simple struct field access (constant index 0, constant field)
    bool isStructFieldAccess() const;

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::GetElementPtr;
    }

private:
    Type* sourceElementType_;
    bool inBounds_;
};

// ============================================================================
// Binary Operations
// ============================================================================

class BinaryOperator : public Instruction {
public:
    static BinaryOperator* Create(Opcode op, Value* lhs, Value* rhs, const std::string& name = "");

    Value* getLHS() const { return getOperand(0); }
    Value* getRHS() const { return getOperand(1); }

    // Flags for integer operations
    bool hasNoSignedWrap() const { return noSignedWrap_; }
    void setNoSignedWrap(bool nsw) { noSignedWrap_ = nsw; }

    bool hasNoUnsignedWrap() const { return noUnsignedWrap_; }
    void setNoUnsignedWrap(bool nuw) { noUnsignedWrap_ = nuw; }

    bool isExact() const { return exact_; }
    void setExact(bool e) { exact_ = e; }

    // Classify operation type
    bool isIntegerOp() const;
    bool isFloatingPointOp() const;
    bool isBitwiseOp() const;

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        Opcode op = static_cast<const Instruction*>(v)->getOpcode();
        return (op >= Opcode::Add && op <= Opcode::URem) ||
               (op >= Opcode::FAdd && op <= Opcode::FNeg) ||
               (op >= Opcode::Shl && op <= Opcode::Xor);
    }

protected:
    BinaryOperator(Opcode op, Value* lhs, Value* rhs, const std::string& name)
        : Instruction(op, lhs->getType(), 2, name),
          noSignedWrap_(false), noUnsignedWrap_(false), exact_(false) {
        setOperand(0, lhs);
        setOperand(1, rhs);
    }

private:
    bool noSignedWrap_;
    bool noUnsignedWrap_;
    bool exact_;
};

// ============================================================================
// Comparison Instructions
// ============================================================================

class ICmpInst : public Instruction {
public:
    enum class Predicate {
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

    ICmpInst(Predicate pred, Value* lhs, Value* rhs, const std::string& name = "")
        : Instruction(Opcode::ICmp, nullptr, 2, name), predicate_(pred) {
        setOperand(0, lhs);
        setOperand(1, rhs);
    }

    Predicate getPredicate() const { return predicate_; }
    void setPredicate(Predicate p) { predicate_ = p; }

    Value* getLHS() const { return getOperand(0); }
    Value* getRHS() const { return getOperand(1); }

    bool isEquality() const { return predicate_ == Predicate::EQ || predicate_ == Predicate::NE; }
    bool isSigned() const;
    bool isUnsigned() const;

    static const char* getPredicateName(Predicate p);
    static Predicate getInversePredicate(Predicate p);
    static Predicate getSwappedPredicate(Predicate p);

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::ICmp;
    }

private:
    Predicate predicate_;
};

class FCmpInst : public Instruction {
public:
    enum class Predicate {
        // Ordered comparisons (false if either operand is NaN)
        OEQ,  // ordered and equal
        OGT,  // ordered and greater than
        OGE,  // ordered and greater or equal
        OLT,  // ordered and less than
        OLE,  // ordered and less or equal
        ONE,  // ordered and not equal
        ORD,  // ordered (neither is NaN)

        // Unordered comparisons (true if either operand is NaN)
        UEQ,  // unordered or equal
        UGT,  // unordered or greater than
        UGE,  // unordered or greater or equal
        ULT,  // unordered or less than
        ULE,  // unordered or less or equal
        UNE,  // unordered or not equal
        UNO,  // unordered (either is NaN)

        // Special predicates
        AlwaysTrue,  // always true
        AlwaysFalse  // always false
    };

    FCmpInst(Predicate pred, Value* lhs, Value* rhs, const std::string& name = "")
        : Instruction(Opcode::FCmp, nullptr, 2, name), predicate_(pred) {
        setOperand(0, lhs);
        setOperand(1, rhs);
    }

    Predicate getPredicate() const { return predicate_; }
    Value* getLHS() const { return getOperand(0); }
    Value* getRHS() const { return getOperand(1); }

    bool isOrdered() const;
    bool isUnordered() const;

    static const char* getPredicateName(Predicate p);

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::FCmp;
    }

private:
    Predicate predicate_;
};

// ============================================================================
// Conversion Instructions
// ============================================================================

class CastInst : public Instruction {
public:
    static CastInst* Create(Opcode op, Value* value, Type* destTy, const std::string& name = "");

    Type* getSrcTy() const { return getOperand(0)->getType(); }
    Type* getDestTy() const { return getType(); }
    Type* getDestType() const { return getType(); }  // Alias
    Value* getOperandValue() const { return getOperand(0); }
    Value* getSrcValue() const { return getOperand(0); }  // Alias

    // Check if cast between these types is valid for given opcode
    static bool castIsValid(Opcode op, Type* srcTy, Type* destTy);

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        Opcode op = static_cast<const Instruction*>(v)->getOpcode();
        return op >= Opcode::Trunc && op <= Opcode::Bitcast;
    }

protected:
    CastInst(Opcode op, Value* value, Type* destTy, const std::string& name)
        : Instruction(op, destTy, 1, name) {
        setOperand(0, value);
    }
};

// Specific cast instruction classes for type-specific operations
class TruncInst : public CastInst {
public:
    TruncInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::Trunc, value, destTy, name) {}
};

class ZExtInst : public CastInst {
public:
    ZExtInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::ZExt, value, destTy, name) {}
};

class SExtInst : public CastInst {
public:
    SExtInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::SExt, value, destTy, name) {}
};

class PtrToIntInst : public CastInst {
public:
    PtrToIntInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::PtrToInt, value, destTy, name) {}
};

class IntToPtrInst : public CastInst {
public:
    IntToPtrInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::IntToPtr, value, destTy, name) {}
};

class BitCastInst : public CastInst {
public:
    BitCastInst(Value* value, Type* destTy, const std::string& name = "")
        : CastInst(Opcode::Bitcast, value, destTy, name) {}
};

// ============================================================================
// Control Flow Instructions (Terminators)
// ============================================================================

class TerminatorInst : public Instruction {
public:
    unsigned getNumSuccessors() const { return numSuccessors_; }
    BasicBlock* getSuccessor(unsigned i) const;
    void setSuccessor(unsigned i, BasicBlock* bb);

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->isTerminator();
    }

protected:
    TerminatorInst(Opcode op, Type* type, unsigned numOperands, unsigned numSuccessors)
        : Instruction(op, type, numOperands), numSuccessors_(numSuccessors) {}

    unsigned numSuccessors_;
};

class BranchInst : public TerminatorInst {
public:
    // Unconditional branch
    static BranchInst* Create(BasicBlock* dest);

    // Conditional branch
    static BranchInst* Create(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse);

    bool isConditional() const { return isConditional_; }
    bool isUnconditional() const { return !isConditional_; }

    Value* getCondition() const {
        return isConditional_ ? getOperand(0) : nullptr;
    }

    BasicBlock* getTrueSuccessor() const;
    BasicBlock* getFalseSuccessor() const;

    // Aliases for Emitter compatibility
    BasicBlock* getTrueBlock() const { return getTrueSuccessor(); }
    BasicBlock* getFalseBlock() const { return getFalseSuccessor(); }
    BasicBlock* getDestBlock() const { return isUnconditional() ? getTrueSuccessor() : nullptr; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        Opcode op = static_cast<const Instruction*>(v)->getOpcode();
        return op == Opcode::Br || op == Opcode::CondBr;
    }

private:
    BranchInst(BasicBlock* dest);
    BranchInst(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse);

    bool isConditional_;
    BasicBlock* successors_[2];
};

class SwitchInst : public TerminatorInst {
public:
    struct Case {
        ConstantInt* value;
        BasicBlock* dest;
    };

    SwitchInst(Value* value, BasicBlock* defaultDest);

    Value* getCondition() const { return getOperand(0); }
    BasicBlock* getDefaultDest() const { return defaultDest_; }

    void addCase(ConstantInt* value, BasicBlock* dest);
    unsigned getNumCases() const { return cases_.size(); }
    const Case& getCase(unsigned i) const { return cases_[i]; }

    // Get all cases as a vector of pairs for iteration
    std::vector<std::pair<ConstantInt*, BasicBlock*>> getCases() const {
        std::vector<std::pair<ConstantInt*, BasicBlock*>> result;
        result.reserve(cases_.size());
        for (const auto& c : cases_) {
            result.emplace_back(c.value, c.dest);
        }
        return result;
    }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Switch;
    }

private:
    BasicBlock* defaultDest_;
    std::vector<Case> cases_;
};

class ReturnInst : public TerminatorInst {
public:
    // ret void
    static ReturnInst* Create();

    // ret <type> <value>
    static ReturnInst* Create(Value* value);

    bool hasReturnValue() const { return getNumOperands() > 0; }
    Value* getReturnValue() const {
        return hasReturnValue() ? getOperand(0) : nullptr;
    }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Ret;
    }

private:
    ReturnInst();
    explicit ReturnInst(Value* value);
};

class UnreachableInst : public TerminatorInst {
public:
    static UnreachableInst* Create();

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Unreachable;
    }

private:
    UnreachableInst();
};

// ============================================================================
// PHI Node
// ============================================================================

class PHINode : public Instruction {
public:
    PHINode(Type* type, unsigned numReservedValues, const std::string& name = "")
        : Instruction(Opcode::PHI, type, 0, name) {
        incomingValues_.reserve(numReservedValues);
        incomingBlocks_.reserve(numReservedValues);
    }

    unsigned getNumIncomingValues() const { return incomingValues_.size(); }
    unsigned getNumIncoming() const { return incomingValues_.size(); }  // Alias

    Value* getIncomingValue(unsigned i) const {
        return i < incomingValues_.size() ? incomingValues_[i] : nullptr;
    }

    BasicBlock* getIncomingBlock(unsigned i) const {
        return i < incomingBlocks_.size() ? incomingBlocks_[i] : nullptr;
    }

    void addIncoming(Value* value, BasicBlock* block);
    void removeIncoming(unsigned i);

    Value* getIncomingValueForBlock(BasicBlock* bb) const;
    int getBasicBlockIndex(BasicBlock* bb) const;

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::PHI;
    }

private:
    std::vector<Value*> incomingValues_;
    std::vector<BasicBlock*> incomingBlocks_;
};

// ============================================================================
// Call Instruction
// ============================================================================

class CallInst : public Instruction {
public:
    CallInst(FunctionType* funcTy, Value* callee, std::vector<Value*> args,
             const std::string& name = "");

    Value* getCalledOperand() const { return getOperand(0); }
    Value* getCallee() const { return getOperand(0); }
    Function* getCalledFunction() const;

    FunctionType* getFunctionType() const { return funcType_; }

    size_t getNumArgs() const { return getNumOperands() > 0 ? getNumOperands() - 1 : 0; }
    Value* getArgOperand(unsigned i) const {
        return i < getNumArgs() ? getOperand(i + 1) : nullptr;
    }
    Value* getArg(size_t i) const {
        return i < getNumArgs() ? getOperand(static_cast<unsigned>(i) + 1) : nullptr;
    }

    bool isTailCall() const { return tailCall_; }
    void setTailCall(bool tc) { tailCall_ = tc; }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Call;
    }

private:
    FunctionType* funcType_;
    bool tailCall_;
};

// ============================================================================
// Select Instruction
// ============================================================================

class SelectInst : public Instruction {
public:
    SelectInst(Value* cond, Value* trueVal, Value* falseVal, const std::string& name = "")
        : Instruction(Opcode::Select, trueVal->getType(), 3, name) {
        setOperand(0, cond);
        setOperand(1, trueVal);
        setOperand(2, falseVal);
    }

    Value* getCondition() const { return getOperand(0); }
    Value* getTrueValue() const { return getOperand(1); }
    Value* getFalseValue() const { return getOperand(2); }

    static bool classof(const Value* v) {
        if (v->getValueKind() != Kind::Instruction) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Select;
    }
};

// ============================================================================
// Helper template for safe downcasting (LLVM-style)
// ============================================================================

template<typename To, typename From>
inline To* dyn_cast(From* val) {
    if (val && To::classof(val)) {
        return static_cast<To*>(val);
    }
    return nullptr;
}

template<typename To, typename From>
inline const To* dyn_cast(const From* val) {
    if (val && To::classof(val)) {
        return static_cast<const To*>(val);
    }
    return nullptr;
}

template<typename To, typename From>
inline bool isa(const From* val) {
    return val && To::classof(val);
}

template<typename To, typename From>
inline To* cast(From* val) {
    assert(isa<To>(val) && "Invalid cast!");
    return static_cast<To*>(val);
}

template<typename To, typename From>
inline const To* cast(const From* val) {
    assert(isa<To>(val) && "Invalid cast!");
    return static_cast<const To*>(val);
}

} // namespace IR
} // namespace Backends
} // namespace XXML
