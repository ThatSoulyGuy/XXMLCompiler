#include "Backends/LLVMIR/TypedModule.h"
#include <algorithm>
#include <stdexcept>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// StructType Implementation
// ============================================================================

void StructType::setBody(std::vector<Type*> types, bool isPacked) {
    isPacked_ = isPacked;
    hasBody_ = true;
    fields_.clear();

    size_t offset = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        Type* fieldType = types[i];

        // Apply alignment unless packed
        if (!isPacked_) {
            size_t align = fieldType->getAlignmentInBytes();
            offset = (offset + align - 1) & ~(align - 1);
        }

        fields_.push_back(Field{
            "field" + std::to_string(i),
            fieldType,
            offset
        });

        offset += fieldType->getSizeInBits() / 8;
    }

    // Calculate total size with alignment padding
    if (!isPacked_ && !fields_.empty()) {
        size_t structAlign = getAlignmentInBytes();
        offset = (offset + structAlign - 1) & ~(structAlign - 1);
    }

    totalSize_ = offset;
}

// ============================================================================
// BasicBlock Implementation
// ============================================================================

void BasicBlock::appendInstruction(Instruction* inst) {
    if (!inst) return;

    inst->setNext(nullptr);
    inst->setPrev(last_);

    if (last_) {
        last_->setNext(inst);
    } else {
        first_ = inst;
    }
    last_ = inst;
}

void BasicBlock::insertBefore(Instruction* inst, Instruction* before) {
    if (!inst) return;

    if (!before) {
        // Insert at end
        appendInstruction(inst);
        return;
    }

    Instruction* prev = before->getPrev();

    inst->setNext(before);
    inst->setPrev(prev);
    before->setPrev(inst);

    if (prev) {
        prev->setNext(inst);
    } else {
        first_ = inst;
    }
}

TerminatorInst* BasicBlock::getTerminator() const {
    if (!last_) return nullptr;

    if (last_->isTerminator()) {
        return static_cast<TerminatorInst*>(last_);
    }
    return nullptr;
}

size_t BasicBlock::size() const {
    size_t count = 0;
    for (Instruction* inst = first_; inst; inst = inst->getNext()) {
        ++count;
    }
    return count;
}

// ============================================================================
// Function Implementation
// ============================================================================

BasicBlock* Function::createBasicBlock(std::string_view name) {
    auto bb = std::make_unique<BasicBlock>(name);
    BasicBlock* ptr = bb.get();
    ptr->setParent(this);
    blocks_.push_back(std::move(bb));
    return ptr;
}

// ============================================================================
// TypeContext Implementation
// ============================================================================

TypeContext::TypeContext() {
    // Pre-create common types
    voidTy_ = std::make_unique<VoidType>();
}

VoidType* TypeContext::getVoidTy() {
    return voidTy_.get();
}

IntegerType* TypeContext::getInt1Ty() {
    return getIntNTy(1);
}

IntegerType* TypeContext::getInt8Ty() {
    return getIntNTy(8);
}

IntegerType* TypeContext::getInt16Ty() {
    return getIntNTy(16);
}

IntegerType* TypeContext::getInt32Ty() {
    return getIntNTy(32);
}

IntegerType* TypeContext::getInt64Ty() {
    return getIntNTy(64);
}

IntegerType* TypeContext::getIntNTy(unsigned bitWidth) {
    auto it = intTypes_.find(bitWidth);
    if (it != intTypes_.end()) {
        return it->second.get();
    }

    auto ty = std::make_unique<IntegerType>(bitWidth);
    IntegerType* ptr = ty.get();
    intTypes_[bitWidth] = std::move(ty);
    return ptr;
}

FloatType* TypeContext::getFloatTy() {
    auto it = floatTypes_.find(FloatType::Precision::Float);
    if (it != floatTypes_.end()) {
        return it->second.get();
    }

    auto ty = std::make_unique<FloatType>(FloatType::Precision::Float);
    FloatType* ptr = ty.get();
    floatTypes_[FloatType::Precision::Float] = std::move(ty);
    return ptr;
}

FloatType* TypeContext::getDoubleTy() {
    auto it = floatTypes_.find(FloatType::Precision::Double);
    if (it != floatTypes_.end()) {
        return it->second.get();
    }

    auto ty = std::make_unique<FloatType>(FloatType::Precision::Double);
    FloatType* ptr = ty.get();
    floatTypes_[FloatType::Precision::Double] = std::move(ty);
    return ptr;
}

PointerType* TypeContext::getPtrTy(unsigned addressSpace) {
    auto it = ptrTypes_.find(addressSpace);
    if (it != ptrTypes_.end()) {
        return it->second.get();
    }

    auto ty = std::make_unique<PointerType>(addressSpace);
    PointerType* ptr = ty.get();
    ptrTypes_[addressSpace] = std::move(ty);
    return ptr;
}

ArrayType* TypeContext::getArrayTy(Type* elementType, size_t numElements) {
    // Check if we already have this exact array type
    for (const auto& existing : arrayTypes_) {
        if (existing->getElementType() == elementType &&
            existing->getNumElements() == numElements) {
            return existing.get();
        }
    }

    auto ty = std::make_unique<ArrayType>(elementType, numElements);
    ArrayType* ptr = ty.get();
    arrayTypes_.push_back(std::move(ty));
    return ptr;
}

