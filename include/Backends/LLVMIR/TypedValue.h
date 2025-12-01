#pragma once

#include <cstdint>
#include <cassert>
#include <variant>
#include <optional>
#include <string>
#include <string_view>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// Forward declarations
class Type;
class IntegerType;
class FloatType;
class PointerType;
class VoidType;
class ArrayType;
class StructType;
class FunctionType;
class Value;
class BasicBlock;
class Function;
class Instruction;

// ============================================================================
// Type Enumeration
// ============================================================================

enum class TypeKind {
    Void,
    Integer,
    Float,
    Pointer,
    Array,
    Struct,
    Function,
    Label
};

// ============================================================================
// Type Base Class
// ============================================================================

class Type {
public:
    virtual ~Type() = default;

    TypeKind getKind() const { return kind_; }

    bool isVoid() const { return kind_ == TypeKind::Void; }
    bool isInteger() const { return kind_ == TypeKind::Integer; }
    bool isFloat() const { return kind_ == TypeKind::Float; }
    bool isPointer() const { return kind_ == TypeKind::Pointer; }
    bool isArray() const { return kind_ == TypeKind::Array; }
    bool isStruct() const { return kind_ == TypeKind::Struct; }
    bool isFunction() const { return kind_ == TypeKind::Function; }

    // Size and alignment
    virtual size_t getSizeInBits() const = 0;
    virtual size_t getAlignmentInBytes() const = 0;

    // LLVM IR string representation
    virtual std::string toLLVM() const = 0;

protected:
    explicit Type(TypeKind kind) : kind_(kind) {}

private:
    TypeKind kind_;
};

// ============================================================================
// Concrete Type Classes
// ============================================================================

class VoidType final : public Type {
public:
    VoidType() : Type(TypeKind::Void) {}

    size_t getSizeInBits() const override { return 0; }
    size_t getAlignmentInBytes() const override { return 0; }
    std::string toLLVM() const override { return "void"; }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Void; }
};

class IntegerType final : public Type {
public:
    explicit IntegerType(unsigned bitWidth)
        : Type(TypeKind::Integer), bitWidth_(bitWidth) {}

    unsigned getBitWidth() const { return bitWidth_; }
    bool isI1() const { return bitWidth_ == 1; }
    bool isI8() const { return bitWidth_ == 8; }
    bool isI16() const { return bitWidth_ == 16; }
    bool isI32() const { return bitWidth_ == 32; }
    bool isI64() const { return bitWidth_ == 64; }

    size_t getSizeInBits() const override { return bitWidth_; }
    size_t getAlignmentInBytes() const override {
        if (bitWidth_ <= 8) return 1;
        if (bitWidth_ <= 16) return 2;
        if (bitWidth_ <= 32) return 4;
        return 8;
    }
    std::string toLLVM() const override { return "i" + std::to_string(bitWidth_); }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Integer; }

private:
    unsigned bitWidth_;
};

class FloatType final : public Type {
public:
    enum class Precision { Half, Float, Double, FP128 };

    explicit FloatType(Precision prec) : Type(TypeKind::Float), precision_(prec) {}

    Precision getPrecision() const { return precision_; }
    bool isFloat() const { return precision_ == Precision::Float; }
    bool isDouble() const { return precision_ == Precision::Double; }

    size_t getSizeInBits() const override {
        switch (precision_) {
            case Precision::Half: return 16;
            case Precision::Float: return 32;
            case Precision::Double: return 64;
            case Precision::FP128: return 128;
        }
        return 32;
    }
    size_t getAlignmentInBytes() const override { return getSizeInBits() / 8; }
    std::string toLLVM() const override {
        switch (precision_) {
            case Precision::Half: return "half";
            case Precision::Float: return "float";
            case Precision::Double: return "double";
            case Precision::FP128: return "fp128";
        }
        return "float";
    }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Float; }

private:
    Precision precision_;
};

class PointerType final : public Type {
public:
    explicit PointerType(unsigned addressSpace = 0)
        : Type(TypeKind::Pointer), addressSpace_(addressSpace) {}

    unsigned getAddressSpace() const { return addressSpace_; }

    size_t getSizeInBits() const override { return 64; }
    size_t getAlignmentInBytes() const override { return 8; }
    std::string toLLVM() const override {
        if (addressSpace_ == 0) return "ptr";
        return "ptr addrspace(" + std::to_string(addressSpace_) + ")";
    }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Pointer; }

private:
    unsigned addressSpace_;
};

// ============================================================================
// Value Kind Enumeration
// ============================================================================

enum class ValueKind {
    Constant,
    Argument,
    Instruction,
    BasicBlock,
    Function,
    GlobalVariable
};

// ============================================================================
// Value Base Class
// ============================================================================

class Value {
public:
    virtual ~Value() = default;

    ValueKind getValueKind() const { return kind_; }
    Type* getType() const { return type_; }

    // Name (for named values like variables, functions)
    std::string_view getName() const { return name_; }
    void setName(std::string_view name) { name_ = std::string(name); }
    bool hasName() const { return !name_.empty(); }

protected:
    Value(ValueKind kind, Type* type, std::string_view name = "")
        : kind_(kind), type_(type), name_(name) {}

private:
    ValueKind kind_;
    Type* type_;
    std::string name_;
};

// ============================================================================
// TypedValue - Compile-Time Type-Safe Value Wrapper
// ============================================================================

// Type tag structs for compile-time type discrimination
struct IntTag {};
struct FloatTag {};
struct PtrTag {};
struct VoidTag {};

template<typename Tag>
class TypedValue {
public:
    // Construct from raw value with type checking
    explicit TypedValue(Value* value);

    // Get the underlying value
    Value* raw() const { return value_; }

