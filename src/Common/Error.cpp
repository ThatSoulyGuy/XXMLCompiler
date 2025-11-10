#include "../../include/Common/Error.h"
#include <iostream>

namespace XXML {
namespace Common {

std::string Error::getLevelString() const {
    switch (level) {
        case ErrorLevel::Note: return "note";
        case ErrorLevel::Warning: return "warning";
        case ErrorLevel::Error: return "error";
        case ErrorLevel::Fatal: return "fatal error";
        default: return "unknown";
    }
}

std::string Error::getColorCode() const {
    // ANSI color codes
    switch (level) {
        case ErrorLevel::Note: return "\033[1;36m";      // Cyan
        case ErrorLevel::Warning: return "\033[1;33m";   // Yellow
        case ErrorLevel::Error: return "\033[1;31m";     // Red
        case ErrorLevel::Fatal: return "\033[1;35m";     // Magenta
        default: return "\033[0m";                       // Reset
    }
}

std::string Error::toString() const {
    std::string result = getColorCode();
    result += location.toString() + ": ";
    result += getLevelString() + ": ";
    result += "\033[0m"; // Reset color
    result += message + " [" + std::to_string(static_cast<int>(code)) + "]\n";

    if (!sourceSnippet.empty()) {
        result += "  " + sourceSnippet + "\n";
        // Add caret pointer
        result += "  ";
        for (size_t i = 1; i < location.column; ++i) {
            result += " ";
        }
        result += getColorCode() + "^\033[0m\n";
    }

    return result;
}

void ErrorReporter::reportError(ErrorCode code, const std::string& message, const SourceLocation& loc) {
    errors.emplace_back(ErrorLevel::Error, code, message, loc);
    hasErrors_ = true;
}

void ErrorReporter::reportWarning(ErrorCode code, const std::string& message, const SourceLocation& loc) {
    errors.emplace_back(ErrorLevel::Warning, code, message, loc);
    hasWarnings_ = true;
}

void ErrorReporter::reportNote(const std::string& message, const SourceLocation& loc) {
    errors.emplace_back(ErrorLevel::Note, ErrorCode::InternalError, message, loc);
}

void ErrorReporter::printErrors() const {
    for (const auto& error : errors) {
        std::cerr << error.toString();
    }

    if (hasErrors_ || hasWarnings_) {
        std::cerr << "\n";
        if (hasErrors_) {
            size_t errorCount = 0;
            for (const auto& e : errors) {
                if (e.level == ErrorLevel::Error || e.level == ErrorLevel::Fatal) {
                    errorCount++;
                }
            }
            std::cerr << errorCount << " error(s) generated.\n";
        }
        if (hasWarnings_) {
            size_t warningCount = 0;
            for (const auto& e : errors) {
                if (e.level == ErrorLevel::Warning) {
                    warningCount++;
                }
            }
            std::cerr << warningCount << " warning(s) generated.\n";
        }
    }
}

void ErrorReporter::clear() {
    errors.clear();
    hasErrors_ = false;
    hasWarnings_ = false;
}

} // namespace Common
} // namespace XXML
