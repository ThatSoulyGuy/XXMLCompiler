#pragma once
#include <string>
#include <cstdint>
#include "TokenType.h"
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Lexer {

class Token {
public:
    TokenType type;
    std::string lexeme;  // The actual text from source
    Common::SourceLocation location;

    // For literals
    union {
        int64_t intValue;
        bool boolValue;
    };
    std::string stringValue;

    Token()
        : type(TokenType::Invalid), lexeme(""), intValue(0) {}

    Token(TokenType t, const std::string& lex, const Common::SourceLocation& loc)
        : type(t), lexeme(lex), location(loc), intValue(0) {}

    bool is(TokenType t) const { return type == t; }
    bool isNot(TokenType t) const { return type != t; }
    bool isOneOf(TokenType t1, TokenType t2) const { return is(t1) || is(t2); }

    template<typename... Args>
    bool isOneOf(TokenType t1, TokenType t2, Args... args) const {
        return is(t1) || isOneOf(t2, args...);
    }

    bool isKeyword() const;
    bool isOperator() const;
    bool isLiteral() const;

    std::string toString() const;
};

} // namespace Lexer
} // namespace XXML
