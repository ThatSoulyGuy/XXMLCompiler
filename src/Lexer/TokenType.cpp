#include "../../include/Lexer/TokenType.h"
#include <unordered_map>

namespace XXML {
namespace Lexer {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"import", TokenType::Import},
    {"Namespace", TokenType::Namespace},
    {"Class", TokenType::Class},
    {"Structure", TokenType::Structure},
    {"Final", TokenType::Final},
    {"Extends", TokenType::Extends},
    {"None", TokenType::None},
    {"Public", TokenType::Public},
    {"Private", TokenType::Private},
    {"Protected", TokenType::Protected},
    {"Property", TokenType::Property},
    {"Types", TokenType::Types},
    {"NativeType", TokenType::NativeType},
    {"Constructor", TokenType::Constructor},
    {"Destructor", TokenType::Destructor},
    {"default", TokenType::Default},
    {"Method", TokenType::Method},
    {"Returns", TokenType::Returns},
    {"Parameters", TokenType::Parameters},
    {"Parameter", TokenType::Parameter},
    {"Entrypoint", TokenType::Entrypoint},
    {"Instantiate", TokenType::Instantiate},
    {"As", TokenType::As},
    {"Run", TokenType::Run},
    {"For", TokenType::For},
    {"While", TokenType::While},
    {"If", TokenType::If},
    {"Else", TokenType::Else},
    {"Exit", TokenType::Exit},
    {"Return", TokenType::Return},
    {"Break", TokenType::Break},
    {"Continue", TokenType::Continue},
    {"Constrains", TokenType::Constrains},
    {"Constraint", TokenType::Constraint},
    {"Require", TokenType::Require},
    {"Truth", TokenType::Truth},
    {"TypeOf", TokenType::TypeOf},
    {"On", TokenType::On},
    {"Static", TokenType::Static},
    {"Do", TokenType::Do},
    {"this", TokenType::This},
    {"Set", TokenType::Set},
    {"Lambda", TokenType::Lambda},
    {"Compiletime", TokenType::Compiletime},
    {"Templates", TokenType::Templates},
    {"Annotation", TokenType::Annotation},
    {"Annotate", TokenType::Annotate},
    {"Allows", TokenType::Allows},
    {"Processor", TokenType::Processor},
    {"Retain", TokenType::Retain},
    {"AnnotationAllow", TokenType::AnnotationAllow},
    {"Derive", TokenType::Derive},
    {"NativeStructure", TokenType::NativeStructure},
    {"Aligns", TokenType::Aligns},
    {"CallbackType", TokenType::CallbackType},
    {"Convention", TokenType::Convention},
    {"Enumeration", TokenType::Enumeration},
    {"Value", TokenType::Value},
    {"true", TokenType::BoolLiteral},
    {"false", TokenType::BoolLiteral}
};

