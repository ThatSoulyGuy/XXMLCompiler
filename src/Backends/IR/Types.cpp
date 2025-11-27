#include "Backends/IR/Types.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Arena Allocator Implementation
// ============================================================================

ArenaAllocator::ArenaAllocator(size_t blockSize) : blockSize_(blockSize) {
    allocateNewBlock(blockSize);
}

ArenaAllocator::~ArenaAllocator() {
    // unique_ptr in Block handles cleanup
}

void* ArenaAllocator::allocateRaw(size_t size, size_t alignment) {
    if (blocks_.empty()) {
        allocateNewBlock(std::max(blockSize_, size + alignment));
    }

    Block& current = blocks_.back();

    // Align the current position
    size_t alignedPos = (current.used + alignment - 1) & ~(alignment - 1);

    // Check if we need a new block
    if (alignedPos + size > current.size) {
        allocateNewBlock(std::max(blockSize_, size + alignment));
        Block& newBlock = blocks_.back();
        alignedPos = (newBlock.used + alignment - 1) & ~(alignment - 1);
    }

    Block& targetBlock = blocks_.back();
    void* ptr = targetBlock.data.get() + alignedPos;
    targetBlock.used = alignedPos + size;
    totalAllocated_ += size;

    return ptr;
}

void ArenaAllocator::allocateNewBlock(size_t minSize) {
    size_t blockSize = std::max(blockSize_, minSize);
    Block block;
    block.data = std::make_unique<char[]>(blockSize);
    block.size = blockSize;
    block.used = 0;
    blocks_.push_back(std::move(block));
}

void ArenaAllocator::reset() {
    blocks_.clear();
    totalAllocated_ = 0;
    allocateNewBlock(blockSize_);
}

// ============================================================================
// Type Context Implementation
// ============================================================================

TypeContext::TypeContext() : arena_(8192) {
    // Pre-create common types
    voidTy_ = arena_.allocate<VoidType>();
    labelTy_ = arena_.allocate<LabelType>();

    // Pre-create common integer types
    integerTypes_[1] = arena_.allocate<IntegerType>(1);
    integerTypes_[8] = arena_.allocate<IntegerType>(8);
    integerTypes_[16] = arena_.allocate<IntegerType>(16);
    integerTypes_[32] = arena_.allocate<IntegerType>(32);
    integerTypes_[64] = arena_.allocate<IntegerType>(64);

    // Pre-create common float types
    floatTypes_[FloatType::Precision::Float] = arena_.allocate<FloatType>(FloatType::Precision::Float);
    floatTypes_[FloatType::Precision::Double] = arena_.allocate<FloatType>(FloatType::Precision::Double);

    // Pre-create default pointer type (address space 0)
    pointerTypes_[0] = arena_.allocate<PointerType>(0);
}

// ========== Type Factory Methods ==========

VoidType* TypeContext::getVoidTy() {
    return voidTy_;
}

IntegerType* TypeContext::getInt1Ty() {
    return integerTypes_[1];
}

IntegerType* TypeContext::getInt8Ty() {
    return integerTypes_[8];
}

IntegerType* TypeContext::getInt16Ty() {
    return integerTypes_[16];
}

IntegerType* TypeContext::getInt32Ty() {
    return integerTypes_[32];
}

IntegerType* TypeContext::getInt64Ty() {
    return integerTypes_[64];
}

IntegerType* TypeContext::getInt128Ty() {
    auto it = integerTypes_.find(128);
    if (it != integerTypes_.end()) {
        return it->second;
    }
    auto* ty = arena_.allocate<IntegerType>(128);
    integerTypes_[128] = ty;
    return ty;
}

IntegerType* TypeContext::getIntNTy(unsigned bitWidth) {
    auto it = integerTypes_.find(bitWidth);
    if (it != integerTypes_.end()) {
        return it->second;
    }
    auto* ty = arena_.allocate<IntegerType>(bitWidth);
    integerTypes_[bitWidth] = ty;
    return ty;
}

FloatType* TypeContext::getHalfTy() {
    auto it = floatTypes_.find(FloatType::Precision::Half);
    if (it != floatTypes_.end()) {
        return it->second;
    }
    auto* ty = arena_.allocate<FloatType>(FloatType::Precision::Half);
    floatTypes_[FloatType::Precision::Half] = ty;
    return ty;
}

FloatType* TypeContext::getFloatTy() {
    return floatTypes_[FloatType::Precision::Float];
}

FloatType* TypeContext::getDoubleTy() {
    return floatTypes_[FloatType::Precision::Double];
}

FloatType* TypeContext::getFP128Ty() {
    auto it = floatTypes_.find(FloatType::Precision::FP128);
    if (it != floatTypes_.end()) {
        return it->second;
    }
    auto* ty = arena_.allocate<FloatType>(FloatType::Precision::FP128);
    floatTypes_[FloatType::Precision::FP128] = ty;
    return ty;
}

PointerType* TypeContext::getPtrTy(unsigned addressSpace) {
    auto it = pointerTypes_.find(addressSpace);
    if (it != pointerTypes_.end()) {
        return it->second;
    }
    auto* ty = arena_.allocate<PointerType>(addressSpace);
    pointerTypes_[addressSpace] = ty;
    return ty;
}

ArrayType* TypeContext::getArrayTy(Type* elementType, uint64_t numElements) {
    // Note: Array types are not cached for simplicity
    // In a production compiler, you'd want to cache these too
    return arena_.allocate<ArrayType>(elementType, numElements);
}

StructType* TypeContext::createStructTy(const std::string& name) {
    // Check if already exists
    auto it = namedStructs_.find(name);
    if (it != namedStructs_.end()) {
        return it->second;
    }

    auto* ty = arena_.allocate<StructType>(name);
    namedStructs_[name] = ty;
    return ty;
}

