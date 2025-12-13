// LSPServer.h - Main LSP Server Class
// XXML Language Server Protocol Implementation

#ifndef XXML_LSP_SERVER_H
#define XXML_LSP_SERVER_H

#include "JsonRpc.h"
#include "Protocol.h"
#include "DocumentManager.h"
#include "AnalysisEngine.h"
#include <functional>
#include <unordered_map>

namespace xxml::lsp {

class LSPServer {
public:
    LSPServer();

    // Run the main message loop (blocks until exit)
    int run();

    // Stop the server
    void stop();

    // Add an include path for import resolution
    void addIncludePath(const std::string& path);

private:
    // JSON-RPC message handlers
    using RequestHandler = std::function<json(const json&)>;
    using NotificationHandler = std::function<void(const json&)>;

    // Register handlers
    void registerHandlers();

    // Process a single message
    void processMessage(const json& message);

    // Core LSP methods
    json handleInitialize(const json& params);
    void handleInitialized(const json& params);
    json handleShutdown(const json& params);
    void handleExit(const json& params);

    // Document synchronization
    void handleDidOpen(const json& params);
    void handleDidChange(const json& params);
    void handleDidClose(const json& params);
    void handleDidSave(const json& params);

    // Language features
    json handleHover(const json& params);
    json handleCompletion(const json& params);
    json handleDefinition(const json& params);
    json handleReferences(const json& params);
    json handleDocumentSymbol(const json& params);

    // Helper methods
    void publishDiagnostics(const std::string& uri);
    void analyzeDocument(const std::string& uri);

    // State
    bool initialized_ = false;
    bool shutdown_ = false;
    bool running_ = true;
    int exitCode_ = 0;

    // Components
    JsonRpcTransport transport_;
    DocumentManager documents_;
    AnalysisEngine analyzer_;

    // Handlers
    std::unordered_map<std::string, RequestHandler> requestHandlers_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;

    // Client capabilities (from initialize)
    json clientCapabilities_;

    // Configuration
    std::string stdlibPath_;
    std::string workspaceRoot_;
    std::vector<std::string> includePaths_;  // User-specified import search paths

    // Apply configuration from settings
    void applyConfiguration(const json& settings);
};

} // namespace xxml::lsp

#endif // XXML_LSP_SERVER_H
