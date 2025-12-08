// DocumentManager.h - Document State Management
// XXML Language Server Protocol Implementation

#ifndef XXML_LSP_DOCUMENT_MANAGER_H
#define XXML_LSP_DOCUMENT_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace xxml::lsp {

// Represents an open document's state
struct DocumentState {
    std::string uri;
    std::string content;
    int version = 0;
    std::string languageId;

    // Pre-computed line offsets for fast position lookup
    std::vector<size_t> lineOffsets;

    // Update line offsets cache
    void updateLineOffsets();
};

// Manages open document state
class DocumentManager {
public:
    // Open a document
    void openDocument(const std::string& uri, const std::string& content,
                      int version = 0, const std::string& languageId = "xxml");

    // Update document content (full sync)
    void updateDocument(const std::string& uri, const std::string& content, int version);

    // Close a document
    void closeDocument(const std::string& uri);

    // Check if document is open
    bool hasDocument(const std::string& uri) const;

    // Get document content
    const std::string& getContent(const std::string& uri) const;

    // Get document state
    const DocumentState* getDocument(const std::string& uri) const;
    DocumentState* getDocument(const std::string& uri);

    // Get all open document URIs
    std::vector<std::string> getOpenDocuments() const;

    // Convert position (line, column) to offset
    size_t positionToOffset(const std::string& uri, int line, int character) const;

    // Convert offset to position (line, column)
    std::pair<int, int> offsetToPosition(const std::string& uri, size_t offset) const;

    // Get text at a range
    std::string getTextRange(const std::string& uri, int startLine, int startChar,
                             int endLine, int endChar) const;

    // Get word at position (for hover/completion context)
    std::string getWordAtPosition(const std::string& uri, int line, int character) const;

    // URI utilities
    static std::string uriToPath(const std::string& uri);
    static std::string pathToUri(const std::string& path);

private:
    std::unordered_map<std::string, DocumentState> documents_;
    static const std::string emptyContent_;
};

} // namespace xxml::lsp

#endif // XXML_LSP_DOCUMENT_MANAGER_H
