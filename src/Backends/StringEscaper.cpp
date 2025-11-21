#include "Backends/StringEscaper.h"
#include <sstream>

namespace XXML {
namespace Backends {

std::string StringEscaper::escape(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
        switch (c) {
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            case '\\': escaped << "\\\\"; break;
            case '"':  escaped << "\\\""; break;
            case '\0': escaped << "\\00"; break;
            default:
                if (c >= 32 && c <= 126) {
                    escaped << c;
                } else {
                    // Non-printable: use hex escape
                    escaped << "\\";
                    int val = static_cast<unsigned char>(c);
                    escaped << ((val >> 4) < 10 ? ('0' + (val >> 4)) : ('A' + (val >> 4) - 10));
                    escaped << ((val & 0xF) < 10 ? ('0' + (val & 0xF)) : ('A' + (val & 0xF) - 10));
                }
        }
    }
    return escaped.str();
}

std::string StringEscaper::unescape(const std::string& str) {
    std::ostringstream unescaped;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            switch (str[i + 1]) {
                case 'n': unescaped << '\n'; i++; break;
                case 'r': unescaped << '\r'; i++; break;
                case 't': unescaped << '\t'; i++; break;
                case '\\': unescaped << '\\'; i++; break;
                case '"': unescaped << '"'; i++; break;
                case '0':
                    if (i + 2 < str.length() && str[i + 2] == '0') {
                        unescaped << '\0';
                        i += 2;
                    } else {
                        unescaped << str[i];
                    }
                    break;
                default: unescaped << str[i]; break;
            }
        } else {
            unescaped << str[i];
        }
    }
    return unescaped.str();
}

} // namespace Backends
} // namespace XXML
