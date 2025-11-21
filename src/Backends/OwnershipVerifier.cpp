#include "Backends/OwnershipVerifier.h"

namespace XXML {
namespace Backends {

void OwnershipVerifier::registerOwnership(const std::string& varName, OwnershipKind kind) {
    ownership_[varName] = kind;
}

void OwnershipVerifier::transferOwnership(const std::string& from, const std::string& to) {
    auto it = ownership_.find(from);
    if (it != ownership_.end() && it->second == OwnershipKind::Owned) {
        ownership_[from] = OwnershipKind::Moved;
        ownership_[to] = OwnershipKind::Owned;
    }
}

void OwnershipVerifier::borrow(const std::string& varName) {
    ownership_[varName] = OwnershipKind::Borrowed;
}

bool OwnershipVerifier::canUse(const std::string& varName) const {
    auto it = ownership_.find(varName);
    if (it == ownership_.end()) return true; // Unknown variables can be used
    return it->second != OwnershipKind::Moved;
}

bool OwnershipVerifier::canMove(const std::string& varName) const {
    auto it = ownership_.find(varName);
    if (it == ownership_.end()) return false;
    return it->second == OwnershipKind::Owned;
}

void OwnershipVerifier::clear() {
    ownership_.clear();
}

} // namespace Backends
} // namespace XXML
