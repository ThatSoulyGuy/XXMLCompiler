#include "Core/IBackend.h"
#include <format>

namespace XXML::Core {

void BackendBase::updateIndentString() {
    if (config_.useSpaces) {
        currentIndent_ = std::string(indentLevel_ * config_.indentSize, ' ');
    } else {
        currentIndent_ = std::string(indentLevel_, '\t');
    }
}

} // namespace XXML::Core