StructType* TypeContext::getStructTy(std::vector<Type*> elements, bool isPacked) {
    // Create a literal (unnamed) struct
    auto* ty = arena_.allocate<StructType>("");
    ty->setBody(std::move(elements), isPacked);
    return ty;
}

StructType* TypeContext::getNamedStructTy(const std::string& name) const {
    auto it = namedStructs_.find(name);
    return it != namedStructs_.end() ? it->second : nullptr;
}

FunctionType* TypeContext::getFunctionTy(Type* returnType, std::vector<Type*> paramTypes, bool isVarArg) {
    // Note: Function types are not cached for simplicity
    return arena_.allocate<FunctionType>(returnType, std::move(paramTypes), isVarArg);
}

LabelType* TypeContext::getLabelTy() {
    return labelTy_;
}

// ========== XXML Type Conversion ==========

Type* TypeContext::getTypeForXXML(const std::string& xxmlType) {
    // Check cache first
    auto it = xxmlTypeCache_.find(xxmlType);
    if (it != xxmlTypeCache_.end()) {
        return it->second;
    }

    Type* result = nullptr;

    // Map XXML types to LLVM IR types
    if (xxmlType == "None" || xxmlType == "Void") {
        result = getVoidTy();
    }
    else if (xxmlType == "Bool") {
        result = getInt1Ty();
    }
    else if (xxmlType == "Integer") {
        result = getInt64Ty();
    }
    else if (xxmlType == "Float") {
        result = getFloatTy();
    }
    else if (xxmlType == "Double") {
        result = getDoubleTy();
    }
    else if (xxmlType == "String") {
        result = getPtrTy();
    }
    // Handle NativeType<"...">
    else if (xxmlType.find("NativeType") == 0) {
        // Extract the native type from NativeType<"..."> or NativeType_...
        std::string nativeType;

        // Try NativeType<"..."> format
        size_t start = xxmlType.find("<\"");
        if (start != std::string::npos) {
            size_t end = xxmlType.find("\">", start);
            if (end != std::string::npos) {
                nativeType = xxmlType.substr(start + 2, end - start - 2);
            }
        }

        // Try NativeType_... format
        if (nativeType.empty()) {
            size_t underscore = xxmlType.find('_');
            if (underscore != std::string::npos) {
                nativeType = xxmlType.substr(underscore + 1);
            }
        }

        // Map native type strings
        if (nativeType == "ptr" || nativeType == "cstr") {
            result = getPtrTy();
        }
        else if (nativeType == "int64" || nativeType == "i64") {
            result = getInt64Ty();
        }
        else if (nativeType == "int32" || nativeType == "i32") {
            result = getInt32Ty();
        }
        else if (nativeType == "int8" || nativeType == "i8") {
            result = getInt8Ty();
        }
        else if (nativeType == "bool" || nativeType == "i1") {
            result = getInt1Ty();
        }
        else if (nativeType == "float") {
            result = getFloatTy();
        }
        else if (nativeType == "double") {
            result = getDoubleTy();
        }
        else {
            // Default to pointer for unknown native types
            result = getPtrTy();
        }
    }
    else {
        // All other types (classes, templates) are pointers
        result = getPtrTy();
    }

    // Cache the result
    xxmlTypeCache_[xxmlType] = result;
    return result;
}

bool TypeContext::isPrimitiveXXML(const std::string& xxmlType) const {
    return xxmlType == "None" || xxmlType == "Bool" ||
           xxmlType == "Integer" || xxmlType == "Float" ||
           xxmlType == "Double";
}

Type* TypeContext::getRepresentationType(const std::string& xxmlType) {
    // Primitives (except None) are passed by value
    if (xxmlType == "Bool") return getInt1Ty();
    if (xxmlType == "Integer") return getInt64Ty();
    if (xxmlType == "Float") return getFloatTy();
    if (xxmlType == "Double") return getDoubleTy();

    // Everything else (including String and all classes) is a pointer
    return getPtrTy();
}

// ========== Name Mangling ==========

std::string TypeContext::mangleClassName(const std::string& className) const {
    return "class." + sanitizeIdentifier(className);
}

std::string TypeContext::mangleMethodName(const std::string& className, const std::string& methodName) const {
    return sanitizeIdentifier(className) + "_" + sanitizeIdentifier(methodName);
}

std::string TypeContext::mangleTemplateName(const std::string& templateName, const std::vector<std::string>& typeArgs) const {
    std::string result = sanitizeIdentifier(templateName);
    for (const auto& arg : typeArgs) {
        result += "_" + sanitizeIdentifier(arg);
    }
    return result;
}

std::string TypeContext::sanitizeIdentifier(const std::string& name) const {
    std::string result;
    result.reserve(name.length());

    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            result += c;
        }
        else if (c == ':') {
            result += '_';
        }
        else if (c == '<' || c == '>' || c == ',' || c == ' ') {
            result += '_';
        }
        // Skip other characters
    }

    // Remove consecutive underscores
    std::string cleaned;
    cleaned.reserve(result.length());
    bool lastWasUnderscore = false;
    for (char c : result) {
        if (c == '_') {
            if (!lastWasUnderscore) {
                cleaned += c;
                lastWasUnderscore = true;
            }
        } else {
            cleaned += c;
            lastWasUnderscore = false;
        }
    }

    // Remove trailing underscore
    if (!cleaned.empty() && cleaned.back() == '_') {
        cleaned.pop_back();
    }

    return cleaned;
}

} // namespace IR
} // namespace Backends
} // namespace XXML
