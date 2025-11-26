#pragma once

#include <string>
#include <sstream>
#include <string_view>

// Check if std::format is available (C++20 with library support)
#if __cplusplus >= 202002L && __has_include(<format>)
    #define HAS_STD_FORMAT 1
    #include <format>
#else
    #define HAS_STD_FORMAT 0
#endif

namespace XXML::Core {

#if !HAS_STD_FORMAT
// Only define compatibility layer if std::format is not available

// Compatibility layer for std::format until widely available
// Simple string formatting using stringstream

// Forward declarations
template<typename T>
inline std::string format(std::string_view fmt, const T& arg);

template<typename T1, typename T2>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2);

template<typename T1, typename T2, typename T3>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3);

template<typename T1, typename T2, typename T3, typename T4>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4);

template<typename T1, typename T2, typename T3, typename T4, typename T5>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5);

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6);

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6, const T7& arg7);

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6, const T7& arg7, const T8& arg8);

// Variadic template - fallback for more than 8 args
// This should only be used when the specialized templates don't match
template<typename... Args>
inline std::string format(std::string_view fmt, Args&&... args)
requires (sizeof...(Args) > 8) {
    // For many arguments, just return the format string for now
    // Real implementation would parse format string properly
    return std::string(fmt);
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

template<typename T1, typename T2, typename T3, typename T4>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3, oss4;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;
    oss4 << arg4;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss4.str());

    return result;
}

template<typename T1, typename T2, typename T3, typename T4, typename T5>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3, oss4, oss5;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;
    oss4 << arg4;
    oss5 << arg5;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss4.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss5.str());

    return result;
}

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3, oss4, oss5, oss6;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;
    oss4 << arg4;
    oss5 << arg5;
    oss6 << arg6;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss4.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss5.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss6.str());

    return result;
}

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6, const T7& arg7) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3, oss4, oss5, oss6, oss7;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;
    oss4 << arg4;
    oss5 << arg5;
    oss6 << arg6;
    oss7 << arg7;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss4.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss5.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss6.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss7.str());

    return result;
}

template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
inline std::string format(std::string_view fmt, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6, const T7& arg7, const T8& arg8) {
    std::string result(fmt);

    std::ostringstream oss1, oss2, oss3, oss4, oss5, oss6, oss7, oss8;
    oss1 << arg1;
    oss2 << arg2;
    oss3 << arg3;
    oss4 << arg4;
    oss5 << arg5;
    oss6 << arg6;
    oss7 << arg7;
    oss8 << arg8;

    size_t pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss1.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss2.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss3.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss4.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss5.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss6.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss7.str());

    pos = result.find("{}");
    if (pos != std::string::npos) result.replace(pos, 2, oss8.str());

    return result;
}

#endif // !HAS_STD_FORMAT

// Unified format function that uses std::format if available, otherwise uses compat layer
#if HAS_STD_FORMAT
    // Use std::format directly
    using std::format;
#endif

} // namespace XXML::Core
