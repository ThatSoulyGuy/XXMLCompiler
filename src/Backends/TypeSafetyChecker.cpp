#include "Backends/TypeSafetyChecker.h"

namespace XXML {
namespace Backends {

bool TypeSafetyChecker::areCompatible(const LLVMType& from, const LLVMType& to) {
    // Same types are always compatible
    if (from == to) return true;

    // Pointers are compatible with each other
    if (from.isPointer() && to.isPointer()) return true;

    // i64 and ptr are compatible (with conversion)
    if ((from.getKind() == LLVMType::Kind::I64 && to.isPointer()) ||
        (from.isPointer() && to.getKind() == LLVMType::Kind::I64)) {
        return true;
    }

    return false;
}

bool TypeSafetyChecker::canUseAs(const LLVMValue& value, const LLVMType& targetType) {
    return areCompatible(value.getType(), targetType);
}

bool TypeSafetyChecker::needsConversion(const LLVMType& from, const LLVMType& to) {
    // Same types don't need conversion
    if (from == to) return false;

    // Different types need conversion
    if ((from.getKind() == LLVMType::Kind::I64 && to.isPointer()) ||
        (from.isPointer() && to.getKind() == LLVMType::Kind::I64)) {
        return true;
    }

    return false;
}

std::string TypeSafetyChecker::getConversionOp(const LLVMType& from, const LLVMType& to) {
    if (from.getKind() == LLVMType::Kind::I64 && to.isPointer()) {
        return "inttoptr";
    }
    if (from.isPointer() && to.getKind() == LLVMType::Kind::I64) {
        return "ptrtoint";
    }
    return "";
}

} // namespace Backends
} // namespace XXML
