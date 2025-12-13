// AnalysisEngine.cpp - XXML Semantic Analysis Integration
// XXML Language Server Protocol Implementation

#include "AnalysisEngine.h"
#include "Import/ImportResolver.h"
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <set>

namespace xxml::lsp {

namespace fs = std::filesystem;

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
    // Paths will be set by LSPServer after initialization
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

    // Convert URI to file path and extract directory
    std::string filePath = uri;
    if (filePath.substr(0, 8) == "file:///") {
        filePath = filePath.substr(8);
#ifdef _WIN32
        // URL decode and fix path separators for Windows
        std::string decoded;
        for (size_t i = 0; i < filePath.size(); ++i) {
            if (filePath[i] == '%' && i + 2 < filePath.size()) {
                int hex = std::stoi(filePath.substr(i + 1, 2), nullptr, 16);
                decoded += static_cast<char>(hex);
                i += 2;
            } else if (filePath[i] == '/') {
                decoded += '\\';
            } else {
                decoded += filePath[i];
            }
        }
        filePath = decoded;
#endif
    }

    // Get directory containing the source file
    std::string sourceDir;
    fs::path sourcePath(filePath);
    if (sourcePath.has_parent_path()) {
        sourceDir = sourcePath.parent_path().string();
    }

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

        // Step 2.5: Load and parse imports
        std::set<std::string> loadedFiles;  // Track loaded files to avoid duplicates
        loadedFiles.insert(filePath);  // Don't re-load the main file

        std::vector<std::unique_ptr<XXML::Parser::Program>> importedPrograms;

        for (const auto& decl : result->ast->declarations) {
            if (auto* importDecl = dynamic_cast<XXML::Parser::ImportDecl*>(decl.get())) {
                std::string resolvedPath = resolveImportPath(importDecl->modulePath, sourceDir);
                if (resolvedPath.empty()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Warning;
                    diag.message = "Cannot resolve import '" + importDecl->modulePath + "'";
                    diag.message += ". Searched in:";
                    if (!includePaths_.empty()) diag.message += " [include paths]";
                    if (!sourceDir.empty()) diag.message += " [source dir]";
                    if (!stdlibPath_.empty()) diag.message += " [stdlib]";
                    if (!workspaceRoot_.empty()) diag.message += " [workspace]";

                    int line = importDecl->location.line > 0 ? importDecl->location.line - 1 : 0;
                    int col = importDecl->location.column > 0 ? importDecl->location.column - 1 : 0;
                    diag.range.start = {line, col};
                    diag.range.end = {line, col + static_cast<int>(importDecl->modulePath.length()) + 8};
                    result->diagnostics.push_back(diag);
                } else {
                    // Find all XXML files in the resolved path
                    std::vector<std::string> filesToLoad;

                    if (fs::is_directory(resolvedPath)) {
                        // Load all XXML files in the directory
                        for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                            if (entry.is_regular_file()) {
                                std::string ext = entry.path().extension().string();
                                if (ext == ".XXML" || ext == ".xxml") {
                                    std::string absPath = fs::absolute(entry.path()).string();
                                    if (loadedFiles.find(absPath) == loadedFiles.end()) {
                                        filesToLoad.push_back(absPath);
                                        loadedFiles.insert(absPath);
                                    }
                                }
                            }
                        }
                    } else if (fs::is_regular_file(resolvedPath)) {
                        std::string absPath = fs::absolute(resolvedPath).string();
                        if (loadedFiles.find(absPath) == loadedFiles.end()) {
                            filesToLoad.push_back(absPath);
                            loadedFiles.insert(absPath);
                        }
                    }

                    // Parse each imported file
                    for (const auto& importFile : filesToLoad) {
                        try {
                            std::ifstream ifs(importFile);
                            if (!ifs) continue;

                            std::string importContent((std::istreambuf_iterator<char>(ifs)),
                                                       std::istreambuf_iterator<char>());
                            ifs.close();

                            // Create a separate error reporter for imports (don't pollute main errors)
                            XXML::Common::ErrorReporter importErrors;

                            // Lex and parse the import
                            XXML::Lexer::Lexer importLexer(importContent,
                                fs::path(importFile).filename().string(), importErrors);
                            auto importTokens = importLexer.tokenize();

                            if (!importErrors.hasErrors()) {
                                XXML::Parser::Parser importParser(importTokens, importErrors);
                                auto importAst = importParser.parse();

                                if (importAst && !importErrors.hasErrors()) {
                                    importedPrograms.push_back(std::move(importAst));
                                }
                            }
                        } catch (...) {
                            // Silently skip files that fail to parse
                        }
                    }
                }
            }
        }

        // Step 3: Semantic analysis
        result->errorReporter->clear();
        result->analyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(
            *result->context, *result->errorReporter);

        // First analyze all imported programs to populate symbol tables
        for (auto& importedAst : importedPrograms) {
            if (importedAst) {
                result->analyzer->analyze(*importedAst);
            }
        }

        // Clear errors from imports (we only want to report errors from the main file)
        result->errorReporter->clear();

        // Now analyze the main file
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
        // Declaration keywords
        {"Class", "**Class**\n\nDeclares a class type.\n\n```xxml\n[ Class <Name> Final Extends Base ... ]\n```"},
        {"Struct", "**Struct**\n\nDeclares a value type (stack-allocated).\n\n```xxml\n[ Struct <Name> ... ]\n```"},
        {"Interface", "**Interface**\n\nDeclares an interface (abstract contract).\n\n```xxml\n[ Interface <Name> ... ]\n```"},
        {"Trait", "**Trait**\n\nDeclares a trait (interface with optional default implementation).\n\n```xxml\n[ Trait <Name> ... ]\n```"},
        {"Enum", "**Enum**\n\nDeclares an enumeration.\n\n```xxml\n[ Enum <Name> ... ]\n```"},
        {"Annotation", "**Annotation**\n\nDeclares a custom annotation for metaprogramming.\n\n```xxml\n[ Annotation <Name> ... ]\n```"},
        {"Namespace", "**Namespace**\n\nDeclares a namespace for organizing code.\n\n```xxml\n[ Namespace <Name::Path> ... ]\n```"},
        {"Entrypoint", "**Entrypoint**\n\nProgram entry point (main function).\n\n```xxml\n[ Entrypoint { ... } ]\n```"},

        // Member declarations
        {"Method", "**Method**\n\nDeclares a method.\n\n```xxml\nMethod <name> Returns Type^ Parameters (...) Do { ... }\n```"},
        {"Property", "**Property**\n\nDeclares a class property/field.\n\n```xxml\nProperty <name> Types Type^;\n```"},
        {"Constructor", "**Constructor**\n\nDeclares or calls a class constructor.\n\n```xxml\nConstructor Parameters (...) -> { ... }\n// or\nConstructor = default;\n```"},
        {"Destructor", "**Destructor**\n\nDeclares a destructor for cleanup.\n\n```xxml\nDestructor -> { ... }\n```"},
        {"Operator", "**Operator**\n\nDeclares an operator overload.\n\n```xxml\nOperator <+> Returns Type^ Parameters (...) Do { ... }\n```"},

        // Statement keywords
        {"Instantiate", "**Instantiate**\n\nCreates a new variable with explicit type.\n\n```xxml\nInstantiate Type^ As <name> = value;\n```"},
        {"Return", "**Return**\n\nReturns a value from a method.\n\n```xxml\nReturn expression;\n```"},
        {"Run", "**Run**\n\nExecutes a statement or method call.\n\n```xxml\nRun obj.method(args);\n```"},
        {"Set", "**Set**\n\nAssigns to a property or variable.\n\n```xxml\nSet propertyName = value;\n```"},
        {"Exit", "**Exit**\n\nExits the program with a return code.\n\n```xxml\nExit(0);\n```"},

        // Control flow
        {"If", "**If**\n\nConditional statement.\n\n```xxml\nIf (condition) -> { ... }\nElse -> { ... }\n```"},
        {"Else", "**Else**\n\nElse branch of conditional."},
        {"While", "**While**\n\nLoop while condition is true.\n\n```xxml\nWhile (condition) -> { ... }\n```"},
        {"For", "**For**\n\nIterator loop over collection.\n\n```xxml\nFor <item> In collection -> { ... }\n```"},
        {"Match", "**Match**\n\nPattern matching statement.\n\n```xxml\nMatch (value) { Case X -> { ... } }\n```"},
        {"Case", "**Case**\n\nCase branch in match statement."},
        {"Break", "**Break**\n\nBreaks out of a loop."},
        {"Continue", "**Continue**\n\nContinues to next loop iteration."},

        // Access modifiers
        {"Public", "**Public**\n\nPublic access - visible everywhere."},
        {"Private", "**Private**\n\nPrivate access - visible only within class."},
        {"Protected", "**Protected**\n\nProtected access - visible within class and subclasses."},

        // Type modifiers
        {"Final", "**Final**\n\nMarks a class as non-inheritable or method as non-overridable."},
        {"Abstract", "**Abstract**\n\nMarks a class or method as abstract (must be implemented)."},
        {"Static", "**Static**\n\nMarks a member as belonging to the class, not instances."},
        {"Virtual", "**Virtual**\n\nMarks a method as overridable in subclasses."},
        {"Override", "**Override**\n\nIndicates method overrides a base class method."},
        {"Const", "**Const**\n\nMarks a value as immutable."},
        {"Compiletime", "**Compiletime**\n\nMarks a class/method for compile-time evaluation."},

        // Type annotations
        {"Types", "**Types**\n\nType annotation for properties/parameters.\n\n```xxml\nProperty <x> Types Integer^;\n```"},
        {"Returns", "**Returns**\n\nReturn type annotation for methods.\n\n```xxml\nMethod <foo> Returns String^ ...\n```"},
        {"Parameters", "**Parameters**\n\nParameter list for methods/constructors."},
        {"Parameter", "**Parameter**\n\nDeclares a method parameter.\n\n```xxml\nParameter <name> Types Type^\n```"},
        {"Extends", "**Extends**\n\nSpecifies base class inheritance.\n\n```xxml\n[ Class <Child> Extends Parent ... ]\n```"},
        {"Implements", "**Implements**\n\nSpecifies interface/trait implementation."},
        {"Do", "**Do**\n\nIntroduces method body block."},
        {"As", "**As**\n\nVariable name specifier in Instantiate."},
        {"In", "**In**\n\nCollection specifier in For loop."},

        // Ownership modifiers (as keywords when used standalone)
        {"Owned", "**Owned (`^`)**\n\nUnique ownership - this reference owns the object and is responsible for its lifetime."},
        {"Reference", "**Reference (`&`)**\n\nBorrowed reference - does not own the object, just borrows access."},
        {"Copy", "**Copy (`%`)**\n\nValue copy - creates a copy of the value."},

        // Special values
        {"true", "**true**\n\nBoolean literal `true`."},
        {"false", "**false**\n\nBoolean literal `false`."},
        {"null", "**null**\n\nNull reference (no object)."},
        {"None", "**None**\n\nNo base class / no value."},
        {"default", "**default**\n\nDefault value or default implementation.\n\n```xxml\nConstructor = default;\n```"},
        {"this", "**this**\n\nReference to the current object instance."},
        {"Self", "**Self**\n\nReference to the current class type."},

        // Common types
        {"Integer", "**Integer**\n\n64-bit signed integer type.\n\nFrom `Language::Core`"},
        {"String", "**String**\n\nUTF-8 string type.\n\nFrom `Language::Core`"},
        {"Bool", "**Bool**\n\nBoolean type (`true`/`false`).\n\nFrom `Language::Core`"},
        {"Float", "**Float**\n\n32-bit floating point type.\n\nFrom `Language::Core`"},
        {"Double", "**Double**\n\n64-bit floating point type.\n\nFrom `Language::Core`"},
        {"Char", "**Char**\n\nSingle character type.\n\nFrom `Language::Core`"},
        {"Void", "**Void**\n\nNo return value type."},
        {"NativeType", "**NativeType**\n\nLow-level native type wrapper.\n\n```xxml\nNativeType<\"int64\">^\n```"},
        {"List", "**List<T>**\n\nDynamic array/list collection.\n\nFrom `Language::Collections`"},
        {"HashMap", "**HashMap<K,V>**\n\nKey-value hash map collection.\n\nFrom `Language::Collections`"},
        {"Set", "**Set<T>**\n\nUnique value set collection.\n\nFrom `Language::Collections`"},
    };

    auto kwIt = keywords.find(word);
    if (kwIt != keywords.end()) {
        return kwIt->second;
    }

    // Try to find class information in the class registry
    if (result->analyzer) {
        const auto& classRegistry = result->analyzer->getClassRegistry();

        // Check if word is a known class name
        for (const auto& [className, classInfo] : classRegistry) {
            // Match either full name or short name
            std::string shortName = className;
            size_t lastColon = className.rfind("::");
            if (lastColon != std::string::npos) {
                shortName = className.substr(lastColon + 2);
            }

            if (shortName == word || className == word) {
                std::ostringstream hover;
                hover << "**" << shortName << "**\n\n";
                hover << "*Class*";

                if (!className.empty() && className != shortName) {
                    hover << " from `" << className.substr(0, lastColon) << "`";
                }

                // List public methods
                if (!classInfo.methods.empty()) {
                    hover << "\n\n**Methods:**\n";
                    int count = 0;
                    for (const auto& [methodName, methodInfo] : classInfo.methods) {
                        if (count++ < 10) {  // Limit to 10 methods
                            hover << "- `" << methodName << "()` → " << methodInfo.returnType << "\n";
                        }
                    }
                    if (count > 10) {
                        hover << "- ... and " << (count - 10) << " more\n";
                    }
                }

                return hover.str();
            }
        }

        // Try symbol table lookup
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
                    hover << "*Local Variable*";
                    break;
                case XXML::Semantic::SymbolKind::Parameter:
                    hover << "*Parameter*";
                    break;
                case XXML::Semantic::SymbolKind::Namespace:
                    hover << "*Namespace*";
                    break;
                default:
                    hover << "*Symbol*";
                    break;
            }

            return hover.str();
        }
    }

    // Check for ownership modifier characters
    if (offset > 0 && offset <= result->sourceContent.size()) {
        char c = result->sourceContent[offset];
        if (c == '^') {
            return "**Owned (`^`)**\n\nUnique ownership modifier.\n\nThis reference owns the object and is responsible for its lifetime. When this reference goes out of scope, the object is destroyed.";
        } else if (c == '&') {
            return "**Reference (`&`)**\n\nBorrowed reference modifier.\n\nThis reference borrows access to an object owned elsewhere. It cannot outlive the owner.";
        } else if (c == '%') {
            return "**Copy (`%`)**\n\nValue copy modifier.\n\nThis creates a copy of the value. Changes to the copy don't affect the original.";
        }
    }

    return std::nullopt;
}

