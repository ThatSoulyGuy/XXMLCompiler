#pragma once
#include <string>

namespace XXML {
namespace Lexer {

enum class TokenType {
    // Special tokens
    EndOfFile,
    Invalid,

    // Keywords
    Import,           // import
    Namespace,        // Namespace
    Class,            // Class
    Final,            // Final
    Extends,          // Extends
    None,             // None
    Public,           // Public
    Private,          // Private
    Protected,        // Protected
    Property,         // Property
    Types,            // Types
    NativeType,       // NativeType
    Constructor,      // Constructor
    Destructor,       // Destructor
    Default,          // default
    Method,           // Method
    Returns,          // Returns
    Parameters,       // Parameters
    Parameter,        // Parameter
    Entrypoint,       // Entrypoint
    Instantiate,      // Instantiate
    Let,              // Let
    As,               // As
    Run,              // Run
    For,              // For
    While,            // While
    If,               // If
    Else,             // Else
    Exit,             // Exit
    Return,           // Return
    Break,            // Break
    Continue,         // Continue
    Constrains,       // Constrains
    Constraint,       // Constraint
    Require,          // Require
    Truth,            // Truth
    TypeOf,           // TypeOf
    On,               // On
    Static,           // Static
    Do,               // Do
    This,             // this
    Set,              // Set
    Lambda,           // Lambda
    Compiletime,      // Compiletime
    Templates,        // Templates

    // Annotation keywords
    Annotation,       // Annotation
    Annotate,         // Annotate
    Allows,           // Allows
    Processor,        // Processor
    Retain,           // Retain
    AnnotationAllow,  // AnnotationAllow

    // FFI keywords
    NativeStructure,  // NativeStructure
    Aligns,           // Aligns
    CallbackType,     // CallbackType
    Convention,       // Convention

    // Enumeration keywords
    Enumeration,      // Enumeration
    Value,            // Value

    // Identifiers and literals
    Identifier,       // variable names, type names, etc.
    AngleBracketId,   // <identifier>
    IntegerLiteral,   // 123i
    FloatLiteral,     // 29.3842f
    DoubleLiteral,    // 37.239D
    StringLiteral,    // "hello"
    BoolLiteral,      // true, false

    // Operators and symbols
    LeftBracket,      // [
    RightBracket,     // ]
    LeftBrace,        // {
    RightBrace,       // }
    LeftParen,        // (
    RightParen,       // )
    LeftAngle,        // <
    RightAngle,       // >
    Semicolon,        // ;
    Comma,            // ,
    Dot,              // .
    Colon,            // :
    DoubleColon,      // ::
    Arrow,            // ->
    Range,            // ..
    Equals,           // =
    Plus,             // +
    Minus,            // -
    Star,             // *
    Slash,            // /
    Percent,          // %
    Ampersand,        // &
    Pipe,             // |
    Caret,            // ^
    At,               // @
    Exclamation,      // !
    Question,         // ?
    Hash,             // #
    DoubleEquals,     // ==
    NotEquals,        // !=
    LessThan,         // <
    GreaterThan,      // >
    LessEquals,       // <=
    GreaterEquals,    // >=
    LogicalAnd,       // &&
    LogicalOr,        // ||
    PlusPlus,         // ++
    MinusMinus,       // --
    PlusEquals,       // +=
    MinusEquals,      // -=
    StarEquals,       // *=
    SlashEquals,      // /=

    // Comments
    Comment           // // ...
};

std::string tokenTypeToString(TokenType type);
bool isKeyword(const std::string& text);
TokenType getKeywordType(const std::string& text);

} // namespace Lexer
} // namespace XXML
