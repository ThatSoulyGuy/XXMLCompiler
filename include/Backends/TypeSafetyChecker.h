#pragma once

#include "Backends/LLVMType.h"
#include "Backends/LLVMValue.h"
#include <string>

namespace XXML {
namespace Backends {

/**
 * Checks type safety during IR generation
 */
class TypeSafetyChecker {
public:
    // Check if types are compatible for assignment
    static bool areCompatible(const LLVMType& from, const LLVMType& to);

    // Check if a value can be used as a specific type
    static bool canUseAs(const LLVMValue& value, const LLVMType& targetType);

    // Check if conversion is needed
    static bool needsConversion(const LLVMType& from, const LLVMType& to);

    // Get conversion instruction (e.g., "inttoptr", "ptrtoint")
    static std::string getConversionOp(const LLVMType& from, const LLVMType& to);
};

} // namespace Backends
} // namespace XXML
