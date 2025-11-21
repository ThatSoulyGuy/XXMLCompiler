#pragma once

#include <string>
#include <memory>

namespace XXML {
namespace Backends {

/**
 * Represents an LLVM type with proper type safety and conversions
 */
class LLVMType {
public:
    enum class Kind {
        Void,
        I1,      // bool
        I8,      // byte
        I32,     // int
        I64,     // long/Integer
        Float,
        Double,
        Ptr,     // pointer
        Struct,  // class types
        Unknown
    };

    LLVMType() : kind_(Kind::Unknown), structName_("") {}
    explicit LLVMType(Kind kind) : kind_(kind), structName_("") {}
    LLVMType(Kind kind, const std::string& structName) : kind_(kind), structName_(structName) {}

    // Static factory methods
    static LLVMType getVoidType() { return LLVMType(Kind::Void); }
    static LLVMType getI1Type() { return LLVMType(Kind::I1); }
    static LLVMType getI8Type() { return LLVMType(Kind::I8); }
    static LLVMType getI32Type() { return LLVMType(Kind::I32); }
    static LLVMType getI64Type() { return LLVMType(Kind::I64); }
    static LLVMType getFloatType() { return LLVMType(Kind::Float); }
    static LLVMType getDoubleType() { return LLVMType(Kind::Double); }
    static LLVMType getPointerType() { return LLVMType(Kind::Ptr); }
    static LLVMType getStructType(const std::string& name) { return LLVMType(Kind::Struct, name); }

    // Query methods
    Kind getKind() const { return kind_; }
    bool isVoid() const { return kind_ == Kind::Void; }
    bool isInteger() const { return kind_ == Kind::I1 || kind_ == Kind::I8 || kind_ == Kind::I32 || kind_ == Kind::I64; }
    bool isFloatingPoint() const { return kind_ == Kind::Float || kind_ == Kind::Double; }
    bool isPointer() const { return kind_ == Kind::Ptr; }
    bool isStruct() const { return kind_ == Kind::Struct; }

    const std::string& getStructName() const { return structName_; }

    // Convert to LLVM IR string representation
    std::string toString() const;

    // Equality
    bool operator==(const LLVMType& other) const {
        return kind_ == other.kind_ && structName_ == other.structName_;
    }
    bool operator!=(const LLVMType& other) const { return !(*this == other); }

private:
    Kind kind_;
    std::string structName_; // For struct types
};

} // namespace Backends
} // namespace XXML
