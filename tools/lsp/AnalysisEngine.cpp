// AnalysisEngine.cpp - XXML Semantic Analysis Integration
// XXML Language Server Protocol Implementation

#include "AnalysisEngine.h"
#include <algorithm>
#include <sstream>
#include <filesystem>

namespace xxml::lsp {

// AnalysisResult methods
void AnalysisResult::computeLineOffsets() {
    lineOffsets.clear();
    lineOffsets.push_back(0);
    for (size_t i = 0; i < sourceContent.size(); ++i) {
        if (sourceContent[i] == '\n') {
            lineOffsets.push_back(i + 1);
        }
    }
}

std::pair<int, int> AnalysisResult::offsetToPosition(size_t offset) const {
    if (lineOffsets.empty()) return {0, 0};

    auto it = std::upper_bound(lineOffsets.begin(), lineOffsets.end(), offset);
    if (it != lineOffsets.begin()) --it;

    int line = static_cast<int>(it - lineOffsets.begin());
    int character = static_cast<int>(offset - *it);
    return {line, character};
}

size_t AnalysisResult::positionToOffset(int line, int character) const {
    if (line < 0) line = 0;
    if (static_cast<size_t>(line) >= lineOffsets.size()) {
        return sourceContent.size();
    }
    size_t lineStart = lineOffsets[line];
    return lineStart + character;
}

// AnalysisEngine methods
AnalysisEngine::AnalysisEngine() {
    // Try to detect stdlib path
    std::filesystem::path exePath = std::filesystem::current_path();
    if (std::filesystem::exists(exePath / "Language")) {
        stdlibPath_ = (exePath / "Language").string();
    } else if (std::filesystem::exists(exePath / ".." / "Language")) {
        stdlibPath_ = (exePath / ".." / "Language").string();
    }
}

AnalysisEngine::~AnalysisEngine() = default;

const AnalysisResult* AnalysisEngine::analyze(const std::string& uri, const std::string& content) {
    auto result = std::make_unique<AnalysisResult>();
    result->sourceContent = content;
    result->computeLineOffsets();

    // Create error reporter
    result->errorReporter = std::make_unique<XXML::Common::ErrorReporter>();

    // Create compilation context
    result->context = std::make_unique<XXML::Core::CompilationContext>();

    try {
        // Step 1: Lexical analysis
        // Extract filename from URI for error reporting
        std::string filename = uri;
        size_t lastSlash = filename.rfind('/');
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }

        XXML::Lexer::Lexer lexer(content, filename, *result->errorReporter);
        auto tokens = lexer.tokenize();

        // Check for lexer errors
        if (result->errorReporter->hasErrors()) {
            for (const auto& error : result->errorReporter->getErrors()) {
                result->diagnostics.push_back(convertError(error, *result));
            }
            cache_[uri] = std::move(result);
            return cache_[uri].get();
        }

        // Step 2: Parsing
        XXML::Parser::Parser parser(tokens, *result->errorReporter);
        result->ast = parser.parse();

        // Collect parse errors
        for (const auto& error : result->errorReporter->getErrors()) {
            result->diagnostics.push_back(convertError(error, *result));
        }

        if (!result->ast || result->errorReporter->hasErrors()) {
            result->parseSuccess = false;
            cache_[uri] = std::move(result);
            return cache_[uri].get();
        }

        result->parseSuccess = true;

        // Step 3: Semantic analysis
        result->errorReporter->clear();
        result->analyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(
            *result->context, *result->errorReporter);

        // Run semantic analysis
        result->analyzer->analyze(*result->ast);

        // Collect semantic errors
        for (const auto& error : result->errorReporter->getErrors()) {
            result->diagnostics.push_back(convertError(error, *result));
        }

        result->semanticSuccess = !result->errorReporter->hasErrors();

    } catch (const std::exception& e) {
        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.message = std::string("Internal compiler error: ") + e.what();
        diag.range.start = {0, 0};
        diag.range.end = {0, 1};
        result->diagnostics.push_back(diag);
    }

