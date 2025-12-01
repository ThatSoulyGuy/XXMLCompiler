#pragma once

#include "Backends/IR/Module.h"
#include <string>
#include <vector>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// IRBuilder - Type-safe instruction construction
// ============================================================================

class IRBuilder {
public:
    explicit IRBuilder(Module& module);
    IRBuilder(BasicBlock* insertBlock);

    // ========== Insertion Point ==========

    void setInsertPoint(BasicBlock* bb);
    void setInsertPoint(BasicBlock* bb, BasicBlock::iterator pos);
    void setInsertPoint(Instruction* inst);  // Insert before this instruction

    BasicBlock* getInsertBlock() const { return insertBlock_; }
    Module& getModule() { return module_; }
    TypeContext& getContext() { return module_.getContext(); }

    // ========== Function Context (for type validation) ==========

    void setCurrentFunction(Function* func) { currentFunction_ = func; }
    Function* getCurrentFunction() const { return currentFunction_; }

    // Type compatibility checking for return type validation
    bool typesCompatible(Type* expected, Type* actual);

    // ========== Type Shortcuts ==========

    Type* getVoidTy() { return getContext().getVoidTy(); }
    IntegerType* getInt1Ty() { return getContext().getInt1Ty(); }
    IntegerType* getInt8Ty() { return getContext().getInt8Ty(); }
    IntegerType* getInt32Ty() { return getContext().getInt32Ty(); }
    IntegerType* getInt64Ty() { return getContext().getInt64Ty(); }
    FloatType* getFloatTy() { return getContext().getFloatTy(); }
    FloatType* getDoubleTy() { return getContext().getDoubleTy(); }
    PointerType* getPtrTy() { return getContext().getPtrTy(); }

    // ========== Constant Creation ==========

    ConstantInt* getInt1(bool value);
    ConstantInt* getInt8(int8_t value);
    ConstantInt* getInt32(int32_t value);
    ConstantInt* getInt64(int64_t value);
    ConstantFP* getFloat(float value);
    ConstantFP* getDouble(double value);
    ConstantNull* getNullPtr();

    ConstantInt* getTrue() { return getInt1(true); }
    ConstantInt* getFalse() { return getInt1(false); }

    // ========== Register Naming ==========

    // Generate a unique name for a value
    std::string getUniqueName(const std::string& prefix = "");

    // ========== Memory Instructions ==========

    // alloca <type>
    AllocaInst* CreateAlloca(Type* type, const std::string& name = "");

    // alloca <type>, <numElements>
    AllocaInst* CreateAlloca(Type* type, Value* arraySize, const std::string& name = "");

    // load <type>, ptr <ptr>
    LoadInst* CreateLoad(Type* type, Value* ptr, const std::string& name = "");

    // store <type> <value>, ptr <ptr>
    StoreInst* CreateStore(Value* value, Value* ptr);

    // getelementptr inbounds <type>, ptr <ptr>, <indices>
    GetElementPtrInst* CreateGEP(Type* pointeeType, Value* ptr,
                                  std::vector<Value*> indices,
                                  const std::string& name = "");

    // getelementptr inbounds <type>, ptr <ptr>, i32 0, i32 <fieldIndex>
    GetElementPtrInst* CreateStructGEP(Type* pointeeType, Value* ptr,
                                        unsigned fieldIndex,
                                        const std::string& name = "");

    // getelementptr inbounds <type>, ptr <ptr>, i64 <index>
    GetElementPtrInst* CreateInBoundsGEP(Type* pointeeType, Value* ptr,
                                          Value* index,
                                          const std::string& name = "");

    // ========== Integer Arithmetic ==========

