#pragma once

#include "Backends/LLVMType.h"
#include <string>
#include <memory>

namespace XXML {
namespace Backends {

/**
 * Represents a value in LLVM IR (register, constant, or global)
 */
class LLVMValue {
public:
    enum class Kind {
        Register,   // %r1, %r2, etc.
        Constant,   // 42, 3.14, etc.
        Global,     // @global_var, @.str.0, etc.
        Null        // null pointer
    };

    LLVMValue() : kind_(Kind::Null), name_(""), type_(LLVMType::getPointerType()) {}
    LLVMValue(Kind kind, const std::string& name, const LLVMType& type)
        : kind_(kind), name_(name), type_(type) {}

    // Static factory methods
    static LLVMValue makeRegister(const std::string& name, const LLVMType& type) {
        return LLVMValue(Kind::Register, name, type);
    }
    static LLVMValue makeConstant(const std::string& value, const LLVMType& type) {
        return LLVMValue(Kind::Constant, value, type);
    }
    static LLVMValue makeGlobal(const std::string& name, const LLVMType& type) {
        return LLVMValue(Kind::Global, name, type);
    }
    static LLVMValue makeNull() {
        return LLVMValue(Kind::Null, "null", LLVMType::getPointerType());
    }

    // Get type-appropriate default value (null for pointers, 0 for integers, etc.)
    static LLVMValue getDefaultValue(const LLVMType& type) {
        if (type.isPointer()) {
            return makeNull();  // ptr null
        } else if (type.getKind() == LLVMType::Kind::I1) {
            return makeConstant("false", type);  // i1 false
        } else if (type.isInteger()) {
            return makeConstant("0", type);  // i64 0, i32 0, etc.
        } else if (type.isFloatingPoint()) {
            return makeConstant("0.0", type);  // float 0.0, double 0.0
        } else if (type.isStruct()) {
            // For struct types, return zeroinitializer
            return makeConstant("zeroinitializer", type);
        }
        // Default fallback: null pointer
        return makeNull();
    }

    // Query methods
    Kind getKind() const { return kind_; }
    const std::string& getName() const { return name_; }
    const LLVMType& getType() const { return type_; }

    bool isRegister() const { return kind_ == Kind::Register; }
    bool isConstant() const { return kind_ == Kind::Constant; }
    bool isGlobal() const { return kind_ == Kind::Global; }
    bool isNull() const { return kind_ == Kind::Null; }

    // Get LLVM IR representation
    std::string toIR() const;

private:
    Kind kind_;
    std::string name_;
    LLVMType type_;
};

} // namespace Backends
} // namespace XXML
