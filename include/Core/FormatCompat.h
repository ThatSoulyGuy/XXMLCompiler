#pragma once

#include <string>
#include <sstream>
#include <string_view>

namespace XXML::Core {

// Compatibility layer for std::format until widely available
// Simple string formatting using stringstream

template<typename... Args>
inline std::string format(std::string_view fmt, Args&&... args) {
    // For simple cases, just concatenate
    // This is a simplified version - production code would use a proper format library
    std::ostringstream oss;

    // Helper lambda to process each argument
    auto process = [&oss](auto&& arg) {
        if constexpr (std::is_convertible_v<decltype(arg), std::string_view>) {
            oss << arg;
        } else {
            oss << arg;
        }
    };

    // Process all arguments
    (process(std::forward<Args>(args)), ...);

    return oss.str();
}

// Specialized overloads for common patterns
inline std::string format(std::string_view str) {
    return std::string(str);
}

template<typename T>
inline std::string format(std::string_view fmt, const T& arg) {
    // Simple replacement of {} with argument
    std::string result(fmt);
    size_t pos = result.find("{}");
    if (pos != std::string::npos) {
        std::ostringstream oss;
        oss << arg;
        result.replace(pos, 2, oss.str());
    }
    return result;
}

template<typename T1, typename T2>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2) {
    std::string result(fmt);

    // Replace first {}
    size_t pos = result.find("{}");
    if (pos != std::string::npos) {
        std::ostringstream oss1;
        oss1 << arg1;
        result.replace(pos, 2, oss1.str());
    }

    // Replace second {}
    pos = result.find("{}");
    if (pos != std::string::npos) {
        std::ostringstream oss2;
        oss2 << arg2;
        result.replace(pos, 2, oss2.str());
    }

    return result;
}

template<typename T1, typename T2, typename T3>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    return result;
}

} // namespace XXML::Core

// Alias for convenience
namespace std {
    using XXML::Core::format;
}