    cache_[uri] = std::move(result);
    return cache_[uri].get();
}

const AnalysisResult* AnalysisEngine::getAnalysis(const std::string& uri) const {
    auto it = cache_.find(uri);
    if (it != cache_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void AnalysisEngine::clearAnalysis(const std::string& uri) {
    cache_.erase(uri);
}

void AnalysisEngine::clearAll() {
    cache_.clear();
}

Diagnostic AnalysisEngine::convertError(const XXML::Common::Error& error,
                                         const AnalysisResult& result) const {
    Diagnostic diag;

    // Convert error level to severity
    switch (error.level) {
        case XXML::Common::ErrorLevel::Error:
        case XXML::Common::ErrorLevel::Fatal:
            diag.severity = DiagnosticSeverity::Error;
            break;
        case XXML::Common::ErrorLevel::Warning:
            diag.severity = DiagnosticSeverity::Warning;
            break;
        case XXML::Common::ErrorLevel::Note:
            diag.severity = DiagnosticSeverity::Information;
            break;
    }

    diag.message = error.message;
    diag.code = std::to_string(static_cast<int>(error.code));

    // Convert source location to range
    // XXML uses 1-based line numbers, LSP uses 0-based
    int line = error.location.line > 0 ? error.location.line - 1 : 0;
    int col = error.location.column > 0 ? error.location.column - 1 : 0;

    diag.range.start.line = line;
    diag.range.start.character = col;
    diag.range.end.line = line;
    diag.range.end.character = col + 1;

    // Try to extend range to end of token/word
    if (!result.sourceContent.empty()) {
        size_t offset = result.positionToOffset(line, col);
        if (offset < result.sourceContent.size()) {
            size_t end = offset;
            while (end < result.sourceContent.size() &&
                   (std::isalnum(result.sourceContent[end]) || result.sourceContent[end] == '_')) {
                ++end;
            }
            if (end > offset) {
                auto [endLine, endCol] = result.offsetToPosition(end);
                diag.range.end.line = endLine;
                diag.range.end.character = endCol;
            }
        }
    }

    return diag;
}

Location AnalysisEngine::convertSourceLocation(const std::string& uri,
                                                const XXML::Common::SourceLocation& loc,
                                                const AnalysisResult& result) const {
    Location location;
    location.uri = uri;

    // XXML uses 1-based, LSP uses 0-based
    int line = loc.line > 0 ? loc.line - 1 : 0;
    int col = loc.column > 0 ? loc.column - 1 : 0;

    location.range.start.line = line;
    location.range.start.character = col;
    location.range.end.line = line;
    location.range.end.character = col + 1;

    return location;
}

std::optional<SymbolLocationInfo> AnalysisEngine::findSymbolAtPosition(
    const std::string& uri, int line, int character) const {

    auto* result = getAnalysis(uri);
    if (!result || !result->analyzer) return std::nullopt;

    // Get word at position
    size_t offset = result->positionToOffset(line, character);
    if (offset >= result->sourceContent.size()) return std::nullopt;

    // Find word boundaries
    size_t start = offset;
    size_t end = offset;

    while (start > 0 && (std::isalnum(result->sourceContent[start - 1]) ||
                         result->sourceContent[start - 1] == '_')) {
        --start;
    }
    while (end < result->sourceContent.size() &&
           (std::isalnum(result->sourceContent[end]) || result->sourceContent[end] == '_')) {
        ++end;
    }

    if (start >= end) return std::nullopt;

    std::string word = result->sourceContent.substr(start, end - start);

    // Look up symbol in symbol table
    auto& symbolTable = result->context->symbolTable();
    auto* symbol = symbolTable.resolve(word);

    if (!symbol) return std::nullopt;

    SymbolLocationInfo info;
    info.name = symbol->name;
    info.qualifiedName = symbol->name;  // TODO: Get full qualified name
    info.typeName = symbol->typeName;

    // Determine kind
    switch (symbol->kind) {
        case XXML::Semantic::SymbolKind::Class:
            info.kind = "class";
            break;
        case XXML::Semantic::SymbolKind::Method:
            info.kind = "method";
            break;
        case XXML::Semantic::SymbolKind::Property:
            info.kind = "property";
            break;
        case XXML::Semantic::SymbolKind::LocalVariable:
        case XXML::Semantic::SymbolKind::Parameter:
            info.kind = "variable";
            break;
        case XXML::Semantic::SymbolKind::Constructor:
            info.kind = "constructor";
            break;
        case XXML::Semantic::SymbolKind::Namespace:
            info.kind = "namespace";
            break;
        default:
            info.kind = "symbol";
    }

    // Get ownership modifier
    switch (symbol->ownership) {
        case XXML::Parser::OwnershipType::Owned:
            info.ownershipModifier = "^";
            break;
        case XXML::Parser::OwnershipType::Reference:
            info.ownershipModifier = "&";
            break;
        case XXML::Parser::OwnershipType::Copy:
            info.ownershipModifier = "%";
            break;
        default:
            info.ownershipModifier = "";
    }

    info.location = convertSourceLocation(uri, symbol->location, *result);

    return info;
}

std::optional<Location> AnalysisEngine::findDefinition(
    const std::string& uri, int line, int character) const {

    auto symbolInfo = findSymbolAtPosition(uri, line, character);
    if (!symbolInfo) return std::nullopt;

    return symbolInfo->location;
}

std::vector<Location> AnalysisEngine::findReferences(
    const std::string& uri, int line, int character) const {

    std::vector<Location> references;

    auto* result = getAnalysis(uri);
    if (!result || !result->ast) return references;

    auto symbolInfo = findSymbolAtPosition(uri, line, character);
    if (!symbolInfo) return references;

    // Search through source content for all occurrences of the symbol name
    const std::string& name = symbolInfo->name;
    const std::string& content = result->sourceContent;

    size_t pos = 0;
    while ((pos = content.find(name, pos)) != std::string::npos) {
        // Check if it's a whole word (not part of a larger identifier)
        bool isWordStart = (pos == 0 || (!std::isalnum(content[pos - 1]) && content[pos - 1] != '_'));
        bool isWordEnd = (pos + name.size() >= content.size() ||
                          (!std::isalnum(content[pos + name.size()]) && content[pos + name.size()] != '_'));

        if (isWordStart && isWordEnd) {
            auto [refLine, refCol] = result->offsetToPosition(pos);

            Location loc;
            loc.uri = uri;
            loc.range.start.line = refLine;
            loc.range.start.character = refCol;
            loc.range.end.line = refLine;
            loc.range.end.character = refCol + static_cast<int>(name.size());

            references.push_back(loc);
        }

        pos += name.size();
    }

    return references;
}

std::vector<OwnershipInfo> AnalysisEngine::getOwnershipInfo(const std::string& uri) const {
    std::vector<OwnershipInfo> info;

    auto* result = getAnalysis(uri);
    if (!result) return info;

    // Scan content for ownership modifiers
    const std::string& content = result->sourceContent;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        if (c == '^' || c == '&' || c == '%') {
            // Check if preceded by identifier (type name)
            if (i > 0 && (std::isalnum(content[i - 1]) || content[i - 1] == '>')) {
                auto [line, col] = result->offsetToPosition(i);

                OwnershipInfo oi;
                oi.range.start.line = line;
                oi.range.start.character = col;
                oi.range.end.line = line;
                oi.range.end.character = col + 1;

                switch (c) {
                    case '^':
                        oi.kind = "owned";
                        oi.hoverMessage = "**Owned** (`^`) - Unique ownership";
                        break;
                    case '&':
                        oi.kind = "reference";
                        oi.hoverMessage = "**Reference** (`&`) - Borrowed reference";
                        break;
                    case '%':
                        oi.kind = "copy";
                        oi.hoverMessage = "**Copy** (`%`) - Value copy";
                        break;
                }

                info.push_back(oi);
            }
        }
    }

    return info;
}

std::optional<std::string> AnalysisEngine::getHoverInfo(
    const std::string& uri, int line, int character) const {

    auto* result = getAnalysis(uri);
    if (!result) return std::nullopt;

    // Get word at position
    size_t offset = result->positionToOffset(line, character);
    if (offset >= result->sourceContent.size()) return std::nullopt;

    size_t start = offset;
    size_t end = offset;

    while (start > 0 && (std::isalnum(result->sourceContent[start - 1]) ||
                         result->sourceContent[start - 1] == '_')) {
        --start;
    }
    while (end < result->sourceContent.size() &&
           (std::isalnum(result->sourceContent[end]) || result->sourceContent[end] == '_')) {
        ++end;
    }

    if (start >= end) return std::nullopt;

    std::string word = result->sourceContent.substr(start, end - start);

    // Check for XXML keywords first
    static const std::unordered_map<std::string, std::string> keywords = {
        {"Class", "**Class**\n\nDeclares a class type.\n\n```xxml\n[ Class <Name> Final Extends Base ... ]\n```"},
        {"Method", "**Method**\n\nDeclares a method.\n\n```xxml\nMethod <name> Returns Type Parameters (...) Do { ... }\n```"},
        {"Property", "**Property**\n\nDeclares a class property.\n\n```xxml\nProperty <name> Types Type^;\n```"},
        {"Constructor", "**Constructor**\n\nDeclares a class constructor.\n\n```xxml\nConstructor Parameters (...) -> { ... }\n```"},
        {"Instantiate", "**Instantiate**\n\nCreates a new variable.\n\n```xxml\nInstantiate Type^ As <name> = value;\n```"},
        {"Return", "**Return**\n\nReturns a value from a method."},
        {"If", "**If**\n\nConditional statement.\n\n```xxml\nIf (condition) -> { ... } Else -> { ... }\n```"},
        {"Else", "**Else**\n\nElse branch of conditional."},
        {"While", "**While**\n\nLoop statement.\n\n```xxml\nWhile (condition) -> { ... }\n```"},
        {"For", "**For**\n\nIterator loop.\n\n```xxml\nFor <item> In collection -> { ... }\n```"},
        {"Run", "**Run**\n\nExecute a statement (call method, etc.)."},
        {"Set", "**Set**\n\nAssign to a property.\n\n```xxml\nSet propertyName = value;\n```"},
        {"Exit", "**Exit**\n\nExit the program with a return code."},
        {"Public", "**Public**\n\nPublic access modifier."},
        {"Private", "**Private**\n\nPrivate access modifier."},
        {"Final", "**Final**\n\nMarks a class as non-inheritable."},
        {"Extends", "**Extends**\n\nSpecifies base class."},
        {"Types", "**Types**\n\nType annotation for properties/parameters."},
        {"Returns", "**Returns**\n\nReturn type annotation for methods."},
        {"Parameters", "**Parameters**\n\nParameter list for methods/constructors."},
        {"Do", "**Do**\n\nIntroduces method body."},
        {"Entrypoint", "**Entrypoint**\n\nProgram entry point (main function)."},
        {"Annotation", "**Annotation**\n\nDeclares a custom annotation."},
        {"Trait", "**Trait**\n\nDeclares a trait (interface with optional implementation)."},
        {"Enum", "**Enum**\n\nDeclares an enumeration."},
    };

    auto kwIt = keywords.find(word);
    if (kwIt != keywords.end()) {
        return kwIt->second;
    }

    // Try to find symbol information
    if (result->analyzer) {
        auto& symbolTable = result->context->symbolTable();
        auto* symbol = symbolTable.resolve(word);

        if (symbol) {
            std::ostringstream hover;

            // Format: **name**: type ownership
            hover << "**" << symbol->name << "**";

            if (!symbol->typeName.empty()) {
                hover << ": `" << symbol->typeName;

                // Add ownership modifier
                switch (symbol->ownership) {
                    case XXML::Parser::OwnershipType::Owned:
                        hover << "^";
                        break;
                    case XXML::Parser::OwnershipType::Reference:
                        hover << "&";
                        break;
                    case XXML::Parser::OwnershipType::Copy:
                        hover << "%";
                        break;
                    default:
                        break;
                }

                hover << "`";
            }

            hover << "\n\n";

            // Add kind info
            switch (symbol->kind) {
                case XXML::Semantic::SymbolKind::Class:
                    hover << "*Class*";
                    break;
                case XXML::Semantic::SymbolKind::Method:
                    hover << "*Method*";
                    break;
                case XXML::Semantic::SymbolKind::Constructor:
                    hover << "*Constructor*";
                    break;
                case XXML::Semantic::SymbolKind::Property:
                    hover << "*Property*";
                    break;
                case XXML::Semantic::SymbolKind::LocalVariable:
                    hover << "*Variable*";
                    break;
                case XXML::Semantic::SymbolKind::Parameter:
                    hover << "*Parameter*";
                    break;
                case XXML::Semantic::SymbolKind::Namespace:
                    hover << "*Namespace*";
                    break;
                default:
                    break;
            }

            return hover.str();
        }
    }

    return std::nullopt;
}

std::vector<CompletionItem> AnalysisEngine::getCompletions(
    const std::string& uri, int line, int character,
    const std::string& precedingText) const {

    std::vector<CompletionItem> completions;

    auto* result = getAnalysis(uri);

    // Check context for completion type
    bool afterDoubleColon = precedingText.size() >= 2 &&
                            precedingText.substr(precedingText.size() - 2) == "::";
    bool afterDot = !precedingText.empty() && precedingText.back() == '.';

    if (afterDoubleColon) {
        // Class member access - try to find class name before ::
        size_t colonPos = precedingText.rfind("::");
        if (colonPos != std::string::npos && colonPos > 0) {
            // Find start of class name
            size_t nameEnd = colonPos;
            size_t nameStart = nameEnd;
            while (nameStart > 0 && (std::isalnum(precedingText[nameStart - 1]) ||
                                      precedingText[nameStart - 1] == '_')) {
                --nameStart;
            }

            std::string className = precedingText.substr(nameStart, nameEnd - nameStart);

            // Look up class in semantic analyzer's class registry
            if (result && result->analyzer) {
                const auto& classRegistry = result->analyzer->getClassRegistry();
                auto it = classRegistry.find(className);
                if (it == classRegistry.end()) {
                    // Try with common namespace prefixes
                    for (const auto& [name, info] : classRegistry) {
                        if (name.ends_with("::" + className)) {
                            it = classRegistry.find(name);
                            break;
                        }
                    }
                }

                if (it != classRegistry.end()) {
                    const auto& classInfo = it->second;

                    // Add constructor
                    CompletionItem ctorItem;
                    ctorItem.label = "Constructor";
                    ctorItem.kind = CompletionItemKind::Constructor;
                    ctorItem.detail = "Create new " + className;
                    ctorItem.insertText = "Constructor";
                    completions.push_back(ctorItem);

                    // Add methods
                    for (const auto& [methodName, methodInfo] : classInfo.methods) {
                        CompletionItem item;
                        item.label = methodName;
                        item.kind = CompletionItemKind::Method;
                        item.detail = methodInfo.returnType;
                        item.insertText = methodName;
                        completions.push_back(item);
                    }
                }
            }

            // Fallback: common methods
            if (completions.empty()) {
                completions.push_back({"Constructor", CompletionItemKind::Constructor,
                                       "Class constructor", "", "Constructor"});
            }
        }
    } else if (afterDot) {
        // Instance member access
        // Would need type inference to know what type we're accessing
        // For now, provide generic completions
    } else {
        // General completions - keywords
        std::vector<std::tuple<std::string, std::string, CompletionItemKind>> items = {
            {"Class", "Declare a class", CompletionItemKind::Keyword},
            {"Method", "Declare a method", CompletionItemKind::Keyword},
            {"Property", "Declare a property", CompletionItemKind::Keyword},
            {"Constructor", "Declare a constructor", CompletionItemKind::Keyword},
            {"Instantiate", "Create a variable", CompletionItemKind::Keyword},
            {"Return", "Return from method", CompletionItemKind::Keyword},
            {"If", "Conditional statement", CompletionItemKind::Keyword},
            {"Else", "Else branch", CompletionItemKind::Keyword},
            {"While", "While loop", CompletionItemKind::Keyword},
            {"For", "For loop", CompletionItemKind::Keyword},
            {"Run", "Execute statement", CompletionItemKind::Keyword},
            {"Set", "Assign property", CompletionItemKind::Keyword},
            {"Exit", "Exit program", CompletionItemKind::Keyword},
            {"Public", "Public access", CompletionItemKind::Keyword},
            {"Private", "Private access", CompletionItemKind::Keyword},
            {"Final", "Final class", CompletionItemKind::Keyword},
            {"Extends", "Inheritance", CompletionItemKind::Keyword},
            {"Types", "Type annotation", CompletionItemKind::Keyword},
            {"Returns", "Return type", CompletionItemKind::Keyword},
            {"Parameters", "Parameter list", CompletionItemKind::Keyword},
            {"Do", "Method body", CompletionItemKind::Keyword},
            {"Entrypoint", "Program entry", CompletionItemKind::Keyword},
        };

        for (const auto& [label, detail, kind] : items) {
            CompletionItem item;
            item.label = label;
            item.kind = kind;
            item.detail = detail;
            item.insertText = label;
            completions.push_back(item);
        }

        // Add symbols from analysis
        if (result && result->analyzer) {
            // Add class names from class registry
            for (const auto& [name, classInfo] : result->analyzer->getClassRegistry()) {
                CompletionItem item;
                // Use short name (without namespace)
                size_t lastColon = name.rfind("::");
                item.label = (lastColon != std::string::npos) ? name.substr(lastColon + 2) : name;
                item.kind = CompletionItemKind::Class;
                item.detail = "Class";
                item.insertText = item.label;
                completions.push_back(item);
            }

            // Add symbols from symbol table
            auto& symbolTable = result->context->symbolTable();
            auto* globalScope = symbolTable.getGlobalScope();
            if (globalScope) {
                for (const auto& [symName, sym] : globalScope->getSymbols()) {
                    CompletionItem item;
                    item.label = sym->name;
                    item.detail = sym->typeName;

                    switch (sym->kind) {
                        case XXML::Semantic::SymbolKind::Class:
                            item.kind = CompletionItemKind::Class;
                            break;
                        case XXML::Semantic::SymbolKind::Method:
                            item.kind = CompletionItemKind::Method;
                            break;
                        case XXML::Semantic::SymbolKind::Property:
                            item.kind = CompletionItemKind::Property;
                            break;
                        case XXML::Semantic::SymbolKind::LocalVariable:
                        case XXML::Semantic::SymbolKind::Parameter:
                            item.kind = CompletionItemKind::Variable;
                            break;
                        default:
                            item.kind = CompletionItemKind::Text;
                    }

                    item.insertText = sym->name;
                    completions.push_back(item);
                }
            }
        }
    }

    return completions;
}

std::vector<DocumentSymbol> AnalysisEngine::getDocumentSymbols(const std::string& uri) const {
    std::vector<DocumentSymbol> symbols;

    auto* result = getAnalysis(uri);
    if (!result || !result->ast) return symbols;

    collectDocumentSymbols(symbols, result->ast.get(), uri, *result);

    return symbols;
}

void AnalysisEngine::collectDocumentSymbols(std::vector<DocumentSymbol>& symbols,
                                             XXML::Parser::Program* ast,
                                             const std::string& uri,
                                             const AnalysisResult& result) const {
    if (!ast) return;

    // Traverse declarations
    for (const auto& decl : ast->declarations) {
        if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(decl.get())) {
            DocumentSymbol classSym;
            classSym.name = classDecl->name;
            classSym.kind = SymbolKind::Class;

            auto loc = convertSourceLocation(uri, classDecl->location, result);
            classSym.range = loc.range;
            classSym.selectionRange = loc.range;

            // Add methods as children
            for (const auto& section : classDecl->sections) {
                for (const auto& memberDecl : section->declarations) {
                    if (auto* methodDecl = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                        DocumentSymbol methodSym;
                        methodSym.name = methodDecl->name;
                        methodSym.kind = SymbolKind::Method;
                        methodSym.detail = methodDecl->returnType ? methodDecl->returnType->toString() : "void";

                        auto methodLoc = convertSourceLocation(uri, methodDecl->location, result);
                        methodSym.range = methodLoc.range;
                        methodSym.selectionRange = methodLoc.range;

                        classSym.children.push_back(methodSym);
                    } else if (auto* propDecl = dynamic_cast<XXML::Parser::PropertyDecl*>(memberDecl.get())) {
                        DocumentSymbol propSym;
                        propSym.name = propDecl->name;
                        propSym.kind = SymbolKind::Property;
                        propSym.detail = propDecl->type ? propDecl->type->toString() : "unknown";

                        auto propLoc = convertSourceLocation(uri, propDecl->location, result);
                        propSym.range = propLoc.range;
                        propSym.selectionRange = propLoc.range;

                        classSym.children.push_back(propSym);
                    } else if (auto* ctorDecl = dynamic_cast<XXML::Parser::ConstructorDecl*>(memberDecl.get())) {
                        DocumentSymbol ctorSym;
                        ctorSym.name = "Constructor";
                        ctorSym.kind = SymbolKind::Constructor;

                        auto ctorLoc = convertSourceLocation(uri, ctorDecl->location, result);
                        ctorSym.range = ctorLoc.range;
                        ctorSym.selectionRange = ctorLoc.range;

                        classSym.children.push_back(ctorSym);
                    }
                }
            }

            symbols.push_back(classSym);
        } else if (auto* entrypoint = dynamic_cast<XXML::Parser::EntrypointDecl*>(decl.get())) {
            DocumentSymbol entrySym;
            entrySym.name = "Entrypoint";
            entrySym.kind = SymbolKind::Method;

            auto loc = convertSourceLocation(uri, entrypoint->location, result);
            entrySym.range = loc.range;
            entrySym.selectionRange = loc.range;

            symbols.push_back(entrySym);
        }
    }
}

void AnalysisEngine::collectOwnershipInfo(std::vector<OwnershipInfo>& info,
                                           XXML::Parser::Program* ast,
                                           const AnalysisResult& result) const {
    // This would traverse the AST and collect ownership annotations
    // For now, we use the text-based approach in getOwnershipInfo()
}

std::string AnalysisEngine::getTypeString(const XXML::Semantic::Symbol* symbol) const {
    if (!symbol) return "";

    std::string result = symbol->typeName;

    // Add ownership modifier
    switch (symbol->ownership) {
        case XXML::Parser::OwnershipType::Owned:
            result += "^";
            break;
        case XXML::Parser::OwnershipType::Reference:
            result += "&";
            break;
        case XXML::Parser::OwnershipType::Copy:
            result += "%";
            break;
        default:
            break;
    }

    return result;
}

} // namespace xxml::lsp
