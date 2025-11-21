#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>

namespace XXML {
namespace Backends {

/**
 * Verifies ownership semantics for memory safety
 */
class OwnershipVerifier {
public:
    enum class OwnershipKind {
        Owned,      // Variable owns the resource
        Borrowed,   // Variable borrows a reference
        Moved       // Ownership has been moved away
    };

    OwnershipVerifier() = default;

    // Register ownership of a variable
    void registerOwnership(const std::string& varName, OwnershipKind kind);

    // Transfer ownership (move)
    void transferOwnership(const std::string& from, const std::string& to);

    // Borrow a reference
    void borrow(const std::string& varName);

    // Check if a variable can be used
    bool canUse(const std::string& varName) const;

    // Check if a variable can be moved
    bool canMove(const std::string& varName) const;

    // Clear ownership information
    void clear();

private:
    std::unordered_map<std::string, OwnershipKind> ownership_;
};

} // namespace Backends
} // namespace XXML
