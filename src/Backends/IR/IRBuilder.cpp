#include "Backends/IR/IRBuilder.h"
#include <stdexcept>
#include <cassert>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// IRBuilder Implementation
// ============================================================================

IRBuilder::IRBuilder(Module& module) : module_(module) {
    destructorScopes_.push_back({});  // Global scope
}

IRBuilder::IRBuilder(BasicBlock* insertBlock)
    : module_(*insertBlock->getParent()->getParent()),
      insertBlock_(insertBlock) {
    insertPoint_ = insertBlock_->end();
    destructorScopes_.push_back({});
}

// ========== Insertion Point ==========

void IRBuilder::setInsertPoint(BasicBlock* bb) {
    insertBlock_ = bb;
    if (bb) {
        insertPoint_ = bb->end();
    }
}

void IRBuilder::setInsertPoint(BasicBlock* bb, BasicBlock::iterator pos) {
    insertBlock_ = bb;
    insertPoint_ = pos;
}

void IRBuilder::setInsertPoint(Instruction* inst) {
    assert(inst && inst->getParent() && "Instruction must be in a block");
    insertBlock_ = inst->getParent();
    // Find iterator for this instruction
    for (auto it = insertBlock_->begin(); it != insertBlock_->end(); ++it) {
        if (it->get() == inst) {
            insertPoint_ = it;
            return;
        }
    }
}

// ========== Constant Creation ==========

ConstantInt* IRBuilder::getInt1(bool value) {
    return ConstantInt::get(getContext(), getInt1Ty(), static_cast<uint64_t>(value ? 1 : 0));
}

ConstantInt* IRBuilder::getInt8(int8_t value) {
    return ConstantInt::get(getContext(), getInt8Ty(), static_cast<int64_t>(value));
}

ConstantInt* IRBuilder::getInt32(int32_t value) {
    return ConstantInt::get(getContext(), getInt32Ty(), static_cast<int64_t>(value));
}

ConstantInt* IRBuilder::getInt64(int64_t value) {
    return ConstantInt::get(getContext(), getInt64Ty(), value);
}

ConstantFP* IRBuilder::getFloat(float value) {
    return ConstantFP::get(getContext(), getFloatTy(), static_cast<double>(value));
}

ConstantFP* IRBuilder::getDouble(double value) {
    return ConstantFP::get(getContext(), getDoubleTy(), value);
}

ConstantNull* IRBuilder::getNullPtr() {
    return ConstantNull::get(getContext(), getPtrTy());
}

// ========== Register Naming ==========

std::string IRBuilder::getUniqueName(const std::string& prefix) {
    std::string name = prefix.empty() ? "" : prefix;
    return name + std::to_string(registerCounter_++);
}

// ========== Memory Instructions ==========

