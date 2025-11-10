#include "../../include/Lexer/Lexer.h"
#include <cctype>
#include <sstream>

namespace XXML {
namespace Lexer {

Lexer::Lexer(const std::string& src, const std::string& file, Common::ErrorReporter& reporter)
    : source(src), filename(file), position(0), line(1), column(1), errorReporter(reporter) {}

char Lexer::current() const {
    if (isAtEnd()) return '\0';
    return source[position];
}

char Lexer::peek(size_t offset) const {
    if (position + offset >= source.length()) return '\0';
    return source[position + offset];
}

char Lexer::advance() {
    if (isAtEnd()) return '\0';
    char c = source[position++];
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return position >= source.length();
}

bool Lexer::match(char expected) {
    if (isAtEnd() || current() != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek() == '/') {
            skipComment();
        } else {
            break;
        }
    }
}

void Lexer::skipComment() {
    // Skip //
    advance();
    advance();

    // Skip until end of line
    while (!isAtEnd() && current() != '\n') {
        advance();
    }
}

Common::SourceLocation Lexer::getCurrentLocation() const {
    return Common::SourceLocation(filename, line, column, position);
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme) {
    return Token(type, lexeme, getCurrentLocation());
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme, const Common::SourceLocation& loc) {
    return Token(type, lexeme, loc);
}

bool Lexer::isDigit(char c) const {
    return c >= '0' && c <= '9';
}

bool Lexer::isAlpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}

bool Lexer::isIdentifierChar(char c) const {
    return isAlphaNumeric(c) || c == '_' || c == ':';
}

Token Lexer::lexIdentifier() {
    auto startLoc = getCurrentLocation();
    std::string text;

    while (!isAtEnd() && isIdentifierChar(current())) {
        text += advance();
    }

    // Check if it's a keyword
    TokenType type = getKeywordType(text);
    Token token = makeToken(type, text, startLoc);

    if (type == TokenType::BoolLiteral) {
        token.boolValue = (text == "true");
    }

    return token;
}

Token Lexer::lexAngleBracketIdentifier() {
    auto startLoc = getCurrentLocation();
    std::string text = "<";
    advance(); // consume '<'

    // Collect the identifier - stop at whitespace, '>', or other special characters
    // This ensures <List> is tokenized as AngleBracketId but <T Constrains None> is not
    while (!isAtEnd() && current() != '>' && !isspace(current())) {
        if (current() == '\n') {
            errorReporter.reportError(
                Common::ErrorCode::UnexpectedCharacter,
                "Unterminated angle bracket identifier",
                getCurrentLocation()
            );
            return makeToken(TokenType::Invalid, text, startLoc);
        }

        // Only allow alphanumeric and underscore in angle bracket identifiers
        if (!isAlphaNumeric(current()) && current() != '_' && current() != ':') {
            break;
        }

        text += advance();
    }

    // Must have at least one character
    if (text.length() == 1) {
        errorReporter.reportError(
            Common::ErrorCode::UnexpectedCharacter,
            "Empty angle bracket identifier",
            getCurrentLocation()
        );
        return makeToken(TokenType::Invalid, text, startLoc);
    }

    // Skip any whitespace before '>'
    while (!isAtEnd() && isspace(current()) && current() != '\n') {
        advance();
    }

    if (isAtEnd() || current() != '>') {
        errorReporter.reportError(
            Common::ErrorCode::UnexpectedCharacter,
            "Expected '>' after angle bracket identifier",
            getCurrentLocation()
        );
        return makeToken(TokenType::Invalid, text, startLoc);
    }

    text += advance(); // consume '>'

    Token token = makeToken(TokenType::AngleBracketId, text, startLoc);
    // Store the identifier without brackets
    token.stringValue = text.substr(1, text.length() - 2);
    return token;
}