std::vector<CompletionItem> AnalysisEngine::getCompletions(
    const std::string& uri, int line, int character,
    const std::string& precedingText) const {

    auto* result = getAnalysis(uri);

    // Build context-aware completion context
    CompletionContext ctx = buildCompletionContext(result, line, character, precedingText);

    // Strict context-based dispatch - return ONLY appropriate items
    if (ctx.afterDot) {
        // After "." - ONLY member completions, nothing else
        return getMemberCompletions(ctx, result);
    } else if (ctx.afterDoubleColon) {
        // After "::" - ONLY static completions
        return getStaticCompletions(ctx, result);
    } else if (ctx.inTypePosition) {
        // In type position - ONLY types, no keywords
        return getTypeCompletions(ctx, result);
    } else if (ctx.inVariableNamePosition) {
        // After "As <" - suggesting variable name, no completions needed
        return {};
    } else if (ctx.inExpressionPosition) {
        // In expression position - variables, class names, literals
        return getExpressionCompletions(ctx, result);
    } else if (ctx.inStatementPosition) {
        // At start of statement - statement keywords + variables
        return getStatementCompletions(ctx, result);
    } else if (ctx.inMemberDeclarationPosition) {
        // Inside class but outside method - member declaration keywords
        return getMemberDeclarationCompletions(ctx, result);
    } else if (ctx.inTopLevelPosition) {
        // At top level - declaration keywords only
        return getTopLevelCompletions(ctx, result);
    } else {
        // Fallback - context-appropriate completions
        return getGeneralCompletions(ctx, result);
    }
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

std::string AnalysisEngine::resolveImportPath(const std::string& importPath, const std::string& sourceDir) const {
    // Convert import path like "Language::Core" to directory path "Language/Core"
    std::string dirPath = importPath;
    size_t pos = 0;
    while ((pos = dirPath.find("::", pos)) != std::string::npos) {
        dirPath.replace(pos, 2, "/");
    }

    // Build list of search paths
    std::vector<fs::path> searchPaths;

    // 0. User-specified include paths (highest priority)
    for (const auto& includePath : includePaths_) {
        searchPaths.push_back(fs::path(includePath));
    }

    // 1. Source file's directory and its parents (walk up to find Language/ or the import)
    if (!sourceDir.empty()) {
        fs::path currentDir = fs::path(sourceDir);
        // Walk up directory tree looking for the import
        for (int depth = 0; depth < 10 && !currentDir.empty(); ++depth) {
            searchPaths.push_back(currentDir);
            if (currentDir.has_parent_path() && currentDir.parent_path() != currentDir) {
                currentDir = currentDir.parent_path();
            } else {
                break;
            }
        }
    }

    // 2. Stdlib path (for Language:: imports)
    if (!stdlibPath_.empty()) {
        // stdlibPath_ points to Language/, so use its parent
        fs::path stdlibParent = fs::path(stdlibPath_).parent_path();
        searchPaths.push_back(stdlibParent);
        // Also add the stdlib path itself in case import is relative to Language/
        searchPaths.push_back(fs::path(stdlibPath_));
    }

    // 3. Workspace root and common build locations
    if (!workspaceRoot_.empty()) {
        searchPaths.push_back(fs::path(workspaceRoot_));
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "release" / "bin");
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "debug" / "bin");
    }

    // Try each search path
    for (const auto& basePath : searchPaths) {
        // Check for directory (namespace import like "Language::Core")
        fs::path candidateDir = basePath / dirPath;
        if (fs::exists(candidateDir) && fs::is_directory(candidateDir)) {
            return candidateDir.string();
        }

        // Check for single-file module (e.g., "System" -> "System.XXML")
        fs::path candidateFile = basePath / (dirPath + ".XXML");
        if (fs::exists(candidateFile) && fs::is_regular_file(candidateFile)) {
            return candidateFile.string();
        }

        // Also try lowercase extension
        fs::path candidateFileLower = basePath / (dirPath + ".xxml");
        if (fs::exists(candidateFileLower) && fs::is_regular_file(candidateFileLower)) {
            return candidateFileLower.string();
        }
    }

    // Not found
    return "";
}

