#pragma once

#include "Backends/LLVMValue.h"
#include "Backends/LLVMType.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace XXML {
namespace Backends {

/**
 * Tracks values and their types during IR generation
 */
class ValueTracker {
public:
    ValueTracker() = default;

    // Register a value with its type
    void registerValue(const std::string& name, const LLVMValue& value);

    // Get a value by name
    const LLVMValue* getValue(const std::string& name) const;

    // Check if value exists
    bool hasValue(const std::string& name) const;

    // Get type of a value
    LLVMType getType(const std::string& name) const;

    // Clear all values (e.g., at function scope exit)
    void clear();

    // Push/pop scope for nested contexts
    void pushScope();
    void popScope();

private:
    std::unordered_map<std::string, LLVMValue> values_;
    std::vector<std::unordered_map<std::string, LLVMValue>> scopeStack_;
};

} // namespace Backends
} // namespace XXML