StructType* TypeContext::createStructTy(std::string_view name) {
    std::string nameStr(name);

    // Check if already exists
    auto it = namedStructs_.find(nameStr);
    if (it != namedStructs_.end()) {
        return it->second.get();
    }

    auto ty = std::make_unique<StructType>(name);
    StructType* ptr = ty.get();
    namedStructs_[nameStr] = std::move(ty);
    return ptr;
}

StructType* TypeContext::getNamedStructTy(std::string_view name) const {
    auto it = namedStructs_.find(std::string(name));
    if (it != namedStructs_.end()) {
        return it->second.get();
    }
    return nullptr;
}

FunctionType* TypeContext::getFunctionTy(Type* returnType, std::vector<Type*> paramTypes,
                                          bool isVarArg) {
    // Check if we already have this exact function type
    for (const auto& existing : funcTypes_) {
        if (existing->getReturnType() == returnType &&
            existing->isVarArg() == isVarArg &&
            existing->getNumParams() == paramTypes.size()) {
            bool match = true;
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (existing->getParamType(i) != paramTypes[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return existing.get();
            }
        }
    }

    auto ty = std::make_unique<FunctionType>(returnType, std::move(paramTypes), isVarArg);
    FunctionType* ptr = ty.get();
    funcTypes_.push_back(std::move(ty));
    return ptr;
}

// ============================================================================
// Module Implementation
// ============================================================================

Module::Module(std::string_view name) : name_(name) {
    // Set default target triple and data layout for x86-64 Windows
    targetTriple_ = "x86_64-pc-windows-msvc";
    dataLayout_ = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
}

StructType* Module::createStruct(std::string_view name) {
    StructType* ty = context_.createStructTy(name);
    structs_[std::string(name)] = ty;
    return ty;
}

StructType* Module::getStruct(std::string_view name) const {
    auto it = structs_.find(std::string(name));
    if (it != structs_.end()) {
        return it->second;
    }
    return nullptr;
}

GlobalVariable* Module::createGlobal(Type* valueType, std::string_view name,
                                      GlobalVariable::Linkage linkage,
                                      Constant* initializer) {
    std::string nameStr(name);

    // Check if already exists
    auto it = globals_.find(nameStr);
    if (it != globals_.end()) {
        return it->second.get();
    }

    PointerType* ptrType = context_.getPtrTy();
    auto gv = std::make_unique<GlobalVariable>(valueType, ptrType, name, linkage, initializer);
    GlobalVariable* ptr = gv.get();
    globals_[nameStr] = std::move(gv);
    return ptr;
}

GlobalVariable* Module::getGlobal(std::string_view name) const {
    auto it = globals_.find(std::string(name));
    if (it != globals_.end()) {
        return it->second.get();
    }
    return nullptr;
}

Function* Module::createFunction(FunctionType* funcType, std::string_view name,
                                  Function::Linkage linkage) {
    std::string nameStr(name);

    // Check if already exists
    auto it = functions_.find(nameStr);
    if (it != functions_.end()) {
        return it->second.get();
    }

    PointerType* ptrType = context_.getPtrTy();
    auto func = std::make_unique<Function>(funcType, ptrType, name, linkage);
    Function* ptr = func.get();
    functions_[nameStr] = std::move(func);
    return ptr;
}

Function* Module::getFunction(std::string_view name) const {
    auto it = functions_.find(std::string(name));
    if (it != functions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

GlobalVariable* Module::getOrCreateStringLiteral(std::string_view value) {
    std::string valueStr(value);

    // Check if already exists
    auto it = stringLiterals_.find(valueStr);
    if (it != stringLiterals_.end()) {
        return it->second;
    }

    // Create array type for the string (including null terminator)
    ArrayType* arrayType = context_.getArrayTy(context_.getInt8Ty(), value.size() + 1);

    // Create the constant string
    auto constStr = std::make_unique<ConstantString>(arrayType, valueStr);

    // Create global variable
    std::string globalName = ".str." + std::to_string(stringCounter_++);
    GlobalVariable* gv = createGlobal(arrayType, globalName,
                                       GlobalVariable::Linkage::Private,
                                       constStr.get());
    gv->setConstant(true);

    constantStrings_.push_back(std::move(constStr));
    stringLiterals_[valueStr] = gv;

    return gv;
}

ConstantInt* Module::getConstantInt(IntegerType* type, int64_t value) {
    // For now, always create new constants
    // Could optimize with interning for common values
    auto ci = std::make_unique<ConstantInt>(type, value);
    ConstantInt* ptr = ci.get();
    constantInts_.push_back(std::move(ci));
    return ptr;
}

ConstantFP* Module::getConstantFP(FloatType* type, double value) {
    auto cf = std::make_unique<ConstantFP>(type, value);
    ConstantFP* ptr = cf.get();
    constantFPs_.push_back(std::move(cf));
    return ptr;
}

ConstantNull* Module::getConstantNull(PointerType* type) {
    auto cn = std::make_unique<ConstantNull>(type);
    ConstantNull* ptr = cn.get();
    constantNulls_.push_back(std::move(cn));
    return ptr;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
