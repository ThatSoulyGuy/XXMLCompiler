#include "Backends/IR/Values.h"
#include <cmath>
#include <limits>
#include <unordered_map>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Use Implementation
// ============================================================================

void Use::setValue(Value* v) {
    if (value_ == v) return;

    // Remove from old value's use list
    if (value_) {
        value_->removeUse(this);
    }

    value_ = v;

    // Add to new value's use list
    if (v) {
        v->addUse(this);
    }
}

// ============================================================================
// Value Implementation
// ============================================================================

size_t Value::getNumUses() const {
    size_t count = 0;
    for (Use* u = useListHead_; u != nullptr; u = u->next_) {
        ++count;
    }
    return count;
}

void Value::addUse(Use* use) {
    use->next_ = useListHead_;
    use->prev_ = nullptr;
    if (useListHead_) {
        useListHead_->prev_ = use;
    }
    useListHead_ = use;
}

void Value::removeUse(Use* use) {
    if (use->prev_) {
        use->prev_->next_ = use->next_;
    } else {
        useListHead_ = use->next_;
    }
    if (use->next_) {
        use->next_->prev_ = use->prev_;
    }
    use->next_ = nullptr;
    use->prev_ = nullptr;
}

void Value::replaceAllUsesWith(Value* newValue) {
    assert(newValue != this && "Cannot replace with self");
    assert(getType()->equals(*newValue->getType()) && "Type mismatch in RAUW");

    while (useListHead_) {
        Use* use = useListHead_;
        use->setValue(newValue);
    }
}

// ============================================================================
// ConstantInt Implementation
// ============================================================================

// Simple cache for common integer constants
static std::unordered_map<uint64_t, std::unique_ptr<ConstantInt>> int64Cache;
static std::unique_ptr<ConstantInt> trueConst;
static std::unique_ptr<ConstantInt> falseConst;

ConstantInt* ConstantInt::get(TypeContext& ctx, IntegerType* type, int64_t value) {
    return get(ctx, type, static_cast<uint64_t>(value));
}

ConstantInt* ConstantInt::get(TypeContext& ctx, IntegerType* type, uint64_t value) {
    // For now, just create new constants (could add caching later)
    // Note: These leak, but that's OK for a compiler that runs once
    return new ConstantInt(type, value);
}

ConstantInt* ConstantInt::getTrue(TypeContext& ctx) {
    return get(ctx, ctx.getInt1Ty(), static_cast<uint64_t>(1));
}

ConstantInt* ConstantInt::getFalse(TypeContext& ctx) {
    return get(ctx, ctx.getInt1Ty(), static_cast<uint64_t>(0));
}

// ============================================================================
// ConstantFP Implementation
// ============================================================================

ConstantFP* ConstantFP::get(TypeContext& ctx, FloatType* type, double value) {
    return new ConstantFP(type, value);
}

ConstantFP* ConstantFP::getZero(TypeContext& ctx, FloatType* type) {
    return get(ctx, type, 0.0);
}

bool ConstantFP::isNaN() const {
    return std::isnan(value_);
}

bool ConstantFP::isInfinity() const {
    return std::isinf(value_);
}

// ============================================================================
// ConstantNull Implementation
// ============================================================================

ConstantNull* ConstantNull::get(TypeContext& ctx, PointerType* type) {
    if (!type) {
        type = ctx.getPtrTy();
    }
    return new ConstantNull(type);
}

// ============================================================================
// ConstantUndef Implementation
// ============================================================================

ConstantUndef* ConstantUndef::get(TypeContext& ctx, Type* type) {
    return new ConstantUndef(type);
}

// ============================================================================
// ConstantZeroInit Implementation
// ============================================================================

ConstantZeroInit* ConstantZeroInit::get(TypeContext& ctx, Type* type) {
    return new ConstantZeroInit(type);
}

// ============================================================================
// ConstantString Implementation
// ============================================================================

ConstantString* ConstantString::get(TypeContext& ctx, const std::string& value, bool nullTerminated) {
    uint64_t size = value.length() + (nullTerminated ? 1 : 0);
    ArrayType* type = ctx.getArrayTy(ctx.getInt8Ty(), size);
    return new ConstantString(type, value, nullTerminated);
}

// ============================================================================
// ConstantArray Implementation
// ============================================================================

ConstantArray* ConstantArray::get(TypeContext& ctx, ArrayType* type, std::vector<Constant*> elements) {
    return new ConstantArray(type, std::move(elements));
}

// ============================================================================
// GlobalVariable Implementation
// ============================================================================

GlobalVariable::GlobalVariable(PointerType* ptrType, Type* valueType, const std::string& name,
                               Linkage linkage, Constant* initializer)
    : GlobalValue(Kind::GlobalVariable, ptrType, name, linkage),
      valueType_(valueType),
      initializer_(initializer),
      isConstant_(false),
      isThreadLocal_(false) {
    isDeclaration_ = (initializer == nullptr);
}

// ============================================================================
// Argument Implementation
// ============================================================================

Argument::Argument(Type* type, const std::string& name, Function* parent, unsigned argNo)
    : Value(Kind::Argument, type, name), parent_(parent), argNo_(argNo) {}

// ============================================================================
// Utility Functions
// ============================================================================

Constant* getZeroValue(TypeContext& ctx, Type* type) {
    switch (type->getKind()) {
        case Type::Kind::Integer:
            return ConstantInt::get(ctx, static_cast<IntegerType*>(type), static_cast<uint64_t>(0));

        case Type::Kind::Float:
            return ConstantFP::getZero(ctx, static_cast<FloatType*>(type));

        case Type::Kind::Pointer:
            return ConstantNull::get(ctx, static_cast<PointerType*>(type));

        case Type::Kind::Array:
        case Type::Kind::Struct:
            return ConstantZeroInit::get(ctx, type);

        case Type::Kind::Void:
        case Type::Kind::Function:
        case Type::Kind::Label:
        default:
            return nullptr;
    }
}

Constant* getUndefValue(TypeContext& ctx, Type* type) {
    return ConstantUndef::get(ctx, type);
}

} // namespace IR
} // namespace Backends
} // namespace XXML
