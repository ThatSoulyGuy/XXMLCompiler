#pragma once

#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/TypedInstructions.h"
#include "Backends/LLVMIR/TypedModule.h"
#include <memory>
#include <vector>
#include <string>
#include <string_view>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// IRBuilder - Type-Safe Instruction Builder
// ============================================================================

/// IRBuilder creates LLVM IR instructions with compile-time type safety.
/// Methods are designed so that incorrect type usage results in compile errors,
/// not runtime crashes or invalid IR output.
///
/// Key design principles:
/// 1. Integer operations only accept IntValue, return IntValue
/// 2. Float operations only accept FloatValue, return FloatValue
/// 3. Pointer operations return PtrValue
/// 4. Comparison operations return BoolValue (i1)
/// 5. Conditional branches require BoolValue, not generic IntValue
/// 6. PHI nodes and select instructions are templated on result type

class IRBuilder {
public:
    explicit IRBuilder(Module& module)
        : module_(module), insertBlock_(nullptr), insertPoint_(nullptr) {}

    // ========================================================================
    // Insertion Point Management
    // ========================================================================

    void setInsertPoint(BasicBlock* block) {
        insertBlock_ = block;
        insertPoint_ = nullptr;  // Insert at end
    }

    void setInsertPoint(BasicBlock* block, Instruction* before) {
        insertBlock_ = block;
        insertPoint_ = before;
    }

    BasicBlock* getInsertBlock() const { return insertBlock_; }

    // ========================================================================
    // Constants
    // ========================================================================

    IntValue getInt1(bool value) {
        auto* ty = module_.getContext().getInt1Ty();
        return IntValue(module_.getConstantInt(ty, value ? 1 : 0));
    }

    IntValue getInt8(int8_t value) {
        auto* ty = module_.getContext().getInt8Ty();
        return IntValue(module_.getConstantInt(ty, value));
    }

    IntValue getInt16(int16_t value) {
        auto* ty = module_.getContext().getInt16Ty();
        return IntValue(module_.getConstantInt(ty, value));
    }

    IntValue getInt32(int32_t value) {
        auto* ty = module_.getContext().getInt32Ty();
        return IntValue(module_.getConstantInt(ty, value));
    }

    IntValue getInt64(int64_t value) {
        auto* ty = module_.getContext().getInt64Ty();
        return IntValue(module_.getConstantInt(ty, value));
    }

    IntValue getIntN(unsigned bitWidth, int64_t value) {
        auto* ty = module_.getContext().getIntNTy(bitWidth);
        return IntValue(module_.getConstantInt(ty, value));
    }

    FloatValue getFloat(float value) {
        auto* ty = module_.getContext().getFloatTy();
        return FloatValue(module_.getConstantFP(ty, value));
    }

    FloatValue getDouble(double value) {
        auto* ty = module_.getContext().getDoubleTy();
        return FloatValue(module_.getConstantFP(ty, value));
    }

    PtrValue getNullPtr() {
        auto* ty = module_.getContext().getPtrTy();
        return PtrValue(module_.getConstantNull(ty));
    }

    BoolValue getTrue() { return getInt1(true); }
    BoolValue getFalse() { return getInt1(false); }

    // ========================================================================
    // Type Accessors
    // ========================================================================

    VoidType* getVoidTy() { return module_.getContext().getVoidTy(); }
    IntegerType* getInt1Ty() { return module_.getContext().getInt1Ty(); }
    IntegerType* getInt8Ty() { return module_.getContext().getInt8Ty(); }
    IntegerType* getInt16Ty() { return module_.getContext().getInt16Ty(); }
    IntegerType* getInt32Ty() { return module_.getContext().getInt32Ty(); }
    IntegerType* getInt64Ty() { return module_.getContext().getInt64Ty(); }
    IntegerType* getIntNTy(unsigned n) { return module_.getContext().getIntNTy(n); }
    FloatType* getFloatTy() { return module_.getContext().getFloatTy(); }
    FloatType* getDoubleTy() { return module_.getContext().getDoubleTy(); }
    PointerType* getPtrTy() { return module_.getContext().getPtrTy(); }