std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EndOfFile";
        case TokenType::Invalid: return "Invalid";
        case TokenType::Import: return "Import";
        case TokenType::Namespace: return "Namespace";
        case TokenType::Class: return "Class";
        case TokenType::Structure: return "Structure";
        case TokenType::Final: return "Final";
        case TokenType::Extends: return "Extends";
        case TokenType::None: return "None";
        case TokenType::Public: return "Public";
        case TokenType::Private: return "Private";
        case TokenType::Protected: return "Protected";
        case TokenType::Property: return "Property";
        case TokenType::Types: return "Types";
        case TokenType::NativeType: return "NativeType";
        case TokenType::Constructor: return "Constructor";
        case TokenType::Destructor: return "Destructor";
        case TokenType::Default: return "Default";
        case TokenType::Method: return "Method";
        case TokenType::Returns: return "Returns";
        case TokenType::Parameters: return "Parameters";
        case TokenType::Parameter: return "Parameter";
        case TokenType::Entrypoint: return "Entrypoint";
        case TokenType::Instantiate: return "Instantiate";
        case TokenType::As: return "As";
        case TokenType::Run: return "Run";
        case TokenType::For: return "For";
        case TokenType::While: return "While";
        case TokenType::If: return "If";
        case TokenType::Else: return "Else";
        case TokenType::Exit: return "Exit";
        case TokenType::Return: return "Return";
        case TokenType::Break: return "Break";
        case TokenType::Continue: return "Continue";
        case TokenType::Constrains: return "Constrains";
        case TokenType::Constraint: return "Constraint";
        case TokenType::Require: return "Require";
        case TokenType::Truth: return "Truth";
        case TokenType::TypeOf: return "TypeOf";
        case TokenType::On: return "On";
        case TokenType::Static: return "Static";
        case TokenType::Do: return "Do";
        case TokenType::This: return "This";
        case TokenType::Set: return "Set";
        case TokenType::Lambda: return "Lambda";
        case TokenType::Compiletime: return "Compiletime";
        case TokenType::Templates: return "Templates";
        case TokenType::Annotation: return "Annotation";
        case TokenType::Annotate: return "Annotate";
        case TokenType::Allows: return "Allows";
        case TokenType::Processor: return "Processor";
        case TokenType::Retain: return "Retain";
        case TokenType::AnnotationAllow: return "AnnotationAllow";
        case TokenType::Derive: return "Derive";
        case TokenType::NativeStructure: return "NativeStructure";
        case TokenType::Aligns: return "Aligns";
        case TokenType::CallbackType: return "CallbackType";
        case TokenType::Convention: return "Convention";
        case TokenType::Enumeration: return "Enumeration";
        case TokenType::Value: return "Value";
        case TokenType::Identifier: return "Identifier";
        case TokenType::AngleBracketId: return "AngleBracketId";
        case TokenType::IntegerLiteral: return "IntegerLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::DoubleLiteral: return "DoubleLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::BoolLiteral: return "BoolLiteral";
        case TokenType::LeftBracket: return "LeftBracket";
        case TokenType::RightBracket: return "RightBracket";
        case TokenType::LeftBrace: return "LeftBrace";
        case TokenType::RightBrace: return "RightBrace";
        case TokenType::LeftParen: return "LeftParen";
        case TokenType::RightParen: return "RightParen";
        case TokenType::LeftAngle: return "LeftAngle";
        case TokenType::RightAngle: return "RightAngle";
        case TokenType::Semicolon: return "Semicolon";
        case TokenType::Comma: return "Comma";
        case TokenType::Dot: return "Dot";
        case TokenType::Colon: return "Colon";
        case TokenType::DoubleColon: return "DoubleColon";
        case TokenType::Arrow: return "Arrow";
        case TokenType::Range: return "Range";
        case TokenType::Equals: return "Equals";
        case TokenType::Plus: return "Plus";
        case TokenType::Minus: return "Minus";
        case TokenType::Star: return "Star";
        case TokenType::Slash: return "Slash";
        case TokenType::Percent: return "Percent";
        case TokenType::Ampersand: return "Ampersand";
        case TokenType::Pipe: return "Pipe";
        case TokenType::Caret: return "Caret";
        case TokenType::At: return "At";
        case TokenType::Exclamation: return "Exclamation";
        case TokenType::Question: return "Question";
        case TokenType::Hash: return "Hash";
        case TokenType::DoubleEquals: return "DoubleEquals";
        case TokenType::NotEquals: return "NotEquals";
        case TokenType::LessThan: return "LessThan";
        case TokenType::GreaterThan: return "GreaterThan";
        case TokenType::LessEquals: return "LessEquals";
        case TokenType::GreaterEquals: return "GreaterEquals";
        case TokenType::LogicalAnd: return "LogicalAnd";
        case TokenType::LogicalOr: return "LogicalOr";
        case TokenType::PlusPlus: return "PlusPlus";
        case TokenType::MinusMinus: return "MinusMinus";
        case TokenType::PlusEquals: return "PlusEquals";
        case TokenType::MinusEquals: return "MinusEquals";
        case TokenType::StarEquals: return "StarEquals";
        case TokenType::SlashEquals: return "SlashEquals";
        case TokenType::Comment: return "Comment";
        default: return "Unknown";
    }
}

bool isKeyword(const std::string& text) {
    return keywords.find(text) != keywords.end();
}

TokenType getKeywordType(const std::string& text) {
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenType::Identifier;
}

} // namespace Lexer
} // namespace XXML
