// LSPServer.cpp - Main LSP Server Implementation
// XXML Language Server Protocol Implementation

#include "LSPServer.h"
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace xxml::lsp {

// Get the directory containing the LSP executable
static std::string getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD size = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        std::string fullPath(buffer);
        size_t pos = fullPath.find_last_of("\\/");
        return (pos != std::string::npos) ? fullPath.substr(0, pos) : ".";
    }
    return ".";
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        std::string fullPath(buffer);
        size_t pos = fullPath.find_last_of('/');
        return (pos != std::string::npos) ? fullPath.substr(0, pos) : ".";
    }
    return ".";
#endif
}

LSPServer::LSPServer() {
    registerHandlers();
}

void LSPServer::registerHandlers() {
    // Lifecycle requests
    requestHandlers_["initialize"] = [this](const json& params) {
        return handleInitialize(params);
    };
    requestHandlers_["shutdown"] = [this](const json& params) {
        return handleShutdown(params);
    };

    // Lifecycle notifications
    notificationHandlers_["initialized"] = [this](const json& params) {
        handleInitialized(params);
    };
    notificationHandlers_["exit"] = [this](const json& params) {
        handleExit(params);
    };

    // Document synchronization
    notificationHandlers_["textDocument/didOpen"] = [this](const json& params) {
        handleDidOpen(params);
    };
    notificationHandlers_["textDocument/didChange"] = [this](const json& params) {
        handleDidChange(params);
    };
    notificationHandlers_["textDocument/didClose"] = [this](const json& params) {
        handleDidClose(params);
    };
    notificationHandlers_["textDocument/didSave"] = [this](const json& params) {
        handleDidSave(params);
    };

    // Configuration
    notificationHandlers_["workspace/didChangeConfiguration"] = [this](const json& params) {
        if (params.contains("settings")) {
            applyConfiguration(params["settings"]);
        }
    };

    // Language features
    requestHandlers_["textDocument/hover"] = [this](const json& params) {
        return handleHover(params);
    };
    requestHandlers_["textDocument/completion"] = [this](const json& params) {
        return handleCompletion(params);
    };
    requestHandlers_["textDocument/definition"] = [this](const json& params) {
        return handleDefinition(params);
    };
    requestHandlers_["textDocument/references"] = [this](const json& params) {
        return handleReferences(params);
    };
    requestHandlers_["textDocument/documentSymbol"] = [this](const json& params) {
        return handleDocumentSymbol(params);
    };
}

int LSPServer::run() {
    transport_.log("XXML Language Server starting...");

    while (running_) {
        auto message = transport_.readMessage();
        if (!message) {
            // EOF or error
            transport_.log("End of input, shutting down");
            break;
        }

        try {
            processMessage(*message);
        } catch (const std::exception& e) {
            transport_.log("Error processing message: " + std::string(e.what()));
        }
    }

    transport_.log("XXML Language Server stopped");
    return exitCode_;
}

void LSPServer::stop() {
    running_ = false;
}

void LSPServer::addIncludePath(const std::string& path) {
    includePaths_.push_back(path);
}

void LSPServer::processMessage(const json& message) {
    auto msg = JsonRpcMessage::fromJson(message);

    if (msg.isRequest()) {
        // Handle request
        const std::string& method = *msg.method;
        transport_.log("Request: " + method);

        // Check initialization
        if (!initialized_ && method != "initialize") {
            auto error = createErrorResponse(
                *msg.id,
                JsonRpcErrorCode::ServerNotInitialized,
                "Server not initialized"
            );
            transport_.writeMessage(error);
            return;
        }

        // Find handler
        auto it = requestHandlers_.find(method);
        if (it != requestHandlers_.end()) {
            try {
                json params = msg.params.value_or(json::object());
                json result = it->second(params);
                auto response = createSuccessResponse(*msg.id, result);
                transport_.writeMessage(response);
            } catch (const std::exception& e) {
                auto error = createErrorResponse(
                    *msg.id,
                    JsonRpcErrorCode::InternalError,
                    e.what()
                );
                transport_.writeMessage(error);
            }
        } else {
            auto error = createErrorResponse(
                *msg.id,
                JsonRpcErrorCode::MethodNotFound,
                "Method not found: " + method
            );
            transport_.writeMessage(error);
        }
    } else if (msg.isNotification()) {
        // Handle notification
        const std::string& method = *msg.method;
        transport_.log("Notification: " + method);

        auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end()) {
            try {
                json params = msg.params.value_or(json::object());
                it->second(params);
            } catch (const std::exception& e) {
                transport_.log("Error handling notification: " + std::string(e.what()));
            }
        }
        // Notifications don't get responses
    }
}