// ============================================================================
// Context-Aware Completion Infrastructure
// ============================================================================

// Helper: Strip ownership suffix from type name
std::string AnalysisEngine::stripOwnership(const std::string& typeName) {
    if (typeName.empty()) return typeName;
    char last = typeName.back();
    if (last == '^' || last == '&' || last == '%') {
        return typeName.substr(0, typeName.size() - 1);
    }
    return typeName;
}

// Helper: Strip generic parameters from type name
std::string AnalysisEngine::stripGenerics(const std::string& typeName) {
    size_t pos = typeName.find('<');
    if (pos != std::string::npos) {
        return typeName.substr(0, pos);
    }
    return typeName;
}

// Find AST node at position - traverse the AST to find enclosing context
XXML::Parser::ASTNode* AnalysisEngine::findNodeAtPosition(
    XXML::Parser::Program* ast, int line, int character,
    const AnalysisResult& result) const {

    if (!ast) return nullptr;

    // Convert to 1-based line numbers used by the AST
    int targetLine = line + 1;

    // Track the best (most specific) match
    XXML::Parser::ASTNode* best = nullptr;

    // Check each declaration
    for (const auto& decl : ast->declarations) {
        if (!decl) continue;

        // Check if the cursor is within or after this declaration's start
        if (decl->location.line <= targetLine) {
            // This declaration starts at or before our position
            // Check specific declaration types

            if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(decl.get())) {
                // Check class sections for methods/properties
                for (const auto& section : classDecl->sections) {
                    for (const auto& memberDecl : section->declarations) {
                        if (memberDecl->location.line <= targetLine) {
                            if (auto* method = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                                // Check if cursor is in method body
                                for (const auto& stmt : method->body) {
                                    if (stmt->location.line <= targetLine) {
                                        best = stmt.get();
                                    }
                                }
                                if (!best || method->location.line >= (best ? best->location.line : 0)) {
                                    best = method;
                                }
                            } else if (auto* ctor = dynamic_cast<XXML::Parser::ConstructorDecl*>(memberDecl.get())) {
                                for (const auto& stmt : ctor->body) {
                                    if (stmt->location.line <= targetLine) {
                                        best = stmt.get();
                                    }
                                }
                                if (!best || ctor->location.line >= (best ? best->location.line : 0)) {
                                    best = ctor;
                                }
                            } else {
                                best = memberDecl.get();
                            }
                        }
                    }
                }
                if (!best) best = classDecl;
            } else if (auto* entrypoint = dynamic_cast<XXML::Parser::EntrypointDecl*>(decl.get())) {
                // Check entrypoint body
                for (const auto& stmt : entrypoint->body) {
                    if (stmt->location.line <= targetLine) {
                        best = stmt.get();
                    }
                }
                if (!best) best = entrypoint;
            } else {
                best = decl.get();
            }
        }
    }

    return best;
}

