#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <cstdint>
#include <cassert>

namespace XXML {
namespace Backends {
namespace IR {

// Forward declarations
class Type;
class VoidType;
class IntegerType;
class FloatType;
class PointerType;
class ArrayType;
class StructType;
class FunctionType;
class LabelType;
class TypeContext;

// ============================================================================
// Arena Allocator for Types
// Types are long-lived and never individually deleted, making arena allocation ideal
// ============================================================================

class ArenaAllocator {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 4096;

    ArenaAllocator(size_t blockSize = DEFAULT_BLOCK_SIZE);
    ~ArenaAllocator();

    // Non-copyable, non-movable
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Allocate memory for an object of type T
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        void* ptr = allocateRaw(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    // Reset the arena (invalidates all allocated objects)
    void reset();

    // Statistics
    size_t getTotalAllocated() const { return totalAllocated_; }
    size_t getNumBlocks() const { return blocks_.size(); }

private:
    void* allocateRaw(size_t size, size_t alignment);
    void allocateNewBlock(size_t minSize);

    struct Block {
        std::unique_ptr<char[]> data;
        size_t size;
        size_t used;
    };

    std::vector<Block> blocks_;
    size_t blockSize_;
    size_t totalAllocated_ = 0;
};

// ============================================================================
// Type Base Class
// ============================================================================

class Type {
public:
    enum class Kind {
        Void,
        Integer,
        Float,
        Pointer,
        Array,
        Struct,
        Function,
        Label
    };

    virtual ~Type() = default;

    Kind getKind() const { return kind_; }
    virtual std::string toString() const = 0;
    virtual bool equals(const Type& other) const = 0;
    virtual size_t getSizeInBits() const = 0;
    virtual size_t getAlignmentInBytes() const = 0;

    // Type predicates
    bool isVoid() const { return kind_ == Kind::Void; }
    bool isInteger() const { return kind_ == Kind::Integer; }
    bool isFloatingPoint() const { return kind_ == Kind::Float; }
    bool isPointer() const { return kind_ == Kind::Pointer; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isStruct() const { return kind_ == Kind::Struct; }
    bool isFunction() const { return kind_ == Kind::Function; }
    bool isLabel() const { return kind_ == Kind::Label; }

    // First-class types can be values (excludes void, label, function)
    bool isFirstClass() const {
        return kind_ != Kind::Void && kind_ != Kind::Label && kind_ != Kind::Function;
    }

    // Aggregate types (array, struct)
    bool isAggregate() const {
        return kind_ == Kind::Array || kind_ == Kind::Struct;
    }

    // Can be used as a single value (not aggregate)
    bool isSingleValue() const {
        return isFirstClass() && !isAggregate();
    }

protected:
    explicit Type(Kind kind) : kind_(kind) {}

private:
    Kind kind_;
};

// ============================================================================
// Void Type (Singleton)
// ============================================================================

class VoidType : public Type {
public:
    std::string toString() const override { return "void"; }
    bool equals(const Type& other) const override { return other.isVoid(); }
    size_t getSizeInBits() const override { return 0; }
    size_t getAlignmentInBytes() const override { return 0; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Void; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    VoidType() : Type(Kind::Void) {}
};

// ============================================================================
// Integer Type (i1, i8, i16, i32, i64, i128)
// ============================================================================

class IntegerType : public Type {
public:
    unsigned getBitWidth() const { return bitWidth_; }

    std::string toString() const override {
        return "i" + std::to_string(bitWidth_);
    }

    bool equals(const Type& other) const override {
        if (!other.isInteger()) return false;
        return static_cast<const IntegerType&>(other).bitWidth_ == bitWidth_;
    }

    size_t getSizeInBits() const override { return bitWidth_; }
    size_t getAlignmentInBytes() const override {
        // Align to nearest power of 2, max 8 bytes
        if (bitWidth_ <= 8) return 1;
        if (bitWidth_ <= 16) return 2;
        if (bitWidth_ <= 32) return 4;
        return 8;
    }

    bool isBoolean() const { return bitWidth_ == 1; }
    bool isByte() const { return bitWidth_ == 8; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Integer; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    explicit IntegerType(unsigned bitWidth) : Type(Kind::Integer), bitWidth_(bitWidth) {}

    unsigned bitWidth_;
};

// ============================================================================
// Floating Point Type (float, double)
// ============================================================================

class FloatType : public Type {
public:
    enum class Precision {
        Half,    // 16-bit
        Float,   // 32-bit
        Double,  // 64-bit
        FP128    // 128-bit
    };

