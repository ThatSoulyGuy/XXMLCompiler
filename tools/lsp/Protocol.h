// Protocol.h - LSP Protocol Message Types
// XXML Language Server Protocol Implementation

#ifndef XXML_LSP_PROTOCOL_H
#define XXML_LSP_PROTOCOL_H

#include <string>
#include <vector>
#include <optional>
#include "third_party/nlohmann/json.hpp"

namespace xxml::lsp {

using json = nlohmann::json;

// Position in a text document (0-indexed)
struct Position {
    int line = 0;
    int character = 0;

    json toJson() const {
        return {{"line", line}, {"character", character}};
    }

    static Position fromJson(const json& j) {
        Position p;
        p.line = j.value("line", 0);
        p.character = j.value("character", 0);
        return p;
    }
};

// Range in a text document
struct Range {
    Position start;
    Position end;

    json toJson() const {
        return {{"start", start.toJson()}, {"end", end.toJson()}};
    }

    static Range fromJson(const json& j) {
        Range r;
        if (j.contains("start")) r.start = Position::fromJson(j["start"]);
        if (j.contains("end")) r.end = Position::fromJson(j["end"]);
        return r;
    }
};

// Location represents a location in a document
struct Location {
    std::string uri;
    Range range;

    json toJson() const {
        return {{"uri", uri}, {"range", range.toJson()}};
    }

    static Location fromJson(const json& j) {
        Location loc;
        loc.uri = j.value("uri", "");
        if (j.contains("range")) loc.range = Range::fromJson(j["range"]);
        return loc;
    }
};

// Diagnostic severity levels
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

// Diagnostic message
struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string source = "xxml";
    std::string message;

    json toJson() const {
        json j;
        j["range"] = range.toJson();
        j["severity"] = static_cast<int>(severity);
        if (!code.empty()) j["code"] = code;
        j["source"] = source;
        j["message"] = message;
        return j;
    }
};

// Text document identifier
struct TextDocumentIdentifier {
    std::string uri;

    static TextDocumentIdentifier fromJson(const json& j) {
        TextDocumentIdentifier id;
        id.uri = j.value("uri", "");
        return id;
    }
};

// Versioned text document identifier
struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
    int version = 0;

    static VersionedTextDocumentIdentifier fromJson(const json& j) {
        VersionedTextDocumentIdentifier id;
        id.uri = j.value("uri", "");
        id.version = j.value("version", 0);
        return id;
    }
};

// Text document item (for didOpen)
struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string text;

    static TextDocumentItem fromJson(const json& j) {
        TextDocumentItem item;
        item.uri = j.value("uri", "");
        item.languageId = j.value("languageId", "");
        item.version = j.value("version", 0);
        item.text = j.value("text", "");
        return item;
    }
};

// Content change event
struct TextDocumentContentChangeEvent {
    std::optional<Range> range;
    std::string text;

    static TextDocumentContentChangeEvent fromJson(const json& j) {
        TextDocumentContentChangeEvent event;
        if (j.contains("range")) {
            event.range = Range::fromJson(j["range"]);
        }
        event.text = j.value("text", "");
        return event;
    }
};

// Text document position params
struct TextDocumentPositionParams {
    TextDocumentIdentifier textDocument;
    Position position;

    static TextDocumentPositionParams fromJson(const json& j) {
        TextDocumentPositionParams params;
        if (j.contains("textDocument")) {
            params.textDocument = TextDocumentIdentifier::fromJson(j["textDocument"]);
        }
        if (j.contains("position")) {
            params.position = Position::fromJson(j["position"]);
        }
        return params;
    }
};

// Completion item kinds
enum class CompletionItemKind {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25
};

// Insert text format
enum class InsertTextFormat {
    PlainText = 1,
    Snippet = 2
};

// Completion item
struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail;
    std::string documentation;
    std::string insertText;
    InsertTextFormat insertTextFormat = InsertTextFormat::PlainText;

    json toJson() const {
        json j;
        j["label"] = label;
        j["kind"] = static_cast<int>(kind);
        if (!detail.empty()) j["detail"] = detail;
        if (!documentation.empty()) j["documentation"] = documentation;
        if (!insertText.empty()) {
            j["insertText"] = insertText;
            j["insertTextFormat"] = static_cast<int>(insertTextFormat);
        }
        return j;
    }
};

// Hover content
struct Hover {
    std::string contents;  // Markdown content
    std::optional<Range> range;

    json toJson() const {
        json j;
        j["contents"] = {{"kind", "markdown"}, {"value", contents}};
        if (range) j["range"] = range->toJson();
        return j;
    }
};

// Symbol kinds
enum class SymbolKind {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26
};

// Document symbol
struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    std::vector<DocumentSymbol> children;

    json toJson() const {
        json j;
        j["name"] = name;
        if (!detail.empty()) j["detail"] = detail;
        j["kind"] = static_cast<int>(kind);
        j["range"] = range.toJson();
        j["selectionRange"] = selectionRange.toJson();
        if (!children.empty()) {
            json childrenJson = json::array();
            for (const auto& child : children) {
                childrenJson.push_back(child.toJson());
            }
            j["children"] = childrenJson;
        }
        return j;
    }
};

// Server capabilities
struct ServerCapabilities {
    json toJson() const {
        return {
            {"textDocumentSync", {
                {"openClose", true},
                {"change", 1},  // Full sync
                {"save", {{"includeText", true}}}
            }},
            {"hoverProvider", true},
            {"completionProvider", {
                {"triggerCharacters", {".", ":", "<"}}
            }},
            {"definitionProvider", true},
            {"referencesProvider", true},
            {"documentSymbolProvider", true}
        };
    }
};

// Initialize result
struct InitializeResult {
    ServerCapabilities capabilities;

    json toJson() const {
        return {
            {"capabilities", capabilities.toJson()},
            {"serverInfo", {
                {"name", "xxml-lsp"},
                {"version", "0.1.0"}
            }}
        };
    }
};

} // namespace xxml::lsp

#endif // XXML_LSP_PROTOCOL_H
