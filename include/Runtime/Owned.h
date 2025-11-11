#ifndef XXML_RUNTIME_OWNED_H
#define XXML_RUNTIME_OWNED_H

#include <utility>
#include <cassert>
#include <stdexcept>

namespace Language {
namespace Runtime {

/**
 * Owned<T> - Runtime wrapper for owned values (T^)
 *
 * Implements move-only semantics with runtime tracking to detect use-after-move.
 *
 * Key properties:
 * - Cannot be copied (move-only)
 * - Tracks whether value has been moved from
 * - Asserts on use-after-move in debug builds
 * - Throws exception in release builds if accessed after move
 */
template<typename T>
class Owned {
private:
    T value_;
    bool movedFrom_;

public:
    // Constructor from value
    explicit Owned(T&& val)
        : value_(std::move(val)), movedFrom_(false) {}

    // Constructor from lvalue (explicit copy)
    explicit Owned(const T& val)
        : value_(val), movedFrom_(false) {}

    // Delete copy constructor and assignment
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;

    // Move constructor
    Owned(Owned&& other) noexcept
        : value_(std::move(other.value_)), movedFrom_(false) {
        other.movedFrom_ = true;
    }

    // Move assignment
    Owned& operator=(Owned&& other) noexcept {
        if (this != &other) {
            value_ = std::move(other.value_);
            movedFrom_ = false;
            other.movedFrom_ = true;
        }
        return *this;
    }

    // Get reference to value (checks if moved from)
    T& get() {
        checkNotMovedFrom("get()");
        return value_;
    }

    const T& get() const {
        checkNotMovedFrom("get()");
        return value_;
    }

    // Extract value (marks as moved from)
    T extract() {
        checkNotMovedFrom("extract()");
        movedFrom_ = true;
        return std::move(value_);
    }

    // Implicit conversion to T& for convenience
    operator T&() {
        return get();
    }

    operator const T&() const {
        return get();
    }

    // Arrow operator for method access
    T* operator->() {
        checkNotMovedFrom("operator->");
        return &value_;
    }

    const T* operator->() const {
        checkNotMovedFrom("operator->");
        return &value_;
    }

    // Check if value has been moved from
    bool isMovedFrom() const {
        return movedFrom_;
    }

private:
    void checkNotMovedFrom(const char* operation) const {
        if (movedFrom_) {
            // In debug mode, assert
            assert(false && "Attempted to use value after it was moved");

            // In release mode, throw exception
            #ifdef NDEBUG
            throw std::runtime_error(std::string("Use-after-move detected in ") + operation);
            #endif
        }
    }
};

// Helper function to create Owned<T> from value
template<typename T>
Owned<T> makeOwned(T&& value) {
    return Owned<T>(std::forward<T>(value));
}

} // namespace Runtime
} // namespace Language

#endif // XXML_RUNTIME_OWNED_H
