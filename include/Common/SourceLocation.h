#pragma once
#include <string>

namespace XXML {
namespace Common {

/**
 * Represents a location in source code (file, line, column)
 * Used for error reporting and diagnostics
 */
struct SourceLocation {
    std::string filename;
    size_t line;
    size_t column;
    size_t position; // Absolute position in file

    SourceLocation()
        : filename(""), line(1), column(1), position(0) {}

    SourceLocation(const std::string& file, size_t ln, size_t col, size_t pos)
        : filename(file), line(ln), column(col), position(pos) {}

    std::string toString() const {
        return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
    }

    bool isValid() const {
        return !filename.empty() && line > 0;
    }
};

} // namespace Common
} // namespace XXML