    // ========================================================================
    // Memory Operations
    // ========================================================================

    /// Create an alloca instruction. Returns a pointer to the allocated memory.
    PtrValue createAlloca(Type* type, std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        auto* inst = createInst<AllocaInst>(type, ptrTy, name);
        return inst->result();
    }

    /// Create an alloca with array size.
    PtrValue createAlloca(Type* type, IntValue arraySize, std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        auto* inst = createInst<AllocaInst>(type, ptrTy, arraySize.raw(), name);
        return inst->result();
    }

    /// Load an integer value from a pointer.
    IntValue createLoadInt(IntegerType* type, PtrValue ptr, std::string_view name = "") {
        auto* inst = createInst<LoadInst>(type, ptr, name);
        return IntValue(inst);
    }

    /// Load a float value from a pointer.
    FloatValue createLoadFloat(FloatType* type, PtrValue ptr, std::string_view name = "") {
        auto* inst = createInst<LoadInst>(type, ptr, name);
        return FloatValue(inst);
    }

    /// Load a pointer value from a pointer.
    PtrValue createLoadPtr(PtrValue ptr, std::string_view name = "") {
        auto* ty = module_.getContext().getPtrTy();
        auto* inst = createInst<LoadInst>(ty, ptr, name);
        return PtrValue(inst);
    }
    // Generic load for runtime type dispatch - returns AnyValue
    AnyValue createLoad(Type* type, PtrValue ptr, std::string_view name = "") {
        auto* inst = createInst<LoadInst>(type, ptr, name);
        return AnyValue(inst);
    }

    /// Store an integer value to a pointer.
    void createStore(IntValue value, PtrValue ptr) {
        createInst<StoreInst>(AnyValue(value), ptr);
    }

    /// Store a float value to a pointer.
    void createStore(FloatValue value, PtrValue ptr) {
        createInst<StoreInst>(AnyValue(value), ptr);
    }

    /// Store a pointer value to a pointer.
    void createStore(PtrValue value, PtrValue ptr) {
        createInst<StoreInst>(AnyValue(value), ptr);
    }
    // Generic store for AnyValue (runtime type dispatch)
    void createStore(AnyValue value, PtrValue ptr) {
        createInst<StoreInst>(value, ptr);
    }

    /// Create a GEP instruction. Returns a pointer.
    PtrValue createGEP(Type* elementType, PtrValue ptr,
                       std::vector<IntValue> indices,
                       std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        std::vector<Value*> rawIndices;
        for (const auto& idx : indices) {
            rawIndices.push_back(idx.raw());
        }
        auto* inst = createInst<GetElementPtrInst>(elementType, ptr, std::move(rawIndices), ptrTy, name);
        return inst->result();
    }

    /// Create a struct GEP (constant indices).
    PtrValue createStructGEP(StructType* structType, PtrValue ptr,
                             unsigned fieldIndex, std::string_view name = "") {
        std::vector<IntValue> indices = {
            getInt32(0),
            getInt32(static_cast<int32_t>(fieldIndex))
        };
        return createGEP(structType, ptr, indices, name);
    }

    /// Create an in-bounds GEP.
    PtrValue createInBoundsGEP(Type* elementType, PtrValue ptr,
                               std::vector<IntValue> indices,
                               std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        std::vector<Value*> rawIndices;
        for (const auto& idx : indices) {
            rawIndices.push_back(idx.raw());
        }
        auto* inst = createInst<GetElementPtrInst>(elementType, ptr, std::move(rawIndices), ptrTy, name);
        inst->setInBounds(true);
        return inst->result();
    }