    Precision getPrecision() const { return precision_; }

    std::string toString() const override {
        switch (precision_) {
            case Precision::Half:   return "half";
            case Precision::Float:  return "float";
            case Precision::Double: return "double";
            case Precision::FP128:  return "fp128";
        }
        return "float";
    }

    bool equals(const Type& other) const override {
        if (!other.isFloatingPoint()) return false;
        return static_cast<const FloatType&>(other).precision_ == precision_;
    }

    size_t getSizeInBits() const override {
        switch (precision_) {
            case Precision::Half:   return 16;
            case Precision::Float:  return 32;
            case Precision::Double: return 64;
            case Precision::FP128:  return 128;
        }
        return 32;
    }

    size_t getAlignmentInBytes() const override {
        return getSizeInBits() / 8;
    }

    // Precision predicates
    bool isHalf() const { return precision_ == Precision::Half; }
    bool isFloat() const { return precision_ == Precision::Float; }
    bool isDouble() const { return precision_ == Precision::Double; }
    bool isFP128() const { return precision_ == Precision::FP128; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Float; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    explicit FloatType(Precision precision) : Type(Kind::Float), precision_(precision) {}

    Precision precision_;
};

// ============================================================================
// Pointer Type (opaque in LLVM 15+)
// ============================================================================

class PointerType : public Type {
public:
    unsigned getAddressSpace() const { return addressSpace_; }

    // Optional pointee type tracking (for diagnostics only, not part of IR)
    Type* getPointeeType() const { return pointeeType_; }
    void setPointeeType(Type* t) { pointeeType_ = t; }

    std::string toString() const override {
        if (addressSpace_ == 0) return "ptr";
        return "ptr addrspace(" + std::to_string(addressSpace_) + ")";
    }

    bool equals(const Type& other) const override {
        if (!other.isPointer()) return false;
        return static_cast<const PointerType&>(other).addressSpace_ == addressSpace_;
    }

    size_t getSizeInBits() const override { return 64; }  // 64-bit pointers
    size_t getAlignmentInBytes() const override { return 8; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Pointer; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    explicit PointerType(unsigned addressSpace = 0)
        : Type(Kind::Pointer), addressSpace_(addressSpace), pointeeType_(nullptr) {}

    unsigned addressSpace_;
    Type* pointeeType_;  // Optional, for diagnostics
};

// ============================================================================
// Array Type
// ============================================================================

class ArrayType : public Type {
public:
    Type* getElementType() const { return elementType_; }
    uint64_t getNumElements() const { return numElements_; }

    std::string toString() const override {
        return "[" + std::to_string(numElements_) + " x " + elementType_->toString() + "]";
    }

    bool equals(const Type& other) const override {
        if (!other.isArray()) return false;
        const auto& arr = static_cast<const ArrayType&>(other);
        return numElements_ == arr.numElements_ && elementType_->equals(*arr.elementType_);
    }

    size_t getSizeInBits() const override {
        return numElements_ * elementType_->getSizeInBits();
    }

    size_t getAlignmentInBytes() const override {
        return elementType_->getAlignmentInBytes();
    }

    static bool classof(const Type* t) { return t->getKind() == Kind::Array; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    ArrayType(Type* elementType, uint64_t numElements)
        : Type(Kind::Array), elementType_(elementType), numElements_(numElements) {}

    Type* elementType_;
    uint64_t numElements_;
};

// ============================================================================
// Struct Type
// ============================================================================

class StructType : public Type {
public:
    struct Field {
        std::string name;
        Type* type;
        size_t offset;  // Byte offset from struct start
    };

    const std::string& getName() const { return name_; }
    bool isNamed() const { return !name_.empty(); }
    bool isPacked() const { return isPacked_; }
    bool isOpaque() const { return fields_.empty() && !bodySet_; }

    const std::vector<Field>& getFields() const { return fields_; }
    size_t getNumFields() const { return fields_.size(); }
    size_t getNumElements() const { return fields_.size(); }  // LLVM-style alias
    bool hasName() const { return !name_.empty(); }  // LLVM-style alias for isNamed

    Type* getElementType(size_t index) const {
        return index < fields_.size() ? fields_[index].type : nullptr;
    }