// Build completion context from position and preceding text
CompletionContext AnalysisEngine::buildCompletionContext(
    const AnalysisResult* result, int line, int character,
    const std::string& precedingText) const {

    CompletionContext ctx;

    // Detect basic context from preceding text first (works without AST)
    std::string trimmed = precedingText;
    while (!trimmed.empty() && std::isspace(trimmed.back())) {
        trimmed.pop_back();
    }

    // Check for . or :: at end - these take priority
    ctx.afterDoubleColon = trimmed.size() >= 2 &&
                           trimmed.substr(trimmed.size() - 2) == "::";
    ctx.afterDot = !trimmed.empty() && trimmed.back() == '.' && !ctx.afterDoubleColon;

    // If after . or ::, extract the preceding identifier
    if (ctx.afterDot || ctx.afterDoubleColon) {
        size_t endPos = ctx.afterDoubleColon ? trimmed.size() - 2 : trimmed.size() - 1;
        size_t startPos = endPos;

        // Walk backwards to find identifier start (handle method chains like "obj.getList()")
        int parenDepth = 0;
        while (startPos > 0) {
            char c = trimmed[startPos - 1];
            if (c == ')') { parenDepth++; --startPos; continue; }
            if (c == '(') { parenDepth--; --startPos; continue; }
            if (parenDepth > 0) { --startPos; continue; }
            if (std::isalnum(c) || c == '_' || c == '.' || c == ':') {
                --startPos;
            } else {
                break;
            }
        }

        ctx.precedingIdentifier = trimmed.substr(startPos, endPos - startPos);
        // Don't return early - we still need to find currentClass/currentMethod
        // for variable scope resolution. Fall through to AST traversal.
    }

    // Skip other position detection if we're doing member access (. or ::)
    // Those take priority and we just need to find the class/method context
    if (!ctx.afterDot && !ctx.afterDoubleColon) {

        // Check for variable name position: "As <" pattern
        {
            size_t asPos = trimmed.rfind(" As <");
            if (asPos == std::string::npos) asPos = trimmed.rfind("\tAs <");
            if (asPos != std::string::npos) {
                // Check if there's no ">" after the "<"
                std::string afterAs = trimmed.substr(asPos + 5);  // After " As <"
                if (afterAs.find('>') == std::string::npos) {
                    ctx.inVariableNamePosition = true;
                    return ctx;  // No completions for variable names
                }
            }
        }

        // Detect expression position: after verbs that take expressions
    // NOTE: Use original precedingText here since trimmed strips trailing spaces
    {
        // Expression-triggering keywords - check if trimmed ENDS with these
        // (trailing space was stripped, so "Run " becomes "Run")
        static const std::vector<std::string> exprKeywords = {
            "Run", "Return", "If", "ElseIf", "While", "Set"
        };

        for (const auto& kw : exprKeywords) {
            // Check if trimmed ends with the keyword
            if (trimmed.size() >= kw.size()) {
                size_t pos = trimmed.size() - kw.size();
                if (trimmed.substr(pos) == kw) {
                    // Verify it's a whole word (not part of another identifier)
                    bool isWord = (pos == 0 || !std::isalnum(trimmed[pos - 1]));
                    if (isWord) {
                        // Check original text - must have space after keyword
                        // (user typed "Run " not just "Run")
                        if (precedingText.size() > trimmed.size()) {
                            ctx.inExpressionPosition = true;
                            break;
                        }
                    }
                }
            }

            // Also check for keyword followed by partial expression (e.g., "Run obj")
            size_t kwPos = trimmed.rfind(kw + " ");
            if (kwPos != std::string::npos) {
                bool isWord = (kwPos == 0 || !std::isalnum(trimmed[kwPos - 1]));
                if (isWord) {
                    std::string afterKw = trimmed.substr(kwPos + kw.length() + 1);
                    if (afterKw.find(';') == std::string::npos) {
                        ctx.inExpressionPosition = true;
                        break;
                    }
                }
            }
        }

        // After "= " in assignment/initialization (but not inside < >)
        if (!ctx.inExpressionPosition) {
            size_t eqPos = trimmed.rfind('=');
            if (eqPos != std::string::npos && eqPos > 0) {
                // Check it's not inside angle brackets (template args)
                int angleDepth = 0;
                for (size_t i = eqPos + 1; i < trimmed.size(); ++i) {
                    if (trimmed[i] == '<') angleDepth++;
                    if (trimmed[i] == '>') angleDepth--;
                }
                if (angleDepth == 0) {
                    ctx.inExpressionPosition = true;
                }
            }
        }

        // After "(" or "," in function arguments (check original text for trailing)
        if (!ctx.inExpressionPosition) {
            // Check last non-space char in original or last char of trimmed
            if (!trimmed.empty()) {
                char lastChar = trimmed.back();
                if (lastChar == '(' || lastChar == ',') {
                    ctx.inExpressionPosition = true;
                }
            }
            // Also check if original ends with ( or , followed by spaces
            if (!ctx.inExpressionPosition && !precedingText.empty()) {
                for (auto it = precedingText.rbegin(); it != precedingText.rend(); ++it) {
                    if (!std::isspace(*it)) {
                        if (*it == '(' || *it == ',') {
                            ctx.inExpressionPosition = true;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Detect type position: after specific keywords, before "As"
    if (!ctx.inExpressionPosition) {
        static const std::vector<std::string> typeKeywords = {
            "Instantiate", "Types", "Returns", "Parameter", "Extends", "Implements"
        };

        for (const auto& kw : typeKeywords) {
            size_t pos = trimmed.rfind(kw);
            if (pos != std::string::npos) {
                bool validStart = (pos == 0 || !std::isalnum(trimmed[pos - 1]));
                if (validStart) {
                    std::string after = trimmed.substr(pos + kw.length());
                    // Check if "As" appears - if so, we're past type position
                    if (after.find(" As ") != std::string::npos ||
                        after.find("\tAs ") != std::string::npos) {
                        break;  // Past type position
                    }
                    // Check if only type-like characters follow
                    bool allTypeChars = true;
                    for (char c : after) {
                        if (std::isspace(c)) continue;
                        if (!std::isalnum(c) && c != '_' && c != ':' && c != '<' && c != '>' &&
                            c != ',' && c != '^' && c != '&' && c != '%') {
                            allTypeChars = false;
                            break;
                        }
                    }
                    if (allTypeChars) {
                        ctx.inTypePosition = true;
                        break;
                    }
                }
            }
        }
    }

    } // End of if (!ctx.afterDot && !ctx.afterDoubleColon)

    // Convert to 1-based line
    int targetLine = line + 1;

    if (!result || !result->ast) return ctx;

    // Find enclosing context from AST (needed for variable scope even with . or ::)
    for (const auto& decl : result->ast->declarations) {
        if (!decl) continue;

        if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(decl.get())) {
            // Check if we're inside this class
            if (classDecl->location.line <= targetLine) {
                ctx.currentClass = classDecl;

                // Check sections for method/constructor context
                for (const auto& section : classDecl->sections) {
                    for (const auto& memberDecl : section->declarations) {
                        if (memberDecl->location.line <= targetLine) {
                            if (auto* method = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                                ctx.currentMethod = method;
                                ctx.currentConstructor = nullptr;
                            } else if (auto* ctor = dynamic_cast<XXML::Parser::ConstructorDecl*>(memberDecl.get())) {
                                ctx.currentConstructor = ctor;
                                ctx.currentMethod = nullptr;
                            }
                        }
                    }
                }
            }
        } else if (auto* entrypoint = dynamic_cast<XXML::Parser::EntrypointDecl*>(decl.get())) {
            if (entrypoint->location.line <= targetLine) {
                ctx.inEntrypoint = true;
            }
        }
    }

    // Determine positional context if not already set
    if (!ctx.inTypePosition && !ctx.inExpressionPosition && !ctx.inVariableNamePosition) {
        // Check if we're at start of a line (statement position check)
        bool atLineStart = true;
        for (char c : trimmed) {
            if (!std::isspace(c)) {
                // Check if what's on the line looks like start of statement
                // Allow partial keywords
                atLineStart = false;
                break;
            }
        }

        if (ctx.currentMethod || ctx.currentConstructor || ctx.inEntrypoint) {
            // Inside a method/constructor/entrypoint body
            // Check if line is mostly empty or has partial keyword
            bool looksLikeStatementStart = trimmed.empty();
            if (!looksLikeStatementStart) {
                // Check for partial statement keywords
                static const std::vector<std::string> stmtKeywords = {
                    "Instantiate", "Run", "Set", "Return", "If", "Else", "While", "For", "Match", "Break", "Continue"
                };
                for (const auto& kw : stmtKeywords) {
                    if (kw.find(trimmed) == 0 || trimmed.find(kw) != std::string::npos) {
                        looksLikeStatementStart = true;
                        break;
                    }
                }
            }
            if (looksLikeStatementStart || trimmed.empty()) {
                ctx.inStatementPosition = true;
            }
        } else if (ctx.currentClass && !ctx.currentMethod && !ctx.currentConstructor) {
            // Inside class, but outside method body - member declaration position
            ctx.inMemberDeclarationPosition = true;
        } else if (!ctx.currentClass && !ctx.inEntrypoint) {
            // At top level
            ctx.inTopLevelPosition = true;
        }
    }

    // Collect variables in scope
    collectVariablesInScope(ctx, result, line, character);

    // Resolve preceding expression type if needed
    if ((ctx.afterDot || ctx.afterDoubleColon) && !ctx.precedingIdentifier.empty()) {
        ctx.precedingTypeName = resolveExpressionType(ctx.precedingIdentifier, ctx, result);
    }

    return ctx;
}

// Collect variables in scope at cursor position
void AnalysisEngine::collectVariablesInScope(
    CompletionContext& ctx, const AnalysisResult* result,
    int line, int character) const {

    if (!result || !result->ast) return;

    int targetLine = line + 1;  // 1-based

    // Helper to collect from statement list
    auto collectFromStatements = [&](const std::vector<std::unique_ptr<XXML::Parser::Statement>>& stmts) {
        for (const auto& stmt : stmts) {
            if (!stmt || stmt->location.line > targetLine) continue;

            if (auto* inst = dynamic_cast<XXML::Parser::InstantiateStmt*>(stmt.get())) {
                VariableInfo var;
                var.name = inst->variableName;
                var.typeName = inst->type ? inst->type->toString() : "";
                var.ownership = inst->type ? inst->type->ownership : XXML::Parser::OwnershipType::None;
                var.isParameter = false;
                var.isProperty = false;
                var.declarationLine = inst->location.line;
                ctx.variablesInScope.push_back(var);
            } else if (auto* forStmt = dynamic_cast<XXML::Parser::ForStmt*>(stmt.get())) {
                // For loop iterator variable
                if (!forStmt->iteratorName.empty() && forStmt->location.line <= targetLine) {
                    VariableInfo var;
                    var.name = forStmt->iteratorName;
                    var.typeName = forStmt->iteratorType ? forStmt->iteratorType->toString() : "Integer^";
                    var.ownership = forStmt->iteratorType ? forStmt->iteratorType->ownership : XXML::Parser::OwnershipType::Owned;
                    var.isParameter = false;
                    var.isProperty = false;
                    var.declarationLine = forStmt->location.line;
                    ctx.variablesInScope.push_back(var);
                }
            }
        }
    };

    // Collect from class properties if in class context
    if (ctx.currentClass) {
        for (const auto& section : ctx.currentClass->sections) {
            for (const auto& memberDecl : section->declarations) {
                if (auto* prop = dynamic_cast<XXML::Parser::PropertyDecl*>(memberDecl.get())) {
                    VariableInfo var;
                    var.name = prop->name;
                    var.typeName = prop->type ? prop->type->toString() : "";
                    var.ownership = prop->type ? prop->type->ownership : XXML::Parser::OwnershipType::None;
                    var.isParameter = false;
                    var.isProperty = true;
                    var.declarationLine = prop->location.line;
                    ctx.variablesInScope.push_back(var);
                }
            }
        }
    }

    // Collect from method parameters and body
    if (ctx.currentMethod) {
        for (const auto& param : ctx.currentMethod->parameters) {
            VariableInfo var;
            var.name = param->name;
            var.typeName = param->type ? param->type->toString() : "";
            var.ownership = param->type ? param->type->ownership : XXML::Parser::OwnershipType::None;
            var.isParameter = true;
            var.isProperty = false;
            var.declarationLine = param->location.line;
            ctx.variablesInScope.push_back(var);
        }
        collectFromStatements(ctx.currentMethod->body);
    }

    // Collect from constructor parameters and body
    if (ctx.currentConstructor) {
        for (const auto& param : ctx.currentConstructor->parameters) {
            VariableInfo var;
            var.name = param->name;
            var.typeName = param->type ? param->type->toString() : "";
            var.ownership = param->type ? param->type->ownership : XXML::Parser::OwnershipType::None;
            var.isParameter = true;
            var.isProperty = false;
            var.declarationLine = param->location.line;
            ctx.variablesInScope.push_back(var);
        }
        collectFromStatements(ctx.currentConstructor->body);
    }

    // Collect from entrypoint body
    if (ctx.inEntrypoint) {
        for (const auto& decl : result->ast->declarations) {
            if (auto* entrypoint = dynamic_cast<XXML::Parser::EntrypointDecl*>(decl.get())) {
                collectFromStatements(entrypoint->body);
                break;
            }
        }
    }
}

// Resolve expression type for member completions
std::string AnalysisEngine::resolveExpressionType(
    const std::string& exprText, const CompletionContext& ctx,
    const AnalysisResult* result) const {

    if (exprText.empty()) return "";

    // Strip any trailing method call like ".getList()"
    std::string cleanExpr = exprText;
    size_t parenPos = cleanExpr.rfind('(');
    if (parenPos != std::string::npos) {
        // Find matching dot before the method name
        size_t dotPos = cleanExpr.rfind('.', parenPos);
        if (dotPos != std::string::npos) {
            cleanExpr = cleanExpr.substr(0, dotPos);
        }
    }

    // Check if it's a known variable
    for (const auto& var : ctx.variablesInScope) {
        if (var.name == cleanExpr) {
            return var.typeName;
        }
    }

    // Check if it's "this" in a class context
    if (cleanExpr == "this" && ctx.currentClass) {
        return ctx.currentClass->name + "^";
    }

    // Check if it's a class name (for static access)
    if (result && result->analyzer) {
        const auto& classRegistry = result->analyzer->getClassRegistry();

        // Try to find as full or short class name
        for (const auto& [fullName, classInfo] : classRegistry) {
            size_t lastColon = fullName.rfind("::");
            std::string shortName = (lastColon != std::string::npos) ?
                                    fullName.substr(lastColon + 2) : fullName;

            if (shortName == cleanExpr || fullName == cleanExpr) {
                return fullName;  // Return as static type
            }
        }
    }

    // Try to resolve method chain (e.g., "obj.getList")
    size_t dotPos = cleanExpr.rfind('.');
    if (dotPos != std::string::npos) {
        std::string baseExpr = cleanExpr.substr(0, dotPos);
        std::string methodName = cleanExpr.substr(dotPos + 1);

        // Remove () from method name if present
        size_t parenIdx = methodName.find('(');
        if (parenIdx != std::string::npos) {
            methodName = methodName.substr(0, parenIdx);
        }

        // Recursively resolve base expression type
        std::string baseType = resolveExpressionType(baseExpr, ctx, result);
        if (!baseType.empty() && result && result->analyzer) {
            // Look up the method's return type
            std::string cleanBaseType = stripGenerics(stripOwnership(baseType));

            const auto& classRegistry = result->analyzer->getClassRegistry();
            for (const auto& [fullName, classInfo] : classRegistry) {
                size_t lastColon = fullName.rfind("::");
                std::string shortName = (lastColon != std::string::npos) ?
                                        fullName.substr(lastColon + 2) : fullName;

                if (shortName == cleanBaseType || fullName == cleanBaseType) {
                    // Found the class - look for the method
                    auto methodIt = classInfo.methods.find(methodName);
                    if (methodIt != classInfo.methods.end()) {
                        return methodIt->second.returnType;
                    }
                    break;
                }
            }
        }
    }

    return "";
}

// Find the source file for a type in the standard library
std::string AnalysisEngine::findTypeSourceFile(const std::string& typeName) const {
    if (typeName.empty()) return "";

    // Search paths to check
    std::vector<fs::path> searchPaths;

    // Add user-specified include paths (highest priority)
    for (const auto& includePath : includePaths_) {
        searchPaths.push_back(fs::path(includePath));
        // Also check Language/ subdirectory within include path
        searchPaths.push_back(fs::path(includePath) / "Language");
    }

    // Add stdlib path (Language/)
    if (!stdlibPath_.empty()) {
        searchPaths.push_back(fs::path(stdlibPath_));
    }

    // Add workspace root and common locations
    if (!workspaceRoot_.empty()) {
        searchPaths.push_back(fs::path(workspaceRoot_) / "Language");
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "release" / "bin" / "Language");
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "debug" / "bin" / "Language");
    }

    // Common subdirectories to search within Language/
    std::vector<std::string> subdirs = {"Core", "Collections", "System", "IO", "Reflection"};

    for (const auto& basePath : searchPaths) {
        // Try direct file: Language/TypeName.XXML
        fs::path directPath = basePath / (typeName + ".XXML");
        if (fs::exists(directPath) && fs::is_regular_file(directPath)) {
            return directPath.string();
        }

        // Try subdirectories: Language/Core/TypeName.XXML, etc.
        for (const auto& subdir : subdirs) {
            fs::path subPath = basePath / subdir / (typeName + ".XXML");
            if (fs::exists(subPath) && fs::is_regular_file(subPath)) {
                return subPath.string();
            }
        }
    }

    return "";
}

// Parse a type's source file to extract methods and properties for completions
std::vector<CompletionItem> AnalysisEngine::parseTypeForCompletions(const std::string& typeName) const {
    // Check cache first
    auto cacheIt = typeCompletionCache_.find(typeName);
    if (cacheIt != typeCompletionCache_.end()) {
        return cacheIt->second;
    }

    std::vector<CompletionItem> completions;

    // Find the source file
    std::string sourceFile = findTypeSourceFile(typeName);
    if (sourceFile.empty()) {
        // Cache empty result to avoid repeated lookups
        typeCompletionCache_[typeName] = completions;
        return completions;
    }

    // Read and parse the file
    try {
        std::ifstream ifs(sourceFile);
        if (!ifs) {
            typeCompletionCache_[typeName] = completions;
            return completions;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        ifs.close();

        // Create error reporter and parse
        XXML::Common::ErrorReporter errorReporter;
        XXML::Lexer::Lexer lexer(content, fs::path(sourceFile).filename().string(), errorReporter);
        auto tokens = lexer.tokenize();

        if (errorReporter.hasErrors()) {
            typeCompletionCache_[typeName] = completions;
            return completions;
        }

        XXML::Parser::Parser parser(tokens, errorReporter);
        auto ast = parser.parse();

        if (!ast || errorReporter.hasErrors()) {
            typeCompletionCache_[typeName] = completions;
            return completions;
        }

        // Find the class declaration matching our type name
        for (const auto& decl : ast->declarations) {
            // Handle namespace declarations
            if (auto* nsDecl = dynamic_cast<XXML::Parser::NamespaceDecl*>(decl.get())) {
                for (const auto& innerDecl : nsDecl->declarations) {
                    if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(innerDecl.get())) {
                        if (classDecl->name == typeName) {
                            // Extract methods and properties from this class
                            for (const auto& section : classDecl->sections) {
                                // Skip private sections for completions
                                if (section->modifier == XXML::Parser::AccessModifier::Private) {
                                    continue;
                                }

                                for (const auto& memberDecl : section->declarations) {
                                    if (auto* method = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                                        // Skip constructors for instance access (they're accessed via ::)
                                        if (method->name == "Constructor") continue;

                                        CompletionItem item;
                                        item.label = method->name;
                                        item.kind = CompletionItemKind::Method;

                                        // Build parameter string
                                        std::string params = "(";
                                        for (size_t i = 0; i < method->parameters.size(); ++i) {
                                            if (i > 0) params += ", ";
                                            if (method->parameters[i]->type) {
                                                params += method->parameters[i]->type->toString();
                                            }
                                        }
                                        params += ")";

                                        std::string returnType = method->returnType ?
                                            method->returnType->toString() : "Void";

                                        item.detail = params + " → " + returnType;
                                        item.insertText = method->name + "()";
                                        completions.push_back(item);
                                    } else if (auto* prop = dynamic_cast<XXML::Parser::PropertyDecl*>(memberDecl.get())) {
                                        CompletionItem item;
                                        item.label = prop->name;
                                        item.kind = CompletionItemKind::Property;
                                        item.detail = prop->type ? prop->type->toString() : "unknown";
                                        item.insertText = prop->name;
                                        completions.push_back(item);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            } else if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(decl.get())) {
                if (classDecl->name == typeName) {
                    // Extract methods and properties from this class
                    for (const auto& section : classDecl->sections) {
                        if (section->modifier == XXML::Parser::AccessModifier::Private) {
                            continue;
                        }

                        for (const auto& memberDecl : section->declarations) {
                            if (auto* method = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                                if (method->name == "Constructor") continue;

                                CompletionItem item;
                                item.label = method->name;
                                item.kind = CompletionItemKind::Method;

                                std::string params = "(";
                                for (size_t i = 0; i < method->parameters.size(); ++i) {
                                    if (i > 0) params += ", ";
                                    if (method->parameters[i]->type) {
                                        params += method->parameters[i]->type->toString();
                                    }
                                }
                                params += ")";

                                std::string returnType = method->returnType ?
                                    method->returnType->toString() : "Void";

                                item.detail = params + " → " + returnType;
                                item.insertText = method->name + "()";
                                completions.push_back(item);
                            } else if (auto* prop = dynamic_cast<XXML::Parser::PropertyDecl*>(memberDecl.get())) {
                                CompletionItem item;
                                item.label = prop->name;
                                item.kind = CompletionItemKind::Property;
                                item.detail = prop->type ? prop->type->toString() : "unknown";
                                item.insertText = prop->name;
                                completions.push_back(item);
                            }
                        }
                    }
                    break;
                }
            }
        }
    } catch (...) {
        // Silently fail on parse errors
    }

    // Cache the result
    typeCompletionCache_[typeName] = completions;
    return completions;
}

// Get member completions (after .)
std::vector<CompletionItem> AnalysisEngine::getMemberCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    // Strip ownership and generics for lookup
    std::string typeName = stripGenerics(stripOwnership(ctx.precedingTypeName));

    // If we have the analyzer's class registry, use it first (most accurate)
    if (!typeName.empty() && result && result->analyzer) {
        const auto& classRegistry = result->analyzer->getClassRegistry();

        for (const auto& [fullName, classInfo] : classRegistry) {
            size_t lastColon = fullName.rfind("::");
            std::string shortName = (lastColon != std::string::npos) ?
                                    fullName.substr(lastColon + 2) : fullName;

            if (shortName == typeName || fullName == typeName) {
                // Found in registry - use it
                for (const auto& [methodName, methodInfo] : classInfo.methods) {
                    if (methodName == "Constructor") continue;

                    CompletionItem item;
                    item.label = methodName;
                    item.kind = CompletionItemKind::Method;

                    std::string detail = "(";
                    for (size_t i = 0; i < methodInfo.parameters.size(); ++i) {
                        if (i > 0) detail += ", ";
                        detail += methodInfo.parameters[i].first;
                    }
                    detail += ") → " + methodInfo.returnType;
                    item.detail = detail;

                    item.insertText = methodName + "()";
                    completions.push_back(item);
                }

                for (const auto& [propName, propInfo] : classInfo.properties) {
                    CompletionItem item;
                    item.label = propName;
                    item.kind = CompletionItemKind::Property;
                    item.detail = propInfo.first;
                    item.insertText = propName;
                    completions.push_back(item);
                }

                return completions;
            }
        }
    }

    // Try to parse the type's source file directly (works without analyzer)
    if (!typeName.empty()) {
        completions = parseTypeForCompletions(typeName);
        if (!completions.empty()) {
            return completions;
        }
    }

    // Fallback: common Object methods (only when we truly can't determine the type)
    std::vector<std::pair<std::string, std::string>> commonMethods = {
        {"toString", "String^"},
        {"equals", "Bool^"},
        {"hash", "Integer^"},
        {"dispose", "Void"},
    };
    for (const auto& [name, retType] : commonMethods) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Method;
        item.detail = "→ " + retType;
        item.insertText = name + "()";
        completions.push_back(item);
    }
    return completions;
}

// Parse a type's source file for static access (::) - includes Constructor and all public methods
std::vector<CompletionItem> AnalysisEngine::parseTypeForStaticCompletions(const std::string& typeName) const {
    // Check cache first (use different key to distinguish from instance completions)
    std::string cacheKey = typeName + "::static";
    auto cacheIt = typeCompletionCache_.find(cacheKey);
    if (cacheIt != typeCompletionCache_.end()) {
        return cacheIt->second;
    }

    std::vector<CompletionItem> completions;

    // Find the source file
    std::string sourceFile = findTypeSourceFile(typeName);
    if (sourceFile.empty()) {
        typeCompletionCache_[cacheKey] = completions;
        return completions;
    }

    // Read and parse the file
    try {
        std::ifstream ifs(sourceFile);
        if (!ifs) {
            typeCompletionCache_[cacheKey] = completions;
            return completions;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        ifs.close();

        XXML::Common::ErrorReporter errorReporter;
        XXML::Lexer::Lexer lexer(content, fs::path(sourceFile).filename().string(), errorReporter);
        auto tokens = lexer.tokenize();

        if (errorReporter.hasErrors()) {
            typeCompletionCache_[cacheKey] = completions;
            return completions;
        }

        XXML::Parser::Parser parser(tokens, errorReporter);
        auto ast = parser.parse();

        if (!ast || errorReporter.hasErrors()) {
            typeCompletionCache_[cacheKey] = completions;
            return completions;
        }

        // Helper lambda to extract completions from a class
        auto extractFromClass = [&](XXML::Parser::ClassDecl* classDecl) {
            // Add Constructor first
            CompletionItem ctorItem;
            ctorItem.label = "Constructor";
            ctorItem.kind = CompletionItemKind::Constructor;
            ctorItem.detail = "Create new " + typeName;
            ctorItem.insertText = "Constructor()";
            completions.push_back(ctorItem);

            // Extract all public methods and properties
            for (const auto& section : classDecl->sections) {
                if (section->modifier == XXML::Parser::AccessModifier::Private) {
                    continue;
                }

                for (const auto& memberDecl : section->declarations) {
                    if (auto* method = dynamic_cast<XXML::Parser::MethodDecl*>(memberDecl.get())) {
                        // Include Constructor methods (factory methods) for static access
                        CompletionItem item;
                        item.label = method->name;
                        item.kind = (method->name == "Constructor") ?
                            CompletionItemKind::Constructor : CompletionItemKind::Method;

                        std::string params = "(";
                        for (size_t i = 0; i < method->parameters.size(); ++i) {
                            if (i > 0) params += ", ";
                            if (method->parameters[i]->type) {
                                params += method->parameters[i]->type->toString();
                            }
                        }
                        params += ")";

                        std::string returnType = method->returnType ?
                            method->returnType->toString() : "Void";

                        item.detail = params + " → " + returnType;
                        item.insertText = method->name + "()";
                        completions.push_back(item);
                    } else if (auto* prop = dynamic_cast<XXML::Parser::PropertyDecl*>(memberDecl.get())) {
                        CompletionItem item;
                        item.label = prop->name;
                        item.kind = CompletionItemKind::Property;
                        item.detail = prop->type ? prop->type->toString() : "unknown";
                        item.insertText = prop->name;
                        completions.push_back(item);
                    }
                }
            }
        };

        // Find the class declaration
        for (const auto& decl : ast->declarations) {
            if (auto* nsDecl = dynamic_cast<XXML::Parser::NamespaceDecl*>(decl.get())) {
                for (const auto& innerDecl : nsDecl->declarations) {
                    if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(innerDecl.get())) {
                        if (classDecl->name == typeName) {
                            extractFromClass(classDecl);
                            break;
                        }
                    }
                }
            } else if (auto* classDecl = dynamic_cast<XXML::Parser::ClassDecl*>(decl.get())) {
                if (classDecl->name == typeName) {
                    extractFromClass(classDecl);
                    break;
                }
            }
        }
    } catch (...) {
        // Silently fail
    }

    typeCompletionCache_[cacheKey] = completions;
    return completions;
}

// Find namespace directory and list its contents
std::vector<CompletionItem> AnalysisEngine::getNamespaceContents(const std::string& namespacePath) const {
    std::vector<CompletionItem> completions;

    // Convert namespace path (e.g., "Language::Core") to directory path
    std::string dirPath = namespacePath;
    size_t pos = 0;
    while ((pos = dirPath.find("::", pos)) != std::string::npos) {
        dirPath.replace(pos, 2, "/");
    }

    // Search paths
    std::vector<fs::path> searchPaths;

    // Add user-specified include paths (highest priority)
    for (const auto& includePath : includePaths_) {
        searchPaths.push_back(fs::path(includePath));
    }

    if (!stdlibPath_.empty()) {
        // stdlibPath_ is typically "Language/", so go up one level
        fs::path stdlibParent = fs::path(stdlibPath_).parent_path();
        searchPaths.push_back(stdlibParent);
        searchPaths.push_back(fs::path(stdlibPath_));
    }
    if (!workspaceRoot_.empty()) {
        searchPaths.push_back(fs::path(workspaceRoot_));
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "release" / "bin");
        searchPaths.push_back(fs::path(workspaceRoot_) / "build" / "debug" / "bin");
    }

    std::set<std::string> addedNames;

    for (const auto& basePath : searchPaths) {
        fs::path nsDir = basePath / dirPath;

        if (!fs::exists(nsDir) || !fs::is_directory(nsDir)) {
            continue;
        }

        try {
            for (const auto& entry : fs::directory_iterator(nsDir)) {
                std::string name = entry.path().stem().string();

                // Skip if already added
                if (addedNames.find(name) != addedNames.end()) continue;

                if (entry.is_directory()) {
                    // Sub-namespace
                    addedNames.insert(name);
                    CompletionItem item;
                    item.label = name;
                    item.kind = CompletionItemKind::Module;
                    item.detail = "Namespace";
                    item.insertText = name;
                    completions.push_back(item);
                } else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".XXML" || ext == ".xxml") {
                        // Class file
                        addedNames.insert(name);
                        CompletionItem item;
                        item.label = name;
                        item.kind = CompletionItemKind::Class;
                        item.detail = "Class";
                        item.insertText = name;
                        completions.push_back(item);
                    }
                }
            }
        } catch (...) {
            // Ignore filesystem errors
        }

        // If we found content, stop searching
        if (!completions.empty()) {
            break;
        }
    }

    return completions;
}

// Get static completions (after ::)
std::vector<CompletionItem> AnalysisEngine::getStaticCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    if (ctx.precedingIdentifier.empty()) return completions;

    std::string qualifiedName = ctx.precedingIdentifier;

    // First, try the analyzer's class registry (if available)
    if (result && result->analyzer) {
        const auto& classRegistry = result->analyzer->getClassRegistry();

        // Check if it's a class
        std::string foundClassName;
        for (const auto& [name, info] : classRegistry) {
            size_t lastColon = name.rfind("::");
            std::string shortName = (lastColon != std::string::npos) ?
                                    name.substr(lastColon + 2) : name;

            if (shortName == qualifiedName || name == qualifiedName) {
                foundClassName = name;
                break;
            }
        }

        if (!foundClassName.empty()) {
            // It's a class - offer Constructor and all public methods
            CompletionItem ctorItem;
            ctorItem.label = "Constructor";
            ctorItem.kind = CompletionItemKind::Constructor;
            ctorItem.detail = "Create new " + qualifiedName;
            ctorItem.insertText = "Constructor()";
            completions.push_back(ctorItem);

            auto it = classRegistry.find(foundClassName);
            if (it != classRegistry.end()) {
                for (const auto& [methodName, methodInfo] : it->second.methods) {
                    CompletionItem item;
                    item.label = methodName;
                    item.kind = CompletionItemKind::Method;

                    std::string detail = "(";
                    for (size_t i = 0; i < methodInfo.parameters.size(); ++i) {
                        if (i > 0) detail += ", ";
                        detail += methodInfo.parameters[i].first;
                    }
                    detail += ") → " + methodInfo.returnType;
                    item.detail = detail;

                    item.insertText = methodName + "()";
                    completions.push_back(item);
                }

                for (const auto& [propName, propInfo] : it->second.properties) {
                    CompletionItem item;
                    item.label = propName;
                    item.kind = CompletionItemKind::Property;
                    item.detail = propInfo.first;
                    item.insertText = propName;
                    completions.push_back(item);
                }
            }
            return completions;
        } else {
            // Might be a namespace - show classes/namespaces from registry
            std::string nsPrefix = qualifiedName + "::";
            std::set<std::string> addedNames;

            for (const auto& [name, info] : classRegistry) {
                if (name.find(nsPrefix) == 0) {
                    std::string remainder = name.substr(nsPrefix.size());
                    size_t nextColon = remainder.find("::");
                    std::string nextPart = (nextColon != std::string::npos) ?
                                           remainder.substr(0, nextColon) : remainder;

                    if (addedNames.find(nextPart) != addedNames.end()) continue;
                    addedNames.insert(nextPart);

                    CompletionItem item;
                    item.label = nextPart;
                    item.kind = (nextColon != std::string::npos) ?
                                CompletionItemKind::Module : CompletionItemKind::Class;
                    item.detail = (nextColon != std::string::npos) ? "Namespace" : "Class";
                    item.insertText = nextPart;
                    completions.push_back(item);
                }
            }

            if (!completions.empty()) {
                return completions;
            }
        }
    }

    // Second, try to parse the type's source file directly (works without analyzer)
    // Check if it looks like a class name (starts with uppercase)
    if (!qualifiedName.empty()) {
        // Extract the last component for class lookup
        std::string typeName = qualifiedName;
        size_t lastColon = qualifiedName.rfind("::");
        if (lastColon != std::string::npos) {
            typeName = qualifiedName.substr(lastColon + 2);
        }

        // Try as a class first
        completions = parseTypeForStaticCompletions(typeName);
        if (!completions.empty()) {
            return completions;
        }

        // Try as a namespace
        completions = getNamespaceContents(qualifiedName);
        if (!completions.empty()) {
            return completions;
        }
    }

    // Fallback: Constructor if it looks like a class name
    if (completions.empty() && !qualifiedName.empty() && std::isupper(qualifiedName[0])) {
        CompletionItem ctorItem;
        ctorItem.label = "Constructor";
        ctorItem.kind = CompletionItemKind::Constructor;
        ctorItem.detail = "Class constructor";
        ctorItem.insertText = "Constructor()";
        completions.push_back(ctorItem);
    }

    return completions;
}

// Get type completions (in type position)
std::vector<CompletionItem> AnalysisEngine::getTypeCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    // Built-in types
    std::vector<std::pair<std::string, std::string>> builtinTypes = {
        {"Integer", "64-bit signed integer"},
        {"String", "UTF-8 string"},
        {"Bool", "Boolean (true/false)"},
        {"Float", "32-bit floating point"},
        {"Double", "64-bit floating point"},
        {"Char", "Single character"},
        {"Void", "No value"},
        {"Object", "Base object type"},
    };

    for (const auto& [name, desc] : builtinTypes) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Class;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    // NativeType and NativeStruct
    CompletionItem nativeItem;
    nativeItem.label = "NativeType";
    nativeItem.kind = CompletionItemKind::Class;
    nativeItem.detail = "Low-level native type";
    nativeItem.insertText = "NativeType<\"\">";
    completions.push_back(nativeItem);

    // Classes from registry
    std::set<std::string> addedNames;
    std::set<std::string> namespaces;

    if (result && result->analyzer) {
        for (const auto& [fullName, classInfo] : result->analyzer->getClassRegistry()) {
            size_t lastColon = fullName.rfind("::");
            std::string shortName = (lastColon != std::string::npos) ?
                                    fullName.substr(lastColon + 2) : fullName;

            if (addedNames.find(shortName) == addedNames.end()) {
                addedNames.insert(shortName);
                CompletionItem item;
                item.label = shortName;
                item.kind = CompletionItemKind::Class;
                item.detail = (lastColon != std::string::npos) ? fullName : "Class";
                item.insertText = shortName;
                completions.push_back(item);
            }

            // Extract namespace
            if (lastColon != std::string::npos) {
                namespaces.insert(fullName.substr(0, lastColon));
            }
        }
    }

    // Add namespace suggestions
    for (const auto& ns : namespaces) {
        if (addedNames.find(ns) == addedNames.end()) {
            addedNames.insert(ns);
            CompletionItem item;
            item.label = ns;
            item.kind = CompletionItemKind::Module;
            item.detail = "Namespace";
            item.insertText = ns + "::";
            completions.push_back(item);
        }
    }

    // Generic collection types
    std::vector<std::pair<std::string, std::string>> genericTypes = {
        {"List", "Dynamic array - List<T>^"},
        {"HashMap", "Key-value map - HashMap<K,V>^"},
        {"Set", "Unique set - Set<T>^"},
    };

    for (const auto& [name, desc] : genericTypes) {
        if (addedNames.find(name) == addedNames.end()) {
            CompletionItem item;
            item.label = name;
            item.kind = CompletionItemKind::Class;
            item.detail = desc;
            item.insertText = name + "<>";
            completions.push_back(item);
        }
    }

    return completions;
}

// Get general completions (keywords, variables, types)
std::vector<CompletionItem> AnalysisEngine::getGeneralCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    // Keywords
    std::vector<std::tuple<std::string, std::string, CompletionItemKind>> keywords = {
        // Declarations
        {"Class", "Declare a class", CompletionItemKind::Keyword},
        {"Struct", "Declare a struct", CompletionItemKind::Keyword},
        {"Interface", "Declare an interface", CompletionItemKind::Keyword},
        {"Trait", "Declare a trait", CompletionItemKind::Keyword},
        {"Enum", "Declare an enum", CompletionItemKind::Keyword},
        {"Annotation", "Declare an annotation", CompletionItemKind::Keyword},
        {"Namespace", "Declare a namespace", CompletionItemKind::Keyword},
        {"Entrypoint", "Program entry point", CompletionItemKind::Keyword},
        // Members
        {"Method", "Declare a method", CompletionItemKind::Keyword},
        {"Property", "Declare a property", CompletionItemKind::Keyword},
        {"Constructor", "Declare a constructor", CompletionItemKind::Keyword},
        {"Destructor", "Declare a destructor", CompletionItemKind::Keyword},
        {"Operator", "Declare an operator", CompletionItemKind::Keyword},
        // Statements
        {"Instantiate", "Create a variable", CompletionItemKind::Keyword},
        {"Run", "Execute statement", CompletionItemKind::Keyword},
        {"Set", "Assign value", CompletionItemKind::Keyword},
        {"Return", "Return from method", CompletionItemKind::Keyword},
        {"Exit", "Exit program", CompletionItemKind::Keyword},
        // Control flow
        {"If", "Conditional", CompletionItemKind::Keyword},
        {"Else", "Else branch", CompletionItemKind::Keyword},
        {"While", "While loop", CompletionItemKind::Keyword},
        {"For", "For loop", CompletionItemKind::Keyword},
        {"Match", "Pattern match", CompletionItemKind::Keyword},
        {"Break", "Break loop", CompletionItemKind::Keyword},
        {"Continue", "Continue loop", CompletionItemKind::Keyword},
        // Modifiers
        {"Public", "Public access", CompletionItemKind::Keyword},
        {"Private", "Private access", CompletionItemKind::Keyword},
        {"Protected", "Protected access", CompletionItemKind::Keyword},
        {"Final", "Non-inheritable", CompletionItemKind::Keyword},
        {"Static", "Class-level member", CompletionItemKind::Keyword},
        {"Abstract", "Abstract member", CompletionItemKind::Keyword},
        {"Virtual", "Virtual method", CompletionItemKind::Keyword},
        {"Override", "Override method", CompletionItemKind::Keyword},
        {"Compiletime", "Compile-time eval", CompletionItemKind::Keyword},
        // Type annotations
        {"Types", "Type annotation", CompletionItemKind::Keyword},
        {"Returns", "Return type", CompletionItemKind::Keyword},
        {"Parameters", "Parameter list", CompletionItemKind::Keyword},
        {"Parameter", "Single parameter", CompletionItemKind::Keyword},
        {"Extends", "Base class", CompletionItemKind::Keyword},
        {"Implements", "Interface impl", CompletionItemKind::Keyword},
        {"Do", "Method body", CompletionItemKind::Keyword},
        {"As", "Variable name", CompletionItemKind::Keyword},
        {"In", "Collection iter", CompletionItemKind::Keyword},
        // Values
        {"true", "Boolean true", CompletionItemKind::Keyword},
        {"false", "Boolean false", CompletionItemKind::Keyword},
        {"null", "Null reference", CompletionItemKind::Keyword},
        {"None", "No value/base", CompletionItemKind::Keyword},
        {"this", "Current instance", CompletionItemKind::Keyword},
        {"Self", "Current type", CompletionItemKind::Keyword},
        {"default", "Default value", CompletionItemKind::Keyword},
    };

    for (const auto& [label, detail, kind] : keywords) {
        CompletionItem item;
        item.label = label;
        item.kind = kind;
        item.detail = detail;
        item.insertText = label;
        completions.push_back(item);
    }

    // Add variables in scope
    for (const auto& var : ctx.variablesInScope) {
        CompletionItem item;
        item.label = var.name;
        item.kind = var.isProperty ? CompletionItemKind::Property :
                    var.isParameter ? CompletionItemKind::Variable :
                    CompletionItemKind::Variable;
        item.detail = var.typeName;
        item.insertText = var.name;
        completions.push_back(item);
    }

    // Add types
    auto typeCompletions = getTypeCompletions(ctx, result);
    for (auto& item : typeCompletions) {
        completions.push_back(std::move(item));
    }

    // Deduplicate by label
    std::set<std::string> seen;
    std::vector<CompletionItem> deduped;
    for (auto& item : completions) {
        if (seen.find(item.label) == seen.end()) {
            seen.insert(item.label);
            deduped.push_back(std::move(item));
        }
    }

    return deduped;
}

