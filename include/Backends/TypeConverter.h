#pragma once

#include "Backends/LLVMType.h"
#include <string>

namespace XXML {
namespace Backends {

/**
 * Converts between XXML types and LLVM types
 */
class TypeConverter {
public:
    // Convert XXML type name to LLVM type
    static LLVMType xxmlToLLVM(const std::string& xxmlType);

    // Convert LLVM type back to XXML type name
    static std::string llvmToXXML(const LLVMType& llvmType);

    // Check if a type is a primitive XXML type
    static bool isPrimitive(const std::string& xxmlType);

    // Get the underlying representation for a type (for passing by value vs pointer)
    static LLVMType getRepresentationType(const std::string& xxmlType);
};

} // namespace Backends
} // namespace XXML