    Value* CreateAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateMul(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateSDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateUDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateSRem(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateURem(Value* lhs, Value* rhs, const std::string& name = "");

    // With NSW (no signed wrap) flag
    Value* CreateNSWAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateNSWSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateNSWMul(Value* lhs, Value* rhs, const std::string& name = "");

    // ========== Floating Point Arithmetic ==========

    Value* CreateFAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFMul(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFRem(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFNeg(Value* v, const std::string& name = "");

    // ========== Bitwise Operations ==========

    Value* CreateShl(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateLShr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateAShr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateAnd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateOr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateXor(Value* lhs, Value* rhs, const std::string& name = "");

    // NOT (xor with -1)
    Value* CreateNot(Value* v, const std::string& name = "");

    // ========== Integer Comparison ==========

    Value* CreateICmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpSLT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpSLE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpSGT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpSGE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpULT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpULE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpUGT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateICmpUGE(Value* lhs, Value* rhs, const std::string& name = "");

    // Generic integer comparison
    Value* CreateICmp(ICmpInst::Predicate pred, Value* lhs, Value* rhs,
                      const std::string& name = "");

    // ========== Floating Point Comparison ==========

    Value* CreateFCmpOEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpONE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpOLT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpOLE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpOGT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpOGE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpUEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* CreateFCmpUNE(Value* lhs, Value* rhs, const std::string& name = "");

    // Generic floating point comparison
    Value* CreateFCmp(FCmpInst::Predicate pred, Value* lhs, Value* rhs,
                      const std::string& name = "");

    // ========== Conversion ==========

    // Truncate integer to smaller type
    Value* CreateTrunc(Value* v, Type* destTy, const std::string& name = "");

    // Zero-extend integer to larger type
    Value* CreateZExt(Value* v, Type* destTy, const std::string& name = "");

    // Sign-extend integer to larger type
    Value* CreateSExt(Value* v, Type* destTy, const std::string& name = "");

    // Convert floating point to unsigned integer
    Value* CreateFPToUI(Value* v, Type* destTy, const std::string& name = "");

    // Convert floating point to signed integer
    Value* CreateFPToSI(Value* v, Type* destTy, const std::string& name = "");

    // Convert unsigned integer to floating point
    Value* CreateUIToFP(Value* v, Type* destTy, const std::string& name = "");

    // Convert signed integer to floating point
    Value* CreateSIToFP(Value* v, Type* destTy, const std::string& name = "");

    // Truncate floating point to smaller type
    Value* CreateFPTrunc(Value* v, Type* destTy, const std::string& name = "");

    // Extend floating point to larger type
    Value* CreateFPExt(Value* v, Type* destTy, const std::string& name = "");

    // Convert pointer to integer
    Value* CreatePtrToInt(Value* v, Type* destTy, const std::string& name = "");

    // Convert integer to pointer
    Value* CreateIntToPtr(Value* v, Type* destTy, const std::string& name = "");

    // Bitcast (reinterpret bits)
    Value* CreateBitCast(Value* v, Type* destTy, const std::string& name = "");

    // ========== Control Flow ==========

    // Unconditional branch
    BranchInst* CreateBr(BasicBlock* dest);

    // Conditional branch
    BranchInst* CreateCondBr(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse);

    // Switch
    SwitchInst* CreateSwitch(Value* v, BasicBlock* defaultDest,
                             unsigned numCases = 10);

    // Return void
    ReturnInst* CreateRetVoid();

    // Return value
    ReturnInst* CreateRet(Value* value);

    // Unreachable
    UnreachableInst* CreateUnreachable();

    // ========== PHI and Select ==========

    PHINode* CreatePHI(Type* type, unsigned numReservedValues,
                       const std::string& name = "");

    SelectInst* CreateSelect(Value* cond, Value* trueVal, Value* falseVal,
                             const std::string& name = "");

    // ========== Function Calls ==========

    // Call a function
    CallInst* CreateCall(Function* callee, std::vector<Value*> args = {},
                         const std::string& name = "");

    // Call with explicit function type (for indirect calls)
    CallInst* CreateCall(FunctionType* funcTy, Value* callee,
                         std::vector<Value*> args = {},
                         const std::string& name = "");

    // ========== Basic Block Creation ==========

    // Create a new basic block in the current function
    BasicBlock* CreateBasicBlock(const std::string& name = "");

    // ========== Destructor Management (absorbed from DestructorManager) ==========

    // Track an owned value that needs destruction
    void trackOwnedValue(Value* ptr, Type* type);

    // Generate destructor calls for all tracked values in scope
    void emitDestructors();

    // Push/pop scope for destructor tracking
    void pushDestructorScope();
    void popDestructorScope();

private:
    // Insert instruction at current insertion point
    template<typename T>
    T* insert(T* inst);

    // Helper to create binary operator
    BinaryOperator* createBinaryOp(Opcode op, Value* lhs, Value* rhs,
                                   const std::string& name);

    // Validate operand types
    void validateBinaryOpTypes(Value* lhs, Value* rhs);
    void validateComparisonTypes(Value* lhs, Value* rhs);

    Module& module_;
    BasicBlock* insertBlock_ = nullptr;
    BasicBlock::iterator insertPoint_;

    // Current function context for type validation
    Function* currentFunction_ = nullptr;

    // Register counter for naming
    unsigned registerCounter_ = 0;

    // Destructor tracking
    struct OwnedValue {
        Value* ptr;
        Type* type;
    };
    std::vector<std::vector<OwnedValue>> destructorScopes_;
};

} // namespace IR
} // namespace Backends
} // namespace XXML
