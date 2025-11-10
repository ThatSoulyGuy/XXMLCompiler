#include "../../include/Lexer/Token.h"

namespace XXML {
namespace Lexer {

bool Token::isKeyword() const {
    return type >= TokenType::Import && type <= TokenType::Continue;
}

bool Token::isOperator() const {
    return type >= TokenType::LeftBracket && type <= TokenType::SlashEquals;
}

bool Token::isLiteral() const {
    return type == TokenType::IntegerLiteral ||
           type == TokenType::StringLiteral ||
           type == TokenType::BoolLiteral;
}

std::string Token::toString() const {
    std::string result = tokenTypeToString(type);
    result += " '" + lexeme + "'";
    if (type == TokenType::IntegerLiteral) {
        result += " (" + std::to_string(intValue) + ")";
    } else if (type == TokenType::StringLiteral) {
        result += " (\"" + stringValue + "\")";
    } else if (type == TokenType::BoolLiteral) {
        result += " (" + std::string(boolValue ? "true" : "false") + ")";
    }
    result += " at " + location.toString();
    return result;
}

} // namespace Lexer
} // namespace XXML
