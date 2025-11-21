#include "Backends/ValueTracker.h"

namespace XXML {
namespace Backends {

void ValueTracker::registerValue(const std::string& name, const LLVMValue& value) {
    values_[name] = value;
}

const LLVMValue* ValueTracker::getValue(const std::string& name) const {
    auto it = values_.find(name);
    if (it != values_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ValueTracker::hasValue(const std::string& name) const {
    return values_.find(name) != values_.end();
}

LLVMType ValueTracker::getType(const std::string& name) const {
    auto it = values_.find(name);
    if (it != values_.end()) {
        return it->second.getType();
    }
    return LLVMType::getPointerType(); // Default
}

void ValueTracker::clear() {
    values_.clear();
}

void ValueTracker::pushScope() {
    scopeStack_.push_back(values_);
}

void ValueTracker::popScope() {
    if (!scopeStack_.empty()) {
        values_ = scopeStack_.back();
        scopeStack_.pop_back();
    }
}

} // namespace Backends
} // namespace XXML
