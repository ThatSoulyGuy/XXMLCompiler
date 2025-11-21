#pragma once

#include <string>

namespace XXML {
namespace Backends {

/**
 * Utility for escaping strings for LLVM IR
 */
class StringEscaper {
public:
    // Escape a string for use in LLVM IR string literals
    static std::string escape(const std::string& str);

    // Unescape a string from LLVM IR format
    static std::string unescape(const std::string& str);
};

} // namespace Backends
} // namespace XXML
