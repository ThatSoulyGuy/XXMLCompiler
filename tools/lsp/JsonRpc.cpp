// JsonRpc.cpp - JSON-RPC Protocol Implementation
// XXML Language Server Protocol Implementation

#include "JsonRpc.h"
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace xxml::lsp {

json JsonRpcMessage::toJson() const {
    json j;
    j["jsonrpc"] = jsonrpc;

    if (id.has_value()) {
        j["id"] = *id;
    }

    if (method.has_value()) {
        j["method"] = *method;
    }

    if (params.has_value()) {
        j["params"] = *params;
    }

    if (result.has_value()) {
        j["result"] = *result;
    }

    if (error.has_value()) {
        j["error"] = *error;
    }

    return j;
}

JsonRpcMessage JsonRpcMessage::fromJson(const json& j) {
    JsonRpcMessage msg;

    if (j.contains("jsonrpc")) {
        msg.jsonrpc = j["jsonrpc"].get<std::string>();
    }

    if (j.contains("id")) {
        msg.id = j["id"];
    }

    if (j.contains("method")) {
        msg.method = j["method"].get<std::string>();
    }

    if (j.contains("params")) {
        msg.params = j["params"];
    }

    if (j.contains("result")) {
        msg.result = j["result"];
    }

    if (j.contains("error")) {
        msg.error = j["error"];
    }

    return msg;
}

json createErrorResponse(const json& id, JsonRpcErrorCode code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", static_cast<int>(code)},
            {"message", message}
        }}
    };
}

json createSuccessResponse(const json& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

json createNotification(const std::string& method, const json& params) {
    return {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
}

std::optional<size_t> JsonRpcTransport::readContentLength() {
    std::string line;
    size_t contentLength = 0;
    bool foundContentLength = false;

    // Read headers until empty line
    while (true) {
        int c = std::getchar();
        if (c == EOF) {
            return std::nullopt;
        }

        if (c == '\r') {
            c = std::getchar();  // Consume \n
            if (c == EOF) {
                return std::nullopt;
            }

            if (line.empty()) {
                // Empty line signals end of headers
                break;
            }

            // Parse Content-Length header
            const std::string prefix = "Content-Length: ";
            if (line.compare(0, prefix.size(), prefix) == 0) {
                try {
                    contentLength = std::stoull(line.substr(prefix.size()));
                    foundContentLength = true;
                } catch (...) {
                    return std::nullopt;
                }
            }

            line.clear();
        } else if (c != '\n') {
            line += static_cast<char>(c);
        }
    }

    if (!foundContentLength) {
        return std::nullopt;
    }

    return contentLength;
}

std::string JsonRpcTransport::readBytes(size_t n) {
    std::string result;
    result.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        int c = std::getchar();
        if (c == EOF) {
            break;
        }
        result += static_cast<char>(c);
    }

    return result;
}

std::optional<json> JsonRpcTransport::readMessage() {
#ifdef _WIN32
    // Set stdin to binary mode on Windows to prevent CR/LF translation
    static bool initialized = false;
    if (!initialized) {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        initialized = true;
    }
#endif

    auto contentLength = readContentLength();
    if (!contentLength) {
        return std::nullopt;
    }

    std::string content = readBytes(*contentLength);
    if (content.size() != *contentLength) {
        log("Error: Failed to read full message content");
        return std::nullopt;
    }

    try {
        return json::parse(content);
    } catch (const json::parse_error& e) {
        log("Error parsing JSON: " + std::string(e.what()));
        return std::nullopt;
    }
}

void JsonRpcTransport::writeMessage(const json& message) {
    std::string content = message.dump();

    // Write headers
    std::cout << "Content-Length: " << content.size() << "\r\n";
    std::cout << "\r\n";

    // Write content
    std::cout << content;
    std::cout.flush();
}

void JsonRpcTransport::log(const std::string& message) {
    std::cerr << "[xxml-lsp] " << message << std::endl;
}

} // namespace xxml::lsp
