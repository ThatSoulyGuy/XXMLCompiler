#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SourceLocation.h"

namespace XXML {
namespace Common {

enum class ErrorLevel {
    Note,
    Warning,
    Error,
    Fatal
};

enum class ErrorCode {
    // Lexer errors (1000-1999)
    UnexpectedCharacter = 1000,
    UnterminatedString,
    InvalidNumberLiteral,
    InvalidIdentifier,

    // Parser errors (2000-2999)
    UnexpectedToken = 2000,
    ExpectedToken,
    InvalidSyntax,
    MissingClosingBracket,
    MissingClosingBrace,

    // Semantic errors (3000-3999)
    UndeclaredIdentifier = 3000,
    TypeMismatch,
    InvalidOwnership,
    DuplicateDeclaration,
    DuplicateSymbol,
    UndefinedType,
    InvalidMethodCall,
    InvalidMethodDeclaration,
    ConstructorError,
    InvalidReference,
    ConstraintViolation,
    MissingArgument,
    UnknownArgument,
    InvalidAnnotationTarget,

    // CodeGen errors (4000-4999)
    CodeGenError = 4000,

    // General errors (5000-5999)
    FileNotFound = 5000,
    IOError,
    InternalError
};

class Error {
public:
    ErrorLevel level;
    ErrorCode code;
    std::string message;
    SourceLocation location;
    std::string sourceSnippet; // The actual source code line

    Error(ErrorLevel lvl, ErrorCode c, const std::string& msg, const SourceLocation& loc)
        : level(lvl), code(c), message(msg), location(loc) {}

    std::string toString() const;
    std::string getLevelString() const;
    std::string getColorCode() const;
};

class ErrorReporter {
private:
    std::vector<Error> errors;
    bool hasErrors_;
    bool hasWarnings_;

    // NEW: Track current file context for STL warning suppression
    std::string currentFile_;
    bool currentFileIsSTL_;

    // NEW: Global flag for STL warning suppression (default: suppress)
    static bool suppressSTLWarnings_;

public:
    ErrorReporter() : hasErrors_(false), hasWarnings_(false), currentFileIsSTL_(false) {}

    void reportError(ErrorCode code, const std::string& message, const SourceLocation& loc);
    void reportWarning(ErrorCode code, const std::string& message, const SourceLocation& loc);
    void reportNote(const std::string& message, const SourceLocation& loc);

    bool hasErrors() const { return hasErrors_; }
    bool hasWarnings() const { return hasWarnings_; }
    const std::vector<Error>& getErrors() const { return errors; }

    void printErrors() const;
    void clear();

    // NEW: Set/get STL warning suppression (static - affects all instances)
    static void setSuppressSTLWarnings(bool suppress);
    static bool getSuppressSTLWarnings();

    // NEW: Set current file context (for STL warning suppression)
    void setCurrentFile(const std::string& filePath, bool isSTL);
};

} // namespace Common
} // namespace XXML