AllocaInst* IRBuilder::CreateAlloca(Type* type, const std::string& name) {
    auto* inst = new AllocaInst(type, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

AllocaInst* IRBuilder::CreateAlloca(Type* type, Value* arraySize, const std::string& name) {
    auto* inst = new AllocaInst(type, arraySize, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

LoadInst* IRBuilder::CreateLoad(Type* type, Value* ptr, const std::string& name) {
    assert(ptr->getType()->isPointer() && "Load requires pointer operand");
    auto* inst = new LoadInst(type, ptr, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

StoreInst* IRBuilder::CreateStore(Value* value, Value* ptr) {
    assert(ptr->getType()->isPointer() && "Store requires pointer operand");
    auto* inst = new StoreInst(value, ptr);
    return insert(inst);
}

GetElementPtrInst* IRBuilder::CreateGEP(Type* pointeeType, Value* ptr,
                                         std::vector<Value*> indices,
                                         const std::string& name) {
    assert(ptr->getType()->isPointer() && "GEP requires pointer operand");
    auto* inst = new GetElementPtrInst(pointeeType, ptr, std::move(indices),
                                       name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

GetElementPtrInst* IRBuilder::CreateStructGEP(Type* pointeeType, Value* ptr,
                                               unsigned fieldIndex,
                                               const std::string& name) {
    // getelementptr inbounds %type, ptr %ptr, i32 0, i32 fieldIndex
    std::vector<Value*> indices = {
        getInt32(0),
        getInt32(static_cast<int32_t>(fieldIndex))
    };
    return CreateGEP(pointeeType, ptr, std::move(indices), name);
}

GetElementPtrInst* IRBuilder::CreateInBoundsGEP(Type* pointeeType, Value* ptr,
                                                 Value* index,
                                                 const std::string& name) {
    std::vector<Value*> indices = { index };
    auto* inst = CreateGEP(pointeeType, ptr, std::move(indices), name);
    inst->setInBounds(true);
    return inst;
}

// ========== Integer Arithmetic ==========

BinaryOperator* IRBuilder::createBinaryOp(Opcode op, Value* lhs, Value* rhs,
                                           const std::string& name) {
    auto* inst = BinaryOperator::Create(op, lhs, rhs,
                                        name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

void IRBuilder::validateBinaryOpTypes(Value* lhs, Value* rhs) {
    assert(lhs->getType()->equals(*rhs->getType()) &&
           "Binary operation requires matching types");
}

Value* IRBuilder::CreateAdd(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Add, lhs, rhs, name);
}

Value* IRBuilder::CreateSub(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Sub, lhs, rhs, name);
}

Value* IRBuilder::CreateMul(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Mul, lhs, rhs, name);
}

Value* IRBuilder::CreateSDiv(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::SDiv, lhs, rhs, name);
}

Value* IRBuilder::CreateUDiv(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::UDiv, lhs, rhs, name);
}

Value* IRBuilder::CreateSRem(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::SRem, lhs, rhs, name);
}

Value* IRBuilder::CreateURem(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::URem, lhs, rhs, name);
}

Value* IRBuilder::CreateNSWAdd(Value* lhs, Value* rhs, const std::string& name) {
    auto* inst = static_cast<BinaryOperator*>(CreateAdd(lhs, rhs, name));
    inst->setNoSignedWrap(true);
    return inst;
}

Value* IRBuilder::CreateNSWSub(Value* lhs, Value* rhs, const std::string& name) {
    auto* inst = static_cast<BinaryOperator*>(CreateSub(lhs, rhs, name));
    inst->setNoSignedWrap(true);
    return inst;
}

Value* IRBuilder::CreateNSWMul(Value* lhs, Value* rhs, const std::string& name) {
    auto* inst = static_cast<BinaryOperator*>(CreateMul(lhs, rhs, name));
    inst->setNoSignedWrap(true);
    return inst;
}

// ========== Floating Point Arithmetic ==========

Value* IRBuilder::CreateFAdd(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::FAdd, lhs, rhs, name);
}

Value* IRBuilder::CreateFSub(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::FSub, lhs, rhs, name);
}

Value* IRBuilder::CreateFMul(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::FMul, lhs, rhs, name);
}

Value* IRBuilder::CreateFDiv(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::FDiv, lhs, rhs, name);
}

Value* IRBuilder::CreateFRem(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::FRem, lhs, rhs, name);
}

Value* IRBuilder::CreateFNeg(Value* v, const std::string& name) {
    // fneg is a unary operation, but we implement it as fsub 0.0, v
    // For proper fneg support, you'd need a UnaryOperator class
    return CreateFSub(ConstantFP::getZero(getContext(),
                      static_cast<FloatType*>(v->getType())), v, name);
}

// ========== Bitwise Operations ==========

Value* IRBuilder::CreateShl(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Shl, lhs, rhs, name);
}

Value* IRBuilder::CreateLShr(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::LShr, lhs, rhs, name);
}

Value* IRBuilder::CreateAShr(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::AShr, lhs, rhs, name);
}

Value* IRBuilder::CreateAnd(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::And, lhs, rhs, name);
}

Value* IRBuilder::CreateOr(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Or, lhs, rhs, name);
}

Value* IRBuilder::CreateXor(Value* lhs, Value* rhs, const std::string& name) {
    validateBinaryOpTypes(lhs, rhs);
    return createBinaryOp(Opcode::Xor, lhs, rhs, name);
}