    // ========================================================================
    // Integer Arithmetic (ONLY accepts IntValue, returns IntValue)
    // ========================================================================

    IntValue createAdd(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::Add, lhs, rhs, name);
        return inst->result();
    }

    IntValue createSub(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::Sub, lhs, rhs, name);
        return inst->result();
    }

    IntValue createMul(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::Mul, lhs, rhs, name);
        return inst->result();
    }

    IntValue createSDiv(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::SDiv, lhs, rhs, name);
        return inst->result();
    }

    IntValue createUDiv(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::UDiv, lhs, rhs, name);
        return inst->result();
    }

    IntValue createSRem(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::SRem, lhs, rhs, name);
        return inst->result();
    }

    IntValue createURem(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<IntBinaryOp>(Opcode::URem, lhs, rhs, name);
        return inst->result();
    }

    IntValue createNeg(IntValue val, std::string_view name = "") {
        return createSub(getIntN(val.getBitWidth(), 0), val, name);
    }

    // ========================================================================
    // Floating Point Arithmetic (ONLY accepts FloatValue, returns FloatValue)
    // ========================================================================

    FloatValue createFAdd(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* inst = createInst<FloatBinaryOp>(Opcode::FAdd, lhs, rhs, name);
        return inst->result();
    }

    FloatValue createFSub(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* inst = createInst<FloatBinaryOp>(Opcode::FSub, lhs, rhs, name);
        return inst->result();
    }

    FloatValue createFMul(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* inst = createInst<FloatBinaryOp>(Opcode::FMul, lhs, rhs, name);
        return inst->result();
    }

    FloatValue createFDiv(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* inst = createInst<FloatBinaryOp>(Opcode::FDiv, lhs, rhs, name);
        return inst->result();
    }

    FloatValue createFRem(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* inst = createInst<FloatBinaryOp>(Opcode::FRem, lhs, rhs, name);
        return inst->result();
    }

    FloatValue createFNeg(FloatValue val, std::string_view name = "") {
        auto* inst = createInst<FloatNegInst>(val, name);
        return inst->result();
    }

    // ========================================================================
    // Bitwise Operations (ONLY accepts IntValue, returns IntValue)
    // ========================================================================

    IntValue createShl(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::Shl, lhs, rhs, name);
        return inst->result();
    }

    IntValue createLShr(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::LShr, lhs, rhs, name);
        return inst->result();
    }

    IntValue createAShr(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::AShr, lhs, rhs, name);
        return inst->result();
    }

    IntValue createAnd(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::And, lhs, rhs, name);
        return inst->result();
    }

    IntValue createOr(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::Or, lhs, rhs, name);
        return inst->result();
    }

    IntValue createXor(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* inst = createInst<BitwiseOp>(Opcode::Xor, lhs, rhs, name);
        return inst->result();
    }

    IntValue createNot(IntValue val, std::string_view name = "") {
        // XOR with all 1s
        return createXor(val, getIntN(val.getBitWidth(), -1), name);
    }

    // ========================================================================
    // Integer Comparisons (returns BoolValue = i1)
    // ========================================================================

    BoolValue createICmpEQ(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::EQ, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpNE(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::NE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpSLT(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::SLT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpSLE(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::SLE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpSGT(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::SGT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpSGE(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::SGE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpULT(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::ULT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpULE(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::ULE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpUGT(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::UGT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createICmpUGE(IntValue lhs, IntValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<ICmpInst>(ICmpPredicate::UGE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    // ========================================================================
    // Pointer Comparisons (returns BoolValue)
    // ========================================================================

    BoolValue createPtrEQ(PtrValue lhs, PtrValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<PtrCmpInst>(ICmpPredicate::EQ, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createPtrNE(PtrValue lhs, PtrValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<PtrCmpInst>(ICmpPredicate::NE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createIsNull(PtrValue ptr, std::string_view name = "") {
        return createPtrEQ(ptr, getNullPtr(), name);
    }

    BoolValue createIsNotNull(PtrValue ptr, std::string_view name = "") {
        return createPtrNE(ptr, getNullPtr(), name);
    }

    // ========================================================================
    // Float Comparisons (returns BoolValue = i1)
    // ========================================================================

    BoolValue createFCmpOEQ(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::OEQ, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpONE(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::ONE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpOLT(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::OLT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpOLE(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::OLE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpOGT(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::OGT, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpOGE(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::OGE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpUEQ(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::UEQ, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    BoolValue createFCmpUNE(FloatValue lhs, FloatValue rhs, std::string_view name = "") {
        auto* i1Ty = module_.getContext().getInt1Ty();
        auto* inst = createInst<FCmpInst>(FCmpPredicate::UNE, lhs, rhs, i1Ty, name);
        return inst->result();
    }

    // ========================================================================
    // Type Conversions - Integer
    // ========================================================================

    IntValue createTrunc(IntValue val, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<TruncInst>(val, destTy, name);
        return inst->result();
    }

    IntValue createZExt(IntValue val, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<ZExtInst>(val, destTy, name);
        return inst->result();
    }

    IntValue createSExt(IntValue val, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<SExtInst>(val, destTy, name);
        return inst->result();
    }

    /// Extend or truncate to target width (sign-extend if widening)
    IntValue createIntCast(IntValue val, IntegerType* destTy, bool isSigned = true,
                           std::string_view name = "") {
        unsigned srcWidth = val.getBitWidth();
        unsigned destWidth = destTy->getBitWidth();

        if (srcWidth == destWidth) {
            return val;
        } else if (srcWidth > destWidth) {
            return createTrunc(val, destTy, name);
        } else {
            return isSigned ? createSExt(val, destTy, name) : createZExt(val, destTy, name);
        }
    }

    // ========================================================================
    // Type Conversions - Float
    // ========================================================================

    FloatValue createFPTrunc(FloatValue val, FloatType* destTy, std::string_view name = "") {
        auto* inst = createInst<FPTruncInst>(val, destTy, name);
        return inst->result();
    }

    FloatValue createFPExt(FloatValue val, FloatType* destTy, std::string_view name = "") {
        auto* inst = createInst<FPExtInst>(val, destTy, name);
        return inst->result();
    }

    FloatValue createFPCast(FloatValue val, FloatType* destTy, std::string_view name = "") {
        size_t srcBits = val.type()->getSizeInBits();
        size_t destBits = destTy->getSizeInBits();

        if (srcBits == destBits) {
            return val;
        } else if (srcBits > destBits) {
            return createFPTrunc(val, destTy, name);
        } else {
            return createFPExt(val, destTy, name);
        }
    }

    // ========================================================================
    // Type Conversions - Float <-> Int
    // ========================================================================

    IntValue createFPToSI(FloatValue val, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<FPToSIInst>(val, destTy, name);
        return inst->result();
    }

    IntValue createFPToUI(FloatValue val, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<FPToUIInst>(val, destTy, name);
        return inst->result();
    }

    FloatValue createSIToFP(IntValue val, FloatType* destTy, std::string_view name = "") {
        auto* inst = createInst<SIToFPInst>(val, destTy, name);
        return inst->result();
    }

    FloatValue createUIToFP(IntValue val, FloatType* destTy, std::string_view name = "") {
        auto* inst = createInst<UIToFPInst>(val, destTy, name);
        return inst->result();
    }

    // ========================================================================
    // Type Conversions - Pointer <-> Int
    // ========================================================================

    IntValue createPtrToInt(PtrValue ptr, IntegerType* destTy, std::string_view name = "") {
        auto* inst = createInst<PtrToIntInst>(ptr, destTy, name);
        return inst->result();
    }

    PtrValue createIntToPtr(IntValue val, std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        auto* inst = createInst<IntToPtrInst>(val, ptrTy, name);
        return inst->result();
    }

    // ========================================================================
    // Control Flow - Terminators
    // ========================================================================

    /// Unconditional branch
    void createBr(BasicBlock* dest) {
        createInst<BranchInst>(dest);
    }

    /// Conditional branch - REQUIRES BoolValue (i1), not generic IntValue!
    /// This is the key type-safety feature: you cannot accidentally pass
    /// an i32 or other integer type here.
    void createCondBr(BoolValue condition, BasicBlock* ifTrue, BasicBlock* ifFalse) {
        createInst<CondBranchInst>(condition, ifTrue, ifFalse);
    }

    /// Return void
    void createRetVoid() {
        createInst<ReturnVoidInst>();
    }

    /// Return an integer value
    void createRet(IntValue val) {
        createInst<ReturnValueInst>(AnyValue(val));
    }

    /// Return a float value
    void createRet(FloatValue val) {
        createInst<ReturnValueInst>(AnyValue(val));
    }

    /// Return a pointer value
    void createRet(PtrValue val) {
        createInst<ReturnValueInst>(AnyValue(val));
    }

    /// Return any value (when type is determined at runtime)
    void createRet(AnyValue val) {
        if (val.isVoid()) {
            createRetVoid();
        } else {
            createInst<ReturnValueInst>(val);
        }
    }

    /// Unreachable instruction
    void createUnreachable() {
        createInst<UnreachableInst>();
    }

    // ========================================================================
    // Function Calls
    // ========================================================================

    /// Call a function returning void
    void createCallVoid(Function* callee, std::vector<AnyValue> args) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        createInst<CallInst>(callee, std::move(rawArgs), "");
    }

    /// Call a function returning an integer
    IntValue createCallInt(Function* callee, std::vector<AnyValue> args,
                           std::string_view name = "") {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<CallInst>(callee, std::move(rawArgs), name);
        return IntValue(inst);
    }

    /// Call a function returning a float
    FloatValue createCallFloat(Function* callee, std::vector<AnyValue> args,
                               std::string_view name = "") {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<CallInst>(callee, std::move(rawArgs), name);
        return FloatValue(inst);
    }

    /// Call a function returning a pointer
    PtrValue createCallPtr(Function* callee, std::vector<AnyValue> args,
                           std::string_view name = "") {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<CallInst>(callee, std::move(rawArgs), name);
        return PtrValue(inst);
    }

    /// Call with runtime-determined return type
    AnyValue createCall(Function* callee, std::vector<AnyValue> args,
                        std::string_view name = "") {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<CallInst>(callee, std::move(rawArgs), name);
        return AnyValue(inst);
    }

    // ========================================================================
    // Indirect Calls (for FFI)
    // ========================================================================

    /// Indirect call through function pointer (void return)
    void createIndirectCallVoid(PtrValue funcPtr, FunctionType* funcType,
                                std::vector<AnyValue> args,
                                CallingConv cc = CallingConv::CDecl) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        createInst<IndirectCallInst>(funcPtr, funcType, std::move(rawArgs), "", cc);
    }

    /// Indirect call through function pointer (integer return)
    IntValue createIndirectCallInt(PtrValue funcPtr, FunctionType* funcType,
                                   std::vector<AnyValue> args,
                                   std::string_view name = "",
                                   CallingConv cc = CallingConv::CDecl) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<IndirectCallInst>(funcPtr, funcType, std::move(rawArgs), name, cc);
        return IntValue(inst);
    }

    /// Indirect call through function pointer (float return)
    FloatValue createIndirectCallFloat(PtrValue funcPtr, FunctionType* funcType,
                                       std::vector<AnyValue> args,
                                       std::string_view name = "",
                                       CallingConv cc = CallingConv::CDecl) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<IndirectCallInst>(funcPtr, funcType, std::move(rawArgs), name, cc);
        return FloatValue(inst);
    }

    /// Indirect call through function pointer (pointer return)
    PtrValue createIndirectCallPtr(PtrValue funcPtr, FunctionType* funcType,
                                   std::vector<AnyValue> args,
                                   std::string_view name = "",
                                   CallingConv cc = CallingConv::CDecl) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<IndirectCallInst>(funcPtr, funcType, std::move(rawArgs), name, cc);
        return PtrValue(inst);
    }

    /// Indirect call with runtime-determined return type
    AnyValue createIndirectCall(PtrValue funcPtr, FunctionType* funcType,
                                std::vector<AnyValue> args,
                                std::string_view name = "",
                                CallingConv cc = CallingConv::CDecl) {
        std::vector<Value*> rawArgs;
        for (const auto& arg : args) {
            rawArgs.push_back(arg.raw());
        }
        auto* inst = createInst<IndirectCallInst>(funcPtr, funcType, std::move(rawArgs), name, cc);
        return AnyValue(inst);
    }

    // ========================================================================
    // PHI Nodes
    // ========================================================================

    TypedPHINode<IntValue>* createIntPHI(IntegerType* type, std::string_view name = "") {
        return createInst<TypedPHINode<IntValue>>(type, name);
    }

    TypedPHINode<FloatValue>* createFloatPHI(FloatType* type, std::string_view name = "") {
        return createInst<TypedPHINode<FloatValue>>(type, name);
    }

    TypedPHINode<PtrValue>* createPtrPHI(std::string_view name = "") {
        auto* ptrTy = module_.getContext().getPtrTy();
        return createInst<TypedPHINode<PtrValue>>(ptrTy, name);
    }

    // ========================================================================
    // Select (Ternary) Operations
    // ========================================================================

    IntValue createSelect(BoolValue condition, IntValue trueVal, IntValue falseVal,
                          std::string_view name = "") {
        auto* inst = createInst<TypedSelectInst<IntValue>>(condition, trueVal, falseVal, name);
        return inst->result();
    }

    FloatValue createSelect(BoolValue condition, FloatValue trueVal, FloatValue falseVal,
                            std::string_view name = "") {
        auto* inst = createInst<TypedSelectInst<FloatValue>>(condition, trueVal, falseVal, name);
        return inst->result();
    }

    PtrValue createSelect(BoolValue condition, PtrValue trueVal, PtrValue falseVal,
                          std::string_view name = "") {
        auto* inst = createInst<TypedSelectInst<PtrValue>>(condition, trueVal, falseVal, name);
        return inst->result();
    }

    // ========================================================================
    // String Literals
    // ========================================================================

    PtrValue createGlobalString(std::string_view value, std::string_view name = "") {
        GlobalVariable* gv = module_.getOrCreateStringLiteral(value);
        return gv->toTypedValue();
    }

    // Get pointer to first element of string
    PtrValue createGlobalStringPtr(std::string_view value, std::string_view name = "") {
        PtrValue strPtr = createGlobalString(value, name);
        return createInBoundsGEP(
            module_.getContext().getInt8Ty(),
            strPtr,
            {getInt32(0)},
            name
        );
    }

private:
    // Helper to create and insert an instruction
    template<typename InstType, typename... Args>
    InstType* createInst(Args&&... args) {
        auto inst = std::make_unique<InstType>(std::forward<Args>(args)...);
        InstType* ptr = inst.get();

        if (insertBlock_) {
            if (insertPoint_) {
                insertBlock_->insertBefore(inst.release(), insertPoint_);
            } else {
                insertBlock_->appendInstruction(inst.release());
            }
        } else {
            // Orphaned instruction - caller must manage
            inst.release();
        }

        return ptr;
    }

    Module& module_;
    BasicBlock* insertBlock_;
    Instruction* insertPoint_;

    // Instruction storage for orphaned instructions
    std::vector<std::unique_ptr<Instruction>> orphanedInstructions_;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