    // Alias for getElementType (LLVM-style)
    Type* getFieldType(size_t index) const { return getElementType(index); }

    const Field* getField(size_t index) const {
        return index < fields_.size() ? &fields_[index] : nullptr;
    }

    std::optional<size_t> getFieldIndex(const std::string& name) const {
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].name == name) return i;
        }
        return std::nullopt;
    }

    // Set the body of the struct (can only be called once)
    void setBody(std::vector<Field> fields, bool isPacked = false) {
        assert(!bodySet_ && "Struct body already set");
        fields_ = std::move(fields);
        isPacked_ = isPacked;
        bodySet_ = true;

        // Calculate offsets
        size_t currentOffset = 0;
        for (auto& field : fields_) {
            if (!isPacked_) {
                size_t align = field.type->getAlignmentInBytes();
                currentOffset = (currentOffset + align - 1) & ~(align - 1);
            }
            field.offset = currentOffset;
            currentOffset += field.type->getSizeInBits() / 8;
        }
        totalSize_ = currentOffset;
    }

    // Simplified setBody with just types (no names)
    void setBody(std::vector<Type*> types, bool isPacked = false) {
        std::vector<Field> fields;
        fields.reserve(types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            fields.push_back({"", types[i], 0});
        }
        setBody(std::move(fields), isPacked);
    }

    std::string toString() const override {
        if (isNamed()) {
            return "%" + name_;
        }
        // Literal struct
        std::string result = isPacked_ ? "<{" : "{";
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (i > 0) result += ", ";
            result += fields_[i].type->toString();
        }
        result += isPacked_ ? "}>" : "}";
        return result;
    }

    bool equals(const Type& other) const override {
        if (!other.isStruct()) return false;
        const auto& s = static_cast<const StructType&>(other);
        if (isNamed() && s.isNamed()) {
            return name_ == s.name_;
        }
        // Structural equality for literal structs
        if (fields_.size() != s.fields_.size()) return false;
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (!fields_[i].type->equals(*s.fields_[i].type)) return false;
        }
        return isPacked_ == s.isPacked_;
    }

    size_t getSizeInBits() const override { return totalSize_ * 8; }
    size_t getAlignmentInBytes() const override {
        if (isPacked_ || fields_.empty()) return 1;
        size_t maxAlign = 1;
        for (const auto& field : fields_) {
            maxAlign = std::max(maxAlign, field.type->getAlignmentInBytes());
        }
        return maxAlign;
    }

    static bool classof(const Type* t) { return t->getKind() == Kind::Struct; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    explicit StructType(const std::string& name = "")
        : Type(Kind::Struct), name_(name), isPacked_(false), bodySet_(false), totalSize_(0) {}

    std::string name_;
    std::vector<Field> fields_;
    bool isPacked_;
    bool bodySet_;
    size_t totalSize_;
};

// ============================================================================
// Function Type
// ============================================================================

class FunctionType : public Type {
public:
    Type* getReturnType() const { return returnType_; }
    const std::vector<Type*>& getParamTypes() const { return paramTypes_; }
    size_t getNumParams() const { return paramTypes_.size(); }
    Type* getParamType(size_t index) const {
        return index < paramTypes_.size() ? paramTypes_[index] : nullptr;
    }
    bool isVarArg() const { return isVarArg_; }

    std::string toString() const override {
        std::string result = returnType_->toString() + " (";
        for (size_t i = 0; i < paramTypes_.size(); ++i) {
            if (i > 0) result += ", ";
            result += paramTypes_[i]->toString();
        }
        if (isVarArg_) {
            if (!paramTypes_.empty()) result += ", ";
            result += "...";
        }
        result += ")";
        return result;
    }

    bool equals(const Type& other) const override {
        if (!other.isFunction()) return false;
        const auto& f = static_cast<const FunctionType&>(other);
        if (!returnType_->equals(*f.returnType_)) return false;
        if (paramTypes_.size() != f.paramTypes_.size()) return false;
        for (size_t i = 0; i < paramTypes_.size(); ++i) {
            if (!paramTypes_[i]->equals(*f.paramTypes_[i])) return false;
        }
        return isVarArg_ == f.isVarArg_;
    }