Token Lexer::lexNumber() {
    auto startLoc = getCurrentLocation();
    std::string text;

    while (!isAtEnd() && isDigit(current())) {
        text += advance();
    }

    // Check for integer suffix 'i'
    bool hasIntegerSuffix = false;
    if (!isAtEnd() && current() == 'i') {
        hasIntegerSuffix = true;
        text += advance();
    }

    // Check for decimal point (for future float support)
    if (!hasIntegerSuffix && !isAtEnd() && current() == '.') {
        // For now, we only support integers
        // This could be extended for float support
    }

    Token token = makeToken(TokenType::IntegerLiteral, text, startLoc);

    try {
        if (hasIntegerSuffix) {
            token.intValue = std::stoll(text.substr(0, text.length() - 1));
        } else {
            token.intValue = std::stoll(text);
        }
    } catch (const std::exception& e) {
        errorReporter.reportError(
            Common::ErrorCode::InvalidNumberLiteral,
            "Invalid integer literal: " + text,
            startLoc
        );
        token.intValue = 0;
    }

    return token;
}

Token Lexer::lexString() {
    auto startLoc = getCurrentLocation();
    std::string text = "\"";
    advance(); // consume opening "

    std::string value;

    while (!isAtEnd() && current() != '"') {
        if (current() == '\n') {
            errorReporter.reportError(
                Common::ErrorCode::UnterminatedString,
                "Unterminated string literal",
                getCurrentLocation()
            );
            return makeToken(TokenType::Invalid, text, startLoc);
        }

        // Handle escape sequences
        if (current() == '\\') {
            text += advance();
            if (!isAtEnd()) {
                char escaped = advance();
                text += escaped;
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    default: value += escaped; break;
                }
            }
        } else {
            char c = advance();
            text += c;
            value += c;
        }
    }

    if (isAtEnd()) {
        errorReporter.reportError(
            Common::ErrorCode::UnterminatedString,
            "Unterminated string literal",
            getCurrentLocation()
        );
        return makeToken(TokenType::Invalid, text, startLoc);
    }

    text += advance(); // consume closing "

    Token token = makeToken(TokenType::StringLiteral, text, startLoc);
    token.stringValue = value;
    return token;
}

Token Lexer::lexOperator() {
    auto startLoc = getCurrentLocation();
    char c = current();

    switch (c) {
        case '[': advance(); return makeToken(TokenType::LeftBracket, "[", startLoc);
        case ']': advance(); return makeToken(TokenType::RightBracket, "]", startLoc);
        case '{': advance(); return makeToken(TokenType::LeftBrace, "{", startLoc);
        case '}': advance(); return makeToken(TokenType::RightBrace, "}", startLoc);
        case '(': advance(); return makeToken(TokenType::LeftParen, "(", startLoc);
        case ')': advance(); return makeToken(TokenType::RightParen, ")", startLoc);
        case ';': advance(); return makeToken(TokenType::Semicolon, ";", startLoc);
        case ',': advance(); return makeToken(TokenType::Comma, ",", startLoc);
        case '?': advance(); return makeToken(TokenType::Question, "?", startLoc);
        case '^': advance(); return makeToken(TokenType::Caret, "^", startLoc);

        case '#': advance(); return makeToken(TokenType::Hash, "#", startLoc);

        case '.':
            advance();
            if (current() == '.') {
                advance();
                return makeToken(TokenType::Range, "..", startLoc);
            }
            return makeToken(TokenType::Dot, ".", startLoc);

        case ':':
            advance();
            if (current() == ':') {
                advance();
                return makeToken(TokenType::DoubleColon, "::", startLoc);
            }
            return makeToken(TokenType::Colon, ":", startLoc);

        case '-':
            advance();
            if (current() == '>') {
                advance();
                return makeToken(TokenType::Arrow, "->", startLoc);
            } else if (current() == '-') {
                advance();
                return makeToken(TokenType::MinusMinus, "--", startLoc);
            } else if (current() == '=') {
                advance();
                return makeToken(TokenType::MinusEquals, "-=", startLoc);
            }
            return makeToken(TokenType::Minus, "-", startLoc);

        case '+':
            advance();
            if (current() == '+') {
                advance();
                return makeToken(TokenType::PlusPlus, "++", startLoc);
            } else if (current() == '=') {
                advance();
                return makeToken(TokenType::PlusEquals, "+=", startLoc);
            }
            return makeToken(TokenType::Plus, "+", startLoc);

        case '*':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::StarEquals, "*=", startLoc);
            }
            return makeToken(TokenType::Star, "*", startLoc);

        case '/':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::SlashEquals, "/=", startLoc);
            }
            return makeToken(TokenType::Slash, "/", startLoc);

        case '%': advance(); return makeToken(TokenType::Percent, "%", startLoc);

        case '&':
            advance();
            if (current() == '&') {
                advance();
                return makeToken(TokenType::LogicalAnd, "&&", startLoc);
            }
            return makeToken(TokenType::Ampersand, "&", startLoc);

        case '|':
            advance();
            if (current() == '|') {
                advance();
                return makeToken(TokenType::LogicalOr, "||", startLoc);
            }
            return makeToken(TokenType::Pipe, "|", startLoc);

        case '=':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::DoubleEquals, "==", startLoc);
            }
            return makeToken(TokenType::Equals, "=", startLoc);

        case '!':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::NotEquals, "!=", startLoc);
            }
            return makeToken(TokenType::Exclamation, "!", startLoc);

        case '<':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::LessEquals, "<=", startLoc);
            }
            // Note: '<' for angle bracket identifiers is handled separately
            return makeToken(TokenType::LeftAngle, "<", startLoc);

        case '>':
            advance();
            if (current() == '=') {
                advance();
                return makeToken(TokenType::GreaterEquals, ">=", startLoc);
            }
            return makeToken(TokenType::RightAngle, ">", startLoc);

        default:
            advance();
            errorReporter.reportError(
                Common::ErrorCode::UnexpectedCharacter,
                std::string("Unexpected character: '") + c + "'",
                startLoc
            );
            return makeToken(TokenType::Invalid, std::string(1, c), startLoc);
    }
}

