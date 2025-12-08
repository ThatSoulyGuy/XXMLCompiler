// JsonRpc.h - JSON-RPC Protocol Handling for LSP
// XXML Language Server Protocol Implementation

#ifndef XXML_LSP_JSONRPC_H
#define XXML_LSP_JSONRPC_H

#include <string>
#include <optional>
#include <iostream>
#include "third_party/nlohmann/json.hpp"

namespace xxml::lsp {

using json = nlohmann::json;

// JSON-RPC message types
struct JsonRpcMessage {
    std::string jsonrpc = "2.0";
    std::optional<json> id;         // null for notifications
    std::optional<std::string> method;
    std::optional<json> params;
    std::optional<json> result;
    std::optional<json> error;

    bool isRequest() const {
        return method.has_value() && id.has_value();
    }

    bool isNotification() const {
        return method.has_value() && !id.has_value();
    }

    bool isResponse() const {
        return !method.has_value() && (result.has_value() || error.has_value());
    }

    json toJson() const;
    static JsonRpcMessage fromJson(const json& j);
};

// JSON-RPC error codes
enum class JsonRpcErrorCode {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,

    // LSP specific errors
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
    RequestCancelled = -32800,
    ContentModified = -32801
};

// Create error response
json createErrorResponse(const json& id, JsonRpcErrorCode code, const std::string& message);

// Create success response
json createSuccessResponse(const json& id, const json& result);

// Create notification
json createNotification(const std::string& method, const json& params);

// JSON-RPC transport over stdio
class JsonRpcTransport {
public:
    // Read a message from stdin
    // Returns std::nullopt on EOF or error
    std::optional<json> readMessage();

    // Write a message to stdout
    void writeMessage(const json& message);

    // Log to stderr (for debugging)
    void log(const std::string& message);

private:
    // Read Content-Length header
    std::optional<size_t> readContentLength();

    // Read exactly n bytes
    std::string readBytes(size_t n);
};

} // namespace xxml::lsp

#endif // XXML_LSP_JSONRPC_H