Value* IRBuilder::CreateNot(Value* v, const std::string& name) {
    // NOT is XOR with -1
    auto* minusOne = ConstantInt::get(getContext(),
                                      static_cast<IntegerType*>(v->getType()), static_cast<int64_t>(-1));
    return CreateXor(v, minusOne, name);
}

// ========== Integer Comparison ==========

void IRBuilder::validateComparisonTypes(Value* lhs, Value* rhs) {
    assert(lhs->getType()->equals(*rhs->getType()) &&
           "Comparison requires matching types");
}

Value* IRBuilder::CreateICmp(ICmpInst::Predicate pred, Value* lhs, Value* rhs,
                              const std::string& name) {
    validateComparisonTypes(lhs, rhs);
    auto* inst = new ICmpInst(pred, lhs, rhs, name.empty() ? getUniqueName("") : name);
    inst->setType(getInt1Ty());  // ICmp produces i1 (boolean) result
    return insert(inst);
}

Value* IRBuilder::CreateICmpEQ(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::EQ, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpNE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::NE, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpSLT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::SLT, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpSLE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::SLE, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpSGT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::SGT, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpSGE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::SGE, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpULT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::ULT, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpULE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::ULE, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpUGT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::UGT, lhs, rhs, name);
}

Value* IRBuilder::CreateICmpUGE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateICmp(ICmpInst::Predicate::UGE, lhs, rhs, name);
}

// ========== Floating Point Comparison ==========

Value* IRBuilder::CreateFCmp(FCmpInst::Predicate pred, Value* lhs, Value* rhs,
                              const std::string& name) {
    validateComparisonTypes(lhs, rhs);
    auto* inst = new FCmpInst(pred, lhs, rhs, name.empty() ? getUniqueName("") : name);
    inst->setType(getInt1Ty());  // FCmp produces i1 (boolean) result
    return insert(inst);
}