json LSPServer::handleInitialize(const json& params) {
    transport_.log("Initializing server...");

    // Store client capabilities
    if (params.contains("capabilities")) {
        clientCapabilities_ = params["capabilities"];
    }

    // Extract workspace root from rootUri or workspaceFolders
    if (params.contains("rootUri") && !params["rootUri"].is_null()) {
        workspaceRoot_ = params["rootUri"].get<std::string>();
        // Convert file URI to path
        if (workspaceRoot_.substr(0, 8) == "file:///") {
            workspaceRoot_ = workspaceRoot_.substr(8);
            // Handle Windows drive letters (file:///C:/... -> C:/...)
            #ifdef _WIN32
            // URL decode and fix path separators
            std::string decoded;
            for (size_t i = 0; i < workspaceRoot_.size(); ++i) {
                if (workspaceRoot_[i] == '%' && i + 2 < workspaceRoot_.size()) {
                    int hex = std::stoi(workspaceRoot_.substr(i + 1, 2), nullptr, 16);
                    decoded += static_cast<char>(hex);
                    i += 2;
                } else if (workspaceRoot_[i] == '/') {
                    decoded += '\\';
                } else {
                    decoded += workspaceRoot_[i];
                }
            }
            workspaceRoot_ = decoded;
            #endif
        }
        transport_.log("Workspace root: " + workspaceRoot_);
    }

    // Extract initialization options
    if (params.contains("initializationOptions")) {
        const auto& opts = params["initializationOptions"];

        if (opts.contains("stdlibPath") && opts["stdlibPath"].is_string()) {
            stdlibPath_ = opts["stdlibPath"].get<std::string>();
            transport_.log("Stdlib path from init options: " + stdlibPath_);
        }

        // Read include paths from initialization options
        if (opts.contains("includePaths") && opts["includePaths"].is_array()) {
            for (const auto& path : opts["includePaths"]) {
                if (path.is_string()) {
                    std::string includePath = path.get<std::string>();
                    includePaths_.push_back(includePath);
                    transport_.log("Include path from init options: " + includePath);
                }
            }
        }
    }

    // If no stdlib path provided, try to find it
    if (stdlibPath_.empty()) {
        namespace fs = std::filesystem;

        std::vector<fs::path> candidates;

        // First priority: relative to executable (for installed layout)
        std::string exeDir = getExecutableDirectory();
        fs::path exePath(exeDir);

        // Check next to executable (e.g., bin/Language)
        candidates.push_back(exePath / "Language");

        // Check parent of executable (installed layout: bin/xxml-lsp.exe with sibling Language/)
        candidates.push_back(exePath.parent_path() / "Language");

        // Second priority: relative to workspace (for development)
        if (!workspaceRoot_.empty()) {
            fs::path wsRoot(workspaceRoot_);
            candidates.push_back(wsRoot / "Language");
            candidates.push_back(wsRoot / "build" / "release" / "bin" / "Language");
            candidates.push_back(wsRoot / "build" / "debug" / "bin" / "Language");
        }

        for (const auto& candidate : candidates) {
            if (fs::exists(candidate) && fs::is_directory(candidate)) {
                stdlibPath_ = candidate.string();
                transport_.log("Auto-detected stdlib path: " + stdlibPath_);
                break;
            }
        }
    }

    // Apply configuration to analysis engine
    if (!stdlibPath_.empty()) {
        analyzer_.setStdlibPath(stdlibPath_);
    }
    if (!workspaceRoot_.empty()) {
        analyzer_.setWorkspaceRoot(workspaceRoot_);
    }

    // Pass command-line include paths to analyzer
    for (const auto& path : includePaths_) {
        analyzer_.addIncludePath(path);
        transport_.log("Include path: " + path);
    }

    // Return server capabilities
    InitializeResult result;
    initialized_ = true;

    return result.toJson();
}