// Get expression completions (variables, class names for construction, literals)
std::vector<CompletionItem> AnalysisEngine::getExpressionCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    // Add variables in scope - these are the primary expression starters
    for (const auto& var : ctx.variablesInScope) {
        CompletionItem item;
        item.label = var.name;
        item.kind = var.isProperty ? CompletionItemKind::Property :
                    var.isParameter ? CompletionItemKind::Variable :
                    CompletionItemKind::Variable;
        item.detail = var.typeName;
        item.insertText = var.name;
        completions.push_back(item);
    }

    // Add "this" if in class context
    if (ctx.currentClass) {
        CompletionItem thisItem;
        thisItem.label = "this";
        thisItem.kind = CompletionItemKind::Keyword;
        thisItem.detail = ctx.currentClass->name + "&";
        thisItem.insertText = "this";
        completions.push_back(thisItem);
    }

    // Add class names for constructor calls
    if (result && result->analyzer) {
        std::set<std::string> addedNames;
        for (const auto& [fullName, classInfo] : result->analyzer->getClassRegistry()) {
            size_t lastColon = fullName.rfind("::");
            std::string shortName = (lastColon != std::string::npos) ?
                                    fullName.substr(lastColon + 2) : fullName;

            if (addedNames.find(shortName) == addedNames.end()) {
                addedNames.insert(shortName);
                CompletionItem item;
                item.label = shortName;
                item.kind = CompletionItemKind::Class;
                item.detail = "Class (use ::Constructor)";
                item.insertText = shortName;
                completions.push_back(item);
            }
        }
    }

    // Literal keywords
    std::vector<std::pair<std::string, std::string>> literals = {
        {"true", "Boolean true"},
        {"false", "Boolean false"},
        {"null", "Null reference"},
    };
    for (const auto& [name, desc] : literals) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Keyword;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    return completions;
}