    // Check if valid
    explicit operator bool() const { return value_ != nullptr; }

    // Comparison
    bool operator==(const TypedValue& other) const { return value_ == other.value_; }
    bool operator!=(const TypedValue& other) const { return value_ != other.value_; }

private:
    Value* value_;
};

// Type aliases for common typed values
using IntValue = TypedValue<IntTag>;
using FloatValue = TypedValue<FloatTag>;
using PtrValue = TypedValue<PtrTag>;
using BoolValue = IntValue;  // i1 is an integer type

// Void is special - represents no value
using VoidValue = TypedValue<VoidTag>;

// ============================================================================
// TypedValue Specializations
// ============================================================================

template<>
class TypedValue<IntTag> {
public:
    explicit TypedValue(Value* value) : value_(value) {
        assert(value && "IntValue requires non-null value");
        assert(value->getType()->isInteger() && "IntValue requires integer type");
    }

    // Create null/invalid IntValue
    TypedValue() : value_(nullptr) {}

    Value* raw() const { return value_; }
    IntegerType* type() const {
        return value_ ? static_cast<IntegerType*>(value_->getType()) : nullptr;
    }

    unsigned getBitWidth() const {
        return type() ? type()->getBitWidth() : 0;
    }

    explicit operator bool() const { return value_ != nullptr; }
    bool operator==(const TypedValue& other) const { return value_ == other.value_; }
    bool operator!=(const TypedValue& other) const { return value_ != other.value_; }

private:
    Value* value_;
};

template<>
class TypedValue<FloatTag> {
public:
    explicit TypedValue(Value* value) : value_(value) {
        assert(value && "FloatValue requires non-null value");
        assert(value->getType()->isFloat() && "FloatValue requires float type");
    }

    TypedValue() : value_(nullptr) {}

    Value* raw() const { return value_; }
    FloatType* type() const {
        return value_ ? static_cast<FloatType*>(value_->getType()) : nullptr;
    }

    FloatType::Precision getPrecision() const {
        return type() ? type()->getPrecision() : FloatType::Precision::Float;
    }

    explicit operator bool() const { return value_ != nullptr; }
    bool operator==(const TypedValue& other) const { return value_ == other.value_; }
    bool operator!=(const TypedValue& other) const { return value_ != other.value_; }

private:
    Value* value_;
};

template<>
class TypedValue<PtrTag> {
public:
    explicit TypedValue(Value* value) : value_(value) {
        assert(value && "PtrValue requires non-null value");
        assert(value->getType()->isPointer() && "PtrValue requires pointer type");
    }

    TypedValue() : value_(nullptr) {}

    Value* raw() const { return value_; }
    PointerType* type() const {
        return value_ ? static_cast<PointerType*>(value_->getType()) : nullptr;
    }

    unsigned getAddressSpace() const {
        return type() ? type()->getAddressSpace() : 0;
    }

    explicit operator bool() const { return value_ != nullptr; }
    bool operator==(const TypedValue& other) const { return value_ == other.value_; }
    bool operator!=(const TypedValue& other) const { return value_ != other.value_; }

private:
    Value* value_;
};

template<>
class TypedValue<VoidTag> {
public:
    // Void values are always "empty" - they represent no value
    TypedValue() = default;
    explicit TypedValue(std::nullptr_t) {}

    Value* raw() const { return nullptr; }

    explicit operator bool() const { return false; }  // Void is never "valid"

private:
};

// ============================================================================
// AnyValue - Runtime Type Variant
// ============================================================================

// When the type isn't known at compile time, use AnyValue
class AnyValue {
public:
    AnyValue() = default;

    AnyValue(IntValue v) : value_(v.raw()), kind_(Kind::Int) {}
    AnyValue(FloatValue v) : value_(v.raw()), kind_(Kind::Float) {}
    AnyValue(PtrValue v) : value_(v.raw()), kind_(Kind::Ptr) {}
    AnyValue(VoidValue) : value_(nullptr), kind_(Kind::Void) {}

    // Direct construction from raw value (determines type at runtime)
    explicit AnyValue(Value* v);

    // Type checking
    bool isInt() const { return kind_ == Kind::Int; }
    bool isFloat() const { return kind_ == Kind::Float; }
    bool isPtr() const { return kind_ == Kind::Ptr; }
    bool isVoid() const { return kind_ == Kind::Void; }

    // Type-safe extraction (throws if wrong type)
    IntValue asInt() const;
    FloatValue asFloat() const;
    PtrValue asPtr() const;

    // Try extraction (returns empty optional if wrong type)
    std::optional<IntValue> tryAsInt() const;
    std::optional<FloatValue> tryAsFloat() const;
    std::optional<PtrValue> tryAsPtr() const;

    // Raw access
    Value* raw() const { return value_; }
    Type* type() const { return value_ ? value_->getType() : nullptr; }

    explicit operator bool() const { return value_ != nullptr; }

private:
    enum class Kind { Void, Int, Float, Ptr };

    Value* value_ = nullptr;
    Kind kind_ = Kind::Void;
};

// ============================================================================
// Type Conversion Helpers
// ============================================================================

// Check if a value can be converted to a specific typed value
inline bool canBeInt(const Value* v) {
    return v && v->getType() && v->getType()->isInteger();
}

inline bool canBeFloat(const Value* v) {
    return v && v->getType() && v->getType()->isFloat();
}

inline bool canBePtr(const Value* v) {
    return v && v->getType() && v->getType()->isPointer();
}

inline bool canBeBool(const Value* v) {
    if (!canBeInt(v)) return false;
    auto* intTy = static_cast<IntegerType*>(v->getType());
    return intTy->getBitWidth() == 1;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
