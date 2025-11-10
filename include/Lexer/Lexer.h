#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Token.h"
#include "../Common/Error.h"

namespace XXML {
namespace Lexer {

class Lexer {
private:
    std::string source;
    std::string filename;
    size_t position;
    size_t line;
    size_t column;
    Common::ErrorReporter& errorReporter;

    char current() const;
    char peek(size_t offset = 1) const;
    char advance();
    bool isAtEnd() const;
    bool match(char expected);

    void skipWhitespace();
    void skipComment();

    Token makeToken(TokenType type, const std::string& lexeme);
    Token makeToken(TokenType type, const std::string& lexeme, const Common::SourceLocation& loc);

    Token lexIdentifier();
    Token lexAngleBracketIdentifier();
    Token lexNumber();
    Token lexString();
    Token lexOperator();

    bool isDigit(char c) const;
    bool isAlpha(char c) const;
    bool isAlphaNumeric(char c) const;
    bool isIdentifierChar(char c) const;

public:
    Lexer(const std::string& src, const std::string& file, Common::ErrorReporter& reporter);

    Token nextToken();
    std::vector<Token> tokenize();

    Common::SourceLocation getCurrentLocation() const;
};

} // namespace Lexer
} // namespace XXML