Token Lexer::nextToken() {
    skipWhitespace();

    if (isAtEnd()) {
        return makeToken(TokenType::EndOfFile, "");
    }

    char c = current();

    // Identifiers and keywords
    if (isAlpha(c)) {
        return lexIdentifier();
    }

    // Numbers
    if (isDigit(c)) {
        return lexNumber();
    }

    // String literals
    if (c == '"') {
        return lexString();
    }

    // Angle bracket identifiers
    // Only treat as angle bracket identifier if it looks like a simple identifier <Name>
    // not like a template parameter <T Constrains ...> or template argument <Type>
    if (c == '<' && isAlpha(peek())) {
        // Look ahead to check if this is a simple identifier or template syntax
        // Simple identifier: <Name> followed by space, =, or other punctuation
        // Template parameter: <T Constrains ...> (has spaces before >)
        // Template argument: <Type>^ or <Type> followed by specific chars
        size_t offset = 1;
        bool hasSpace = false;
        bool foundClosing = false;
        char charAfterClosing = '\0';

        while (position + offset < source.length()) {
            char ch = source[position + offset];
            if (ch == '>') {
                foundClosing = true;
                // Check what comes after >
                if (position + offset + 1 < source.length()) {
                    charAfterClosing = source[position + offset + 1];
                }
                break;
            }
            if (ch == ' ' || ch == '\t') {
                hasSpace = true;
                break;
            }
            if (ch == '\n') {
                break;
            }
            offset++;
        }

        // Heuristics to distinguish angle bracket identifiers from template arguments:
        // - If has space before >, it's template syntax (e.g., <T Constrains None>)
        // - If followed by ^, it's a template argument (e.g., <Integer>^)
        // - If followed by ::, it's a template argument (e.g., <Integer>::method)
        // - Otherwise, it's an angle bracket identifier
        bool isTemplateArgument = (charAfterClosing == '^' || charAfterClosing == ':');

        if (foundClosing && !hasSpace && !isTemplateArgument) {
            return lexAngleBracketIdentifier();
        }
    }

    // Operators and punctuation
    return lexOperator();
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        Token token = nextToken();
        tokens.push_back(token);

        if (token.type == TokenType::EndOfFile || token.type == TokenType::Invalid) {
            break;
        }
    }

    return tokens;
}

} // namespace Lexer
} // namespace XXML