// Get statement completions (statement keywords at start of line in method body)
std::vector<CompletionItem> AnalysisEngine::getStatementCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    std::vector<CompletionItem> completions;

    // Statement keywords only - no declaration or modifier keywords
    std::vector<std::pair<std::string, std::string>> stmtKeywords = {
        {"Instantiate", "Create a local variable"},
        {"Run", "Execute an expression"},
        {"Set", "Assign a value"},
        {"Return", "Return from method"},
        {"If", "Conditional statement"},
        {"While", "While loop"},
        {"For", "For loop"},
        {"Match", "Pattern matching"},
        {"Break", "Break out of loop"},
        {"Continue", "Continue to next iteration"},
    };

    // Add Exit only in entrypoint
    if (ctx.inEntrypoint) {
        stmtKeywords.push_back({"Exit", "Exit the program"});
    }

    for (const auto& [name, desc] : stmtKeywords) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Keyword;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    // Also add variables (they can start expressions used in statements)
    for (const auto& var : ctx.variablesInScope) {
        CompletionItem item;
        item.label = var.name;
        item.kind = CompletionItemKind::Variable;
        item.detail = var.typeName;
        item.insertText = var.name;
        completions.push_back(item);
    }

    return completions;
}

// Get member declaration completions (inside class, outside method body)
std::vector<CompletionItem> AnalysisEngine::getMemberDeclarationCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    (void)ctx;
    (void)result;

    std::vector<CompletionItem> completions;

    // Member declaration keywords only
    std::vector<std::pair<std::string, std::string>> memberKeywords = {
        {"Method", "Declare a method"},
        {"Property", "Declare a property"},
        {"Constructor", "Declare a constructor"},
        {"Destructor", "Declare a destructor"},
        {"Operator", "Declare an operator overload"},
    };

    for (const auto& [name, desc] : memberKeywords) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Keyword;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    // Access modifiers (for starting new access section)
    std::vector<std::pair<std::string, std::string>> accessKeywords = {
        {"Public", "Public access section"},
        {"Private", "Private access section"},
        {"Protected", "Protected access section"},
    };

    for (const auto& [name, desc] : accessKeywords) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Keyword;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    return completions;
}

// Get top-level declaration completions (at file top level)
std::vector<CompletionItem> AnalysisEngine::getTopLevelCompletions(
    const CompletionContext& ctx, const AnalysisResult* result) const {

    (void)ctx;
    (void)result;

    std::vector<CompletionItem> completions;

    // Top-level declaration keywords only
    std::vector<std::pair<std::string, std::string>> topLevelKeywords = {
        {"Class", "Declare a class"},
        {"Structure", "Declare a structure"},
        {"Interface", "Declare an interface"},
        {"Trait", "Declare a trait"},
        {"Enumeration", "Declare an enumeration"},
        {"Annotation", "Declare an annotation"},
        {"Namespace", "Declare a namespace"},
        {"Entrypoint", "Program entry point"},
        {"#import", "Import a module"},
    };

    for (const auto& [name, desc] : topLevelKeywords) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Keyword;
        item.detail = desc;
        item.insertText = name;
        completions.push_back(item);
    }

    return completions;
}

} // namespace xxml::lsp