void LSPServer::handleInitialized(const json& /* params */) {
    transport_.log("Server initialized");
}

json LSPServer::handleShutdown(const json& /* params */) {
    transport_.log("Shutdown requested");
    shutdown_ = true;
    return nullptr;  // null result
}

void LSPServer::handleExit(const json& /* params */) {
    transport_.log("Exit notification received");
    exitCode_ = shutdown_ ? 0 : 1;
    running_ = false;
}

void LSPServer::handleDidOpen(const json& params) {
    if (!params.contains("textDocument")) return;

    auto textDoc = TextDocumentItem::fromJson(params["textDocument"]);
    transport_.log("Document opened: " + textDoc.uri);

    documents_.openDocument(textDoc.uri, textDoc.text, textDoc.version, textDoc.languageId);

    // Analyze and publish diagnostics
    analyzeDocument(textDoc.uri);
}

void LSPServer::handleDidChange(const json& params) {
    if (!params.contains("textDocument")) return;

    auto textDoc = VersionedTextDocumentIdentifier::fromJson(params["textDocument"]);

    if (params.contains("contentChanges") && params["contentChanges"].is_array()) {
        for (const auto& change : params["contentChanges"]) {
            auto changeEvent = TextDocumentContentChangeEvent::fromJson(change);

            // Full document sync (we advertised change: 1)
            if (!changeEvent.range.has_value()) {
                documents_.updateDocument(textDoc.uri, changeEvent.text, textDoc.version);
            }
        }
    }

    // Re-analyze and publish diagnostics
    analyzeDocument(textDoc.uri);
}

void LSPServer::handleDidClose(const json& params) {
    if (!params.contains("textDocument")) return;

    auto textDoc = TextDocumentIdentifier::fromJson(params["textDocument"]);
    transport_.log("Document closed: " + textDoc.uri);

    documents_.closeDocument(textDoc.uri);

    // Clear diagnostics for closed document
    auto notification = createNotification("textDocument/publishDiagnostics", {
        {"uri", textDoc.uri},
        {"diagnostics", json::array()}
    });
    transport_.writeMessage(notification);
}

void LSPServer::handleDidSave(const json& params) {
    if (!params.contains("textDocument")) return;

    auto textDoc = TextDocumentIdentifier::fromJson(params["textDocument"]);
    transport_.log("Document saved: " + textDoc.uri);

    // If text is included, update content
    if (params.contains("text")) {
        auto* doc = documents_.getDocument(textDoc.uri);
        if (doc) {
            documents_.updateDocument(textDoc.uri, params["text"].get<std::string>(), doc->version);
        }
    }

    // Re-analyze on save
    analyzeDocument(textDoc.uri);
}