Value* IRBuilder::CreateFCmpOEQ(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::OEQ, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpONE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::ONE, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpOLT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::OLT, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpOLE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::OLE, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpOGT(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::OGT, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpOGE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::OGE, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpUEQ(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::UEQ, lhs, rhs, name);
}

Value* IRBuilder::CreateFCmpUNE(Value* lhs, Value* rhs, const std::string& name) {
    return CreateFCmp(FCmpInst::Predicate::UNE, lhs, rhs, name);
}

// ========== Conversion ==========

Value* IRBuilder::CreateTrunc(Value* v, Type* destTy, const std::string& name) {
    assert(CastInst::castIsValid(Opcode::Trunc, v->getType(), destTy) &&
           "Invalid trunc conversion");
    auto* inst = new TruncInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateZExt(Value* v, Type* destTy, const std::string& name) {
    assert(CastInst::castIsValid(Opcode::ZExt, v->getType(), destTy) &&
           "Invalid zext conversion");
    auto* inst = new ZExtInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateSExt(Value* v, Type* destTy, const std::string& name) {
    assert(CastInst::castIsValid(Opcode::SExt, v->getType(), destTy) &&
           "Invalid sext conversion");
    auto* inst = new SExtInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateFPToUI(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::FPToUI, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateFPToSI(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::FPToSI, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateUIToFP(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::UIToFP, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateSIToFP(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::SIToFP, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateFPTrunc(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::FPTrunc, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateFPExt(Value* v, Type* destTy, const std::string& name) {
    auto* inst = CastInst::Create(Opcode::FPExt, v, destTy,
                                  name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreatePtrToInt(Value* v, Type* destTy, const std::string& name) {
    assert(v->getType()->isPointer() && "PtrToInt requires pointer operand");
    assert(destTy->isInteger() && "PtrToInt requires integer destination");
    auto* inst = new PtrToIntInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateIntToPtr(Value* v, Type* destTy, const std::string& name) {
    assert(v->getType()->isInteger() && "IntToPtr requires integer operand");
    assert(destTy->isPointer() && "IntToPtr requires pointer destination");
    auto* inst = new IntToPtrInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

Value* IRBuilder::CreateBitCast(Value* v, Type* destTy, const std::string& name) {
    auto* inst = new BitCastInst(v, destTy, name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

// ========== Control Flow ==========

BranchInst* IRBuilder::CreateBr(BasicBlock* dest) {
    return insert(BranchInst::Create(dest));
}

BranchInst* IRBuilder::CreateCondBr(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse) {
    assert(cond->getType()->isInteger() &&
           static_cast<IntegerType*>(cond->getType())->getBitWidth() == 1 &&
           "Conditional branch requires i1 condition");
    return insert(BranchInst::Create(cond, ifTrue, ifFalse));
}

SwitchInst* IRBuilder::CreateSwitch(Value* v, BasicBlock* defaultDest, unsigned numCases) {
    return insert(new SwitchInst(v, defaultDest));
}

ReturnInst* IRBuilder::CreateRetVoid() {
    return insert(ReturnInst::Create());
}

ReturnInst* IRBuilder::CreateRet(Value* value) {
    return insert(ReturnInst::Create(value));
}

UnreachableInst* IRBuilder::CreateUnreachable() {
    return insert(UnreachableInst::Create());
}

// ========== PHI and Select ==========

PHINode* IRBuilder::CreatePHI(Type* type, unsigned numReservedValues,
                               const std::string& name) {
    auto* inst = new PHINode(type, numReservedValues,
                             name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

SelectInst* IRBuilder::CreateSelect(Value* cond, Value* trueVal, Value* falseVal,
                                     const std::string& name) {
    assert(cond->getType()->isInteger() &&
           static_cast<IntegerType*>(cond->getType())->getBitWidth() == 1 &&
           "Select requires i1 condition");
    assert(trueVal->getType()->equals(*falseVal->getType()) &&
           "Select requires matching value types");
    auto* inst = new SelectInst(cond, trueVal, falseVal,
                                name.empty() ? getUniqueName("") : name);
    return insert(inst);
}

// ========== Function Calls ==========

CallInst* IRBuilder::CreateCall(Function* callee, std::vector<Value*> args,
                                 const std::string& name) {
    return CreateCall(callee->getFunctionType(),
                      static_cast<Value*>(callee), std::move(args), name);
}

CallInst* IRBuilder::CreateCall(FunctionType* funcTy, Value* callee,
                                 std::vector<Value*> args,
                                 const std::string& name) {
    std::string resultName = funcTy->getReturnType()->isVoid() ? "" :
                             (name.empty() ? getUniqueName("") : name);
    auto* inst = new CallInst(funcTy, callee, std::move(args), resultName);
    return insert(inst);
}

// ========== Basic Block Creation ==========

BasicBlock* IRBuilder::CreateBasicBlock(const std::string& name) {
    if (insertBlock_ && insertBlock_->getParent()) {
        return insertBlock_->getParent()->createBasicBlock(name);
    }
    return new BasicBlock(name);
}

// ========== Destructor Management ==========

void IRBuilder::trackOwnedValue(Value* ptr, Type* type) {
    if (!destructorScopes_.empty()) {
        destructorScopes_.back().push_back({ptr, type});
    }
}

void IRBuilder::emitDestructors() {
    // Would generate destructor calls for tracked values
    // Implementation depends on how destructors are represented in XXML
}

void IRBuilder::pushDestructorScope() {
    destructorScopes_.push_back({});
}

void IRBuilder::popDestructorScope() {
    if (destructorScopes_.size() > 1) {
        emitDestructors();
        destructorScopes_.pop_back();
    }
}

// ========== Private Helpers ==========

template<typename T>
T* IRBuilder::insert(T* inst) {
    if (insertBlock_) {
        if (insertPoint_ != insertBlock_->end()) {
            insertBlock_->insert(insertPoint_, std::unique_ptr<Instruction>(inst));
        } else {
            insertBlock_->push_back(std::unique_ptr<Instruction>(inst));
        }
    }
    return inst;
}

} // namespace IR
} // namespace Backends
} // namespace XXML
