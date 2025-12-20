#pragma once

#include <string>
#include <memory>
#include <vector>
#include "../Parser/AST.h"

namespace XXML {
namespace Derive {

/**
 * Serializes AST nodes to/from JSON format for the derive API.
 *
 * This allows derives written in XXML to build AST templates using Quote blocks,
 * pass them across the DLL boundary as JSON, and have them deserialized back
 * into proper AST nodes for code generation.
 */
class ASTSerializer {
public:
    /**
     * Serialize an AST node to JSON string
     */
    static std::string serialize(const Parser::ASTNode* node);

    /**
     * Serialize an expression to JSON string
     */
    static std::string serializeExpr(const Parser::Expression* expr);

    /**
     * Serialize a statement to JSON string
     */
    static std::string serializeStmt(const Parser::Statement* stmt);

    /**
     * Serialize a vector of statements to JSON string
     */
    static std::string serializeStatements(const std::vector<std::unique_ptr<Parser::Statement>>& stmts);

    /**
     * Deserialize a JSON string back to an AST node
     */
    static std::unique_ptr<Parser::ASTNode> deserialize(const std::string& json);

    /**
     * Deserialize a JSON string to an expression
     */
    static std::unique_ptr<Parser::Expression> deserializeExpr(const std::string& json);

    /**
     * Deserialize a JSON string to a statement
     */
    static std::unique_ptr<Parser::Statement> deserializeStmt(const std::string& json);

    /**
     * Deserialize a JSON string to a vector of statements
     */
    static std::vector<std::unique_ptr<Parser::Statement>> deserializeStatements(const std::string& json);

private:
    // Helper methods for serialization
    static std::string serializeExprImpl(const Parser::Expression* expr);
    static std::string serializeStmtImpl(const Parser::Statement* stmt);
    static std::string serializeTypeRef(const Parser::TypeRef* type);

    // Helper methods for deserialization
    static std::unique_ptr<Parser::Expression> deserializeExprImpl(const std::string& json, size_t& pos);
    static std::unique_ptr<Parser::Statement> deserializeStmtImpl(const std::string& json, size_t& pos);
    static std::unique_ptr<Parser::TypeRef> deserializeTypeRef(const std::string& json, size_t& pos);

    // JSON parsing utilities
    static void skipWhitespace(const std::string& json, size_t& pos);
    static std::string parseString(const std::string& json, size_t& pos);
    static int64_t parseInt(const std::string& json, size_t& pos);
    static double parseDouble(const std::string& json, size_t& pos);
    static bool parseBool(const std::string& json, size_t& pos);
    static bool parseNull(const std::string& json, size_t& pos);
    static std::string escapeString(const std::string& s);
    static std::string unescapeString(const std::string& s);
};

} // namespace Derive
} // namespace XXML
