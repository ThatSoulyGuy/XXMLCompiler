#include "Backends/LLVMIR/TypedValue.h"
#include <stdexcept>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// AnyValue Implementation
// ============================================================================

AnyValue::AnyValue(Value* v) : value_(v) {
    if (!v) {
        kind_ = Kind::Void;
        return;
    }

    Type* type = v->getType();
    if (!type) {
        kind_ = Kind::Void;
        return;
    }

    switch (type->getKind()) {
        case TypeKind::Integer:
            kind_ = Kind::Int;
            break;
        case TypeKind::Float:
            kind_ = Kind::Float;
            break;
        case TypeKind::Pointer:
            kind_ = Kind::Ptr;
            break;
        case TypeKind::Void:
        default:
            kind_ = Kind::Void;
            break;
    }
}

IntValue AnyValue::asInt() const {
    if (kind_ != Kind::Int) {
        throw std::runtime_error("AnyValue is not an IntValue");
    }
    return IntValue(value_);
}

FloatValue AnyValue::asFloat() const {
    if (kind_ != Kind::Float) {
        throw std::runtime_error("AnyValue is not a FloatValue");
    }
    return FloatValue(value_);
}

PtrValue AnyValue::asPtr() const {
    if (kind_ != Kind::Ptr) {
        throw std::runtime_error("AnyValue is not a PtrValue");
    }
    return PtrValue(value_);
}

std::optional<IntValue> AnyValue::tryAsInt() const {
    if (kind_ == Kind::Int) {
        return IntValue(value_);
    }
    return std::nullopt;
}

std::optional<FloatValue> AnyValue::tryAsFloat() const {
    if (kind_ == Kind::Float) {
        return FloatValue(value_);
    }
    return std::nullopt;
}

std::optional<PtrValue> AnyValue::tryAsPtr() const {
    if (kind_ == Kind::Ptr) {
        return PtrValue(value_);
    }
    return std::nullopt;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
