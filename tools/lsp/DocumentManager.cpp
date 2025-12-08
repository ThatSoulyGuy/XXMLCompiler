// DocumentManager.cpp - Document State Management Implementation
// XXML Language Server Protocol Implementation

#include "DocumentManager.h"
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xxml::lsp {

const std::string DocumentManager::emptyContent_;

void DocumentState::updateLineOffsets() {
    lineOffsets.clear();
    lineOffsets.push_back(0);  // First line starts at offset 0

    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lineOffsets.push_back(i + 1);
        }
    }
}

void DocumentManager::openDocument(const std::string& uri, const std::string& content,
                                    int version, const std::string& languageId) {
    DocumentState state;
    state.uri = uri;
    state.content = content;
    state.version = version;
    state.languageId = languageId;
    state.updateLineOffsets();

    documents_[uri] = std::move(state);
}

void DocumentManager::updateDocument(const std::string& uri, const std::string& content, int version) {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second.content = content;
        it->second.version = version;
        it->second.updateLineOffsets();
    }
}

void DocumentManager::closeDocument(const std::string& uri) {
    documents_.erase(uri);
}

bool DocumentManager::hasDocument(const std::string& uri) const {
    return documents_.find(uri) != documents_.end();
}

const std::string& DocumentManager::getContent(const std::string& uri) const {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return it->second.content;
    }
    return emptyContent_;
}

const DocumentState* DocumentManager::getDocument(const std::string& uri) const {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return &it->second;
    }
    return nullptr;
}

DocumentState* DocumentManager::getDocument(const std::string& uri) {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> DocumentManager::getOpenDocuments() const {
    std::vector<std::string> result;
    result.reserve(documents_.size());
    for (const auto& [uri, _] : documents_) {
        result.push_back(uri);
    }
    return result;
}

size_t DocumentManager::positionToOffset(const std::string& uri, int line, int character) const {
    auto* doc = getDocument(uri);
    if (!doc || doc->lineOffsets.empty()) {
        return 0;
    }

    if (line < 0) line = 0;
    if (static_cast<size_t>(line) >= doc->lineOffsets.size()) {
        // Return end of document
        return doc->content.size();
    }

    size_t lineStart = doc->lineOffsets[line];
    size_t lineEnd = (static_cast<size_t>(line) + 1 < doc->lineOffsets.size())
                         ? doc->lineOffsets[line + 1]
                         : doc->content.size();

    // Clamp character to line bounds
    size_t offset = lineStart + character;
    if (offset > lineEnd) {
        offset = lineEnd;
    }

    return offset;
}

std::pair<int, int> DocumentManager::offsetToPosition(const std::string& uri, size_t offset) const {
    auto* doc = getDocument(uri);
    if (!doc || doc->lineOffsets.empty()) {
        return {0, 0};
    }

    // Binary search for the line
    auto it = std::upper_bound(doc->lineOffsets.begin(), doc->lineOffsets.end(), offset);
    if (it != doc->lineOffsets.begin()) {
        --it;
    }

    int line = static_cast<int>(it - doc->lineOffsets.begin());
    int character = static_cast<int>(offset - *it);

    return {line, character};
}

std::string DocumentManager::getTextRange(const std::string& uri, int startLine, int startChar,
                                          int endLine, int endChar) const {
    auto* doc = getDocument(uri);
    if (!doc) {
        return "";
    }

    size_t startOffset = positionToOffset(uri, startLine, startChar);
    size_t endOffset = positionToOffset(uri, endLine, endChar);

    if (startOffset >= endOffset || startOffset >= doc->content.size()) {
        return "";
    }

    return doc->content.substr(startOffset, endOffset - startOffset);
}

std::string DocumentManager::getWordAtPosition(const std::string& uri, int line, int character) const {
    auto* doc = getDocument(uri);
    if (!doc) {
        return "";
    }

    size_t offset = positionToOffset(uri, line, character);
    if (offset >= doc->content.size()) {
        return "";
    }

    // Find word boundaries
    size_t start = offset;
    size_t end = offset;

    // Move backwards to find word start
    while (start > 0) {
        char c = doc->content[start - 1];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            break;
        }
        --start;
    }

    // Move forwards to find word end
    while (end < doc->content.size()) {
        char c = doc->content[end];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            break;
        }
        ++end;
    }

    if (start >= end) {
        return "";
    }

    return doc->content.substr(start, end - start);
}

std::string DocumentManager::uriToPath(const std::string& uri) {
    // Handle file:// URIs
    const std::string prefix = "file:///";
    if (uri.compare(0, prefix.size(), prefix) == 0) {
        std::string path = uri.substr(prefix.size());

        // Decode percent-encoded characters
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                int value = 0;
                if (sscanf(path.c_str() + i + 1, "%2x", &value) == 1) {
                    decoded += static_cast<char>(value);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }

#ifdef _WIN32
        // Convert forward slashes to backslashes on Windows
        std::replace(decoded.begin(), decoded.end(), '/', '\\');
#endif

        return decoded;
    }

    return uri;
}

std::string DocumentManager::pathToUri(const std::string& path) {
    std::string uri = "file:///";

#ifdef _WIN32
    // Convert backslashes to forward slashes on Windows
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#else
    const std::string& normalized = path;
#endif

    // Percent-encode special characters
    for (char c : normalized) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '/' || c == ':' || c == '-' || c == '_' || c == '.') {
            uri += c;
        } else if (c == ' ') {
            uri += "%20";
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            uri += buf;
        }
    }

    return uri;
}

} // namespace xxml::lsp