json LSPServer::handleHover(const json& params) {
    auto posParams = TextDocumentPositionParams::fromJson(params);
    const std::string& uri = posParams.textDocument.uri;

    // Get word at position
    std::string word = documents_.getWordAtPosition(uri, posParams.position.line, posParams.position.character);

    if (word.empty()) {
        return nullptr;  // No hover info
    }

    // TODO: Integrate with XXML semantic analyzer to get real type info
    // For now, return basic info for keywords

    std::string hoverText;

    // XXML keywords
    if (word == "Class") {
        hoverText = "**Class**\n\nDeclares a class type.\n\n```xxml\n[ Class <Name> Final Extends Base\n    ...\n]\n```";
    } else if (word == "Method") {
        hoverText = "**Method**\n\nDeclares a method.\n\n```xxml\nMethod <name> Returns Type Parameters (...) Do { ... }\n```";
    } else if (word == "Property") {
        hoverText = "**Property**\n\nDeclares a class property.\n\n```xxml\nProperty <name> Types Type^;\n```";
    } else if (word == "Constructor") {
        hoverText = "**Constructor**\n\nDeclares a constructor.\n\n```xxml\nConstructor Parameters (...) -> { ... }\n```";
    } else if (word == "Instantiate") {
        hoverText = "**Instantiate**\n\nCreates a new variable.\n\n```xxml\nInstantiate Type^ As <name> = value;\n```";
    } else if (word == "Return") {
        hoverText = "**Return**\n\nReturns a value from a method.";
    } else if (word == "If") {
        hoverText = "**If**\n\nConditional statement.\n\n```xxml\nIf (condition) -> { ... } Else -> { ... }\n```";
    } else if (word == "While") {
        hoverText = "**While**\n\nLoop statement.\n\n```xxml\nWhile (condition) -> { ... }\n```";
    } else if (word == "For") {
        hoverText = "**For**\n\nIterator loop.\n\n```xxml\nFor <item> In collection -> { ... }\n```";
    } else {
        // Not a keyword, could be identifier - would need semantic analysis
        return nullptr;
    }

    Hover hover;
    hover.contents = hoverText;
    return hover.toJson();
}

json LSPServer::handleCompletion(const json& params) {
    auto posParams = TextDocumentPositionParams::fromJson(params);
    const std::string& uri = posParams.textDocument.uri;

    auto* doc = documents_.getDocument(uri);
    if (!doc) {
        return json::array();
    }

    // Get the current line text up to cursor for context detection
    std::string currentLineText;
    int cursorLine = posParams.position.line;
    int cursorChar = posParams.position.character;

    // Extract the current line up to cursor position
    size_t lineStart = 0;
    int lineNum = 0;
    for (size_t i = 0; i < doc->content.size(); ++i) {
        if (lineNum == cursorLine) {
            // Found our line, extract text up to cursor
            size_t endPos = std::min(lineStart + static_cast<size_t>(cursorChar), doc->content.size());
            currentLineText = doc->content.substr(lineStart, endPos - lineStart);
            break;
        }
        if (doc->content[i] == '\n') {
            ++lineNum;
            lineStart = i + 1;
        }
    }

    // Use analysis engine for context-aware completions
    auto items = analyzer_.getCompletions(uri, cursorLine, cursorChar, currentLineText);

    json completions = json::array();
    for (const auto& item : items) {
        completions.push_back(item.toJson());
    }

    return completions;
}

json LSPServer::handleDefinition(const json& params) {
    auto posParams = TextDocumentPositionParams::fromJson(params);
    const std::string& uri = posParams.textDocument.uri;

    // Get word at position
    std::string word = documents_.getWordAtPosition(uri, posParams.position.line, posParams.position.character);

    if (word.empty()) {
        return nullptr;
    }

    // TODO: Integrate with XXML semantic analyzer to find actual definitions
    // This requires parsing the document and looking up symbols in the symbol table

    // For now, return null (no definition found)
    // Real implementation would:
    // 1. Parse the document
    // 2. Find the symbol at the cursor position
    // 3. Look up its definition in the symbol table
    // 4. Return the location

    return nullptr;
}

json LSPServer::handleReferences(const json& params) {
    auto posParams = TextDocumentPositionParams::fromJson(params);

    // TODO: Find all references to symbol at position
    // This requires:
    // 1. Building a reverse index of symbol usages
    // 2. Finding the symbol at cursor
    // 3. Looking up all usages

    return json::array();  // Empty array = no references found
}