    size_t getSizeInBits() const override { return 0; }  // Functions have no size
    size_t getAlignmentInBytes() const override { return 0; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Function; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    FunctionType(Type* returnType, std::vector<Type*> paramTypes, bool isVarArg)
        : Type(Kind::Function), returnType_(returnType),
          paramTypes_(std::move(paramTypes)), isVarArg_(isVarArg) {}

    Type* returnType_;
    std::vector<Type*> paramTypes_;
    bool isVarArg_;
};

// ============================================================================
// Label Type (for basic block references)
// ============================================================================

class LabelType : public Type {
public:
    std::string toString() const override { return "label"; }
    bool equals(const Type& other) const override { return other.isLabel(); }
    size_t getSizeInBits() const override { return 0; }
    size_t getAlignmentInBytes() const override { return 0; }

    static bool classof(const Type* t) { return t->getKind() == Kind::Label; }

private:
    friend class TypeContext;
    friend class ArenaAllocator;
    LabelType() : Type(Kind::Label) {}
};

// ============================================================================
// Type Context - Factory for all types with interning/caching
// Also absorbs NameMangler and TypeConverter functionality
// ============================================================================

class TypeContext {
public:
    TypeContext();
    ~TypeContext() = default;

    // Non-copyable
    TypeContext(const TypeContext&) = delete;
    TypeContext& operator=(const TypeContext&) = delete;

    // ========== Type Factory Methods ==========

    // Void type (singleton)
    VoidType* getVoidTy();

    // Integer types
    IntegerType* getInt1Ty();    // i1 (boolean)
    IntegerType* getInt8Ty();    // i8 (byte)
    IntegerType* getInt16Ty();   // i16
    IntegerType* getInt32Ty();   // i32
    IntegerType* getInt64Ty();   // i64
    IntegerType* getInt128Ty();  // i128
    IntegerType* getIntNTy(unsigned bitWidth);

    // Floating point types
    FloatType* getHalfTy();      // half (16-bit)
    FloatType* getFloatTy();     // float (32-bit)
    FloatType* getDoubleTy();    // double (64-bit)
    FloatType* getFP128Ty();     // fp128 (128-bit)

    // Pointer type (opaque)
    PointerType* getPtrTy(unsigned addressSpace = 0);

    // Array type
    ArrayType* getArrayTy(Type* elementType, uint64_t numElements);

    // Struct types
    StructType* createStructTy(const std::string& name);       // Named struct
    StructType* getStructTy(std::vector<Type*> elements, bool isPacked = false);  // Literal struct
    StructType* getNamedStructTy(const std::string& name) const;

    // Function type
    FunctionType* getFunctionTy(Type* returnType, std::vector<Type*> paramTypes, bool isVarArg = false);

    // Label type (singleton)
    LabelType* getLabelTy();

    // ========== XXML Type Conversion (absorbed from TypeConverter) ==========

    // Convert XXML type name to IR type
    Type* getTypeForXXML(const std::string& xxmlType);

    // Check if XXML type is primitive
    bool isPrimitiveXXML(const std::string& xxmlType) const;

    // Get representation type for parameter passing
    Type* getRepresentationType(const std::string& xxmlType);

    // ========== Name Mangling (absorbed from NameMangler) ==========

    // Mangle a class name for struct type: MyClass -> class.MyClass
    std::string mangleClassName(const std::string& className) const;

    // Mangle a method name: ClassName + methodName -> ClassName_methodName
    std::string mangleMethodName(const std::string& className, const std::string& methodName) const;

    // Mangle template instantiation: List<Integer> -> List_Integer
    std::string mangleTemplateName(const std::string& templateName, const std::vector<std::string>& typeArgs) const;

    // Sanitize identifier for LLVM (remove invalid characters)
    std::string sanitizeIdentifier(const std::string& name) const;

    // ========== Arena Statistics ==========

    size_t getArenaMemoryUsed() const { return arena_.getTotalAllocated(); }

private:
    ArenaAllocator arena_;

    // Cached singleton types
    VoidType* voidTy_ = nullptr;
    LabelType* labelTy_ = nullptr;

    // Cached integer types
    std::unordered_map<unsigned, IntegerType*> integerTypes_;

    // Cached float types
    std::unordered_map<FloatType::Precision, FloatType*> floatTypes_;

    // Cached pointer types (by address space)
    std::unordered_map<unsigned, PointerType*> pointerTypes_;

    // Named struct types
    std::unordered_map<std::string, StructType*> namedStructs_;

    // XXML type mapping cache
    std::unordered_map<std::string, Type*> xxmlTypeCache_;
};

} // namespace IR
} // namespace Backends
} // namespace XXML