json LSPServer::handleDocumentSymbol(const json& params) {
    if (!params.contains("textDocument")) {
        return json::array();
    }

    auto textDoc = TextDocumentIdentifier::fromJson(params["textDocument"]);
    auto* doc = documents_.getDocument(textDoc.uri);

    if (!doc) {
        return json::array();
    }

    // TODO: Parse document and extract symbols
    // For now, do simple regex-like scanning for class/method declarations

    json symbols = json::array();
    const std::string& content = doc->content;

    // Simple scanning for [ Class <Name>
    size_t pos = 0;
    while ((pos = content.find("[ Class <", pos)) != std::string::npos) {
        size_t nameStart = pos + 9;  // After "[ Class <"
        size_t nameEnd = content.find(">", nameStart);
        if (nameEnd != std::string::npos) {
            std::string className = content.substr(nameStart, nameEnd - nameStart);

            auto [line, col] = documents_.offsetToPosition(textDoc.uri, pos);

            DocumentSymbol sym;
            sym.name = className;
            sym.kind = SymbolKind::Class;
            sym.range = Range{Position{line, col}, Position{line, col + static_cast<int>(className.size())}};
            sym.selectionRange = sym.range;

            symbols.push_back(sym.toJson());
        }
        pos = nameEnd != std::string::npos ? nameEnd : pos + 1;
    }

    // Scan for Method <Name>
    pos = 0;
    while ((pos = content.find("Method <", pos)) != std::string::npos) {
        size_t nameStart = pos + 8;  // After "Method <"
        size_t nameEnd = content.find(">", nameStart);
        if (nameEnd != std::string::npos) {
            std::string methodName = content.substr(nameStart, nameEnd - nameStart);

            auto [line, col] = documents_.offsetToPosition(textDoc.uri, pos);

            DocumentSymbol sym;
            sym.name = methodName;
            sym.kind = SymbolKind::Method;
            sym.range = Range{Position{line, col}, Position{line, col + static_cast<int>(methodName.size())}};
            sym.selectionRange = sym.range;

            symbols.push_back(sym.toJson());
        }
        pos = nameEnd != std::string::npos ? nameEnd : pos + 1;
    }

    return symbols;
}

void LSPServer::analyzeDocument(const std::string& uri) {
    auto* doc = documents_.getDocument(uri);
    if (!doc) return;

    // Run analysis using the analysis engine
    analyzer_.analyze(uri, doc->content);

    // Publish diagnostics
    publishDiagnostics(uri);
}

void LSPServer::publishDiagnostics(const std::string& uri) {
    json diagnosticsJson = json::array();

    // Get diagnostics from analysis engine
    auto* result = analyzer_.getAnalysis(uri);
    if (result) {
        for (const auto& diag : result->diagnostics) {
            diagnosticsJson.push_back(diag.toJson());
        }
    }

    auto notification = createNotification("textDocument/publishDiagnostics", {
        {"uri", uri},
        {"diagnostics", diagnosticsJson}
    });
    transport_.writeMessage(notification);
}

void LSPServer::applyConfiguration(const json& settings) {
    transport_.log("Applying configuration...");

    // Extract xxml settings
    if (settings.contains("xxml")) {
        const auto& xxmlSettings = settings["xxml"];

        // Stdlib path
        if (xxmlSettings.contains("stdlibPath") && xxmlSettings["stdlibPath"].is_string()) {
            stdlibPath_ = xxmlSettings["stdlibPath"].get<std::string>();
            analyzer_.setStdlibPath(stdlibPath_);
            transport_.log("Updated stdlib path: " + stdlibPath_);
        }
    }

    // Re-analyze all open documents with new configuration
    // This would require storing list of open documents and re-analyzing them
}

} // namespace xxml::lsp
