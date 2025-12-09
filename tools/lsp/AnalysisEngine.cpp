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

    std::vector<CompletionItem> completions;
    auto* result = getAnalysis(uri);

    // Helper to add all known types (includes namespaces and fully-qualified names)
    auto addAllTypes = [&]() {
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

        // NativeType and NativeStruct suggestions
        CompletionItem nativeItem;
        nativeItem.label = "NativeType";
        nativeItem.kind = CompletionItemKind::Class;
        nativeItem.detail = "Low-level native type";
        nativeItem.insertText = "NativeType<\"\">"; // No snippet placeholders
        completions.push_back(nativeItem);

        CompletionItem nativeStructItem;
        nativeStructItem.label = "NativeStruct";
        nativeStructItem.kind = CompletionItemKind::Struct;
        nativeStructItem.detail = "Low-level native struct";
        nativeStructItem.insertText = "NativeStruct<\"\">"; // No snippet placeholders
        completions.push_back(nativeStructItem);

        // Collect namespaces and classes from registry
        std::set<std::string> addedNames;
        std::set<std::string> namespaces;

        if (result && result->analyzer) {
            for (const auto& [fullName, classInfo] : result->analyzer->getClassRegistry()) {
                // Add the short name
                size_t lastColon = fullName.rfind("::");
                std::string shortName = (lastColon != std::string::npos) ? fullName.substr(lastColon + 2) : fullName;

                if (addedNames.find(shortName) == addedNames.end()) {
                    addedNames.insert(shortName);
                    CompletionItem item;
                    item.label = shortName;
                    item.kind = CompletionItemKind::Class;
                    item.detail = (lastColon != std::string::npos) ? fullName : "Class";
                    item.insertText = shortName;
                    completions.push_back(item);
                }

                // Also add the fully-qualified name if different
                if (lastColon != std::string::npos && addedNames.find(fullName) == addedNames.end()) {
                    addedNames.insert(fullName);
                    CompletionItem item;
                    item.label = fullName;
                    item.kind = CompletionItemKind::Class;
                    item.detail = "Fully qualified";
                    item.insertText = fullName;
                    completions.push_back(item);

                    // Extract namespace
                    std::string ns = fullName.substr(0, lastColon);
                    namespaces.insert(ns);
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

        // Common XXML namespaces
        std::vector<std::string> commonNamespaces = {
            "Language::Core",
            "Language::Collections",
            "Language::System",
            "Language::IO",
            "Language::Math",
        };
        for (const auto& ns : commonNamespaces) {
            if (addedNames.find(ns) == addedNames.end()) {
                addedNames.insert(ns);
                CompletionItem item;
                item.label = ns;
                item.kind = CompletionItemKind::Module;
                item.detail = "Standard library namespace";
                item.insertText = ns + "::";
                completions.push_back(item);
            }
        }

        // Generic collection types
        std::vector<std::pair<std::string, std::string>> genericTypes = {
            {"List", "Dynamic array - List<T>^"},
            {"HashMap", "Key-value map - HashMap<K,V>^"},
            {"Set", "Unique set - Set<T>^"},
            {"Optional", "Optional value - Optional<T>"},
            {"Result", "Result type - Result<T,E>"},
        };
        for (const auto& [name, desc] : genericTypes) {
            if (addedNames.find(name) == addedNames.end()) {
                CompletionItem item;
                item.label = name;
                item.kind = CompletionItemKind::Class;
                item.detail = desc;
                item.insertText = name + "<>"; // No snippet placeholders
                completions.push_back(item);
            }
        }
    };

    // Helper to add local variables
    auto addLocalVariables = [&]() {
        if (result && result->context) {
            auto& symbolTable = result->context->symbolTable();
            auto* currentScope = symbolTable.getCurrentScope();
            while (currentScope) {
                for (const auto& [symName, sym] : currentScope->getSymbols()) {
                    if (sym->kind == XXML::Semantic::SymbolKind::LocalVariable ||
                        sym->kind == XXML::Semantic::SymbolKind::Parameter) {
                        CompletionItem item;
                        item.label = sym->name;
                        item.kind = CompletionItemKind::Variable;
                        item.detail = sym->typeName;
                        item.insertText = sym->name;
                        completions.push_back(item);
                    }
                }
                currentScope = currentScope->getParent();
            }
        }
    };

    // Detect context from preceding text
    std::string trimmed = precedingText;
    // Trim trailing whitespace for context detection
    while (!trimmed.empty() && std::isspace(trimmed.back())) {
        trimmed.pop_back();
    }

    // Check for specific contexts
    bool afterDoubleColon = precedingText.size() >= 2 &&
                            precedingText.substr(precedingText.size() - 2) == "::";
    bool afterDot = !precedingText.empty() && precedingText.back() == '.';

    // Helper to find the last keyword in the text
    auto findLastKeyword = [](const std::string& text) -> std::string {
        // Keywords that expect specific completions to follow
        std::vector<std::string> typeKeywords = {
            "Instantiate", "Types", "Returns", "Extends", "Implements", "Parameter", "Run", "Set"
        };
        for (const auto& kw : typeKeywords) {
            // Look for keyword followed by space (and possibly partial type)
            size_t pos = text.rfind(kw);
            if (pos != std::string::npos) {
                // Check if it's a complete word (not part of another word)
                bool validStart = (pos == 0 || !std::isalnum(text[pos - 1]));
                bool validEnd = (pos + kw.size() >= text.size() ||
                                 !std::isalnum(text[pos + kw.size()]) ||
                                 std::isspace(text[pos + kw.size()]));
                if (validStart) {
                    // Check what comes after the keyword
                    size_t afterKw = pos + kw.size();
                    if (afterKw < text.size()) {
                        // There's content after keyword - check if it's just space + partial word
                        std::string after = text.substr(afterKw);
                        // Skip whitespace
                        size_t nonSpace = after.find_first_not_of(" \t");
                        if (nonSpace == std::string::npos) {
                            // Only whitespace after keyword
                            return kw;
                        }
                        // Check if what follows looks like start of a type (no other keywords)
                        std::string rest = after.substr(nonSpace);
                        // If the rest is just identifier chars (possibly partial type), we're in type context
                        bool allIdentChars = true;
                        for (char c : rest) {
                            if (!std::isalnum(c) && c != '_' && c != ':' && c != '<' && c != '>' && c != ',' && c != '^' && c != '&' && c != '%') {
                                allIdentChars = false;
                                break;
                            }
                        }
                        if (allIdentChars) {
                            return kw;
                        }
                    } else {
                        return kw;
                    }
                }
            }
        }
        return "";
    };

    // Find the context keyword
    std::string contextKeyword = findLastKeyword(trimmed);

    // Type context - only show types, no keywords
    if (contextKeyword == "Instantiate" || contextKeyword == "Types" ||
        contextKeyword == "Returns" || contextKeyword == "Parameter") {
        addAllTypes();
        return completions;
    }

    // After "Extends " - show class names
    if (contextKeyword == "Extends") {
        if (result && result->analyzer) {
            for (const auto& [name, classInfo] : result->analyzer->getClassRegistry()) {
                size_t lastColon = name.rfind("::");
                std::string shortName = (lastColon != std::string::npos) ? name.substr(lastColon + 2) : name;
                CompletionItem item;
                item.label = shortName;
                item.kind = CompletionItemKind::Class;
                item.detail = "Base class";
                item.insertText = shortName;
                completions.push_back(item);
            }
        }
        // Add None for no base class
        CompletionItem noneItem;
        noneItem.label = "None";
        noneItem.kind = CompletionItemKind::Keyword;
        noneItem.detail = "No base class";
        noneItem.insertText = "None";
        completions.push_back(noneItem);
        return completions;
    }

    // After "Run " - show variables and class names for method calls
    if (contextKeyword == "Run") {
        addLocalVariables();
        // Add class names for static calls (with namespaces)
        if (result && result->analyzer) {
            std::set<std::string> addedNames;
            for (const auto& [fullName, classInfo] : result->analyzer->getClassRegistry()) {
                size_t lastColon = fullName.rfind("::");
                std::string shortName = (lastColon != std::string::npos) ? fullName.substr(lastColon + 2) : fullName;
                if (addedNames.find(shortName) == addedNames.end()) {
                    addedNames.insert(shortName);
                    CompletionItem item;
                    item.label = shortName;
                    item.kind = CompletionItemKind::Class;
                    item.detail = (lastColon != std::string::npos) ? fullName : "Class";
                    item.insertText = shortName;
                    completions.push_back(item);
                }
            }
        }
        return completions;
    }

    // After "Set " - show local variables and properties
    if (contextKeyword == "Set") {
        addLocalVariables();
        // Also show 'this' for setting properties
        CompletionItem thisItem;
        thisItem.label = "this";
        thisItem.kind = CompletionItemKind::Keyword;
        thisItem.detail = "Current instance";
        thisItem.insertText = "this";
        completions.push_back(thisItem);
        return completions;
    }

    // After "::" - show class/namespace members
    if (afterDoubleColon) {
        size_t colonPos = precedingText.rfind("::");
        if (colonPos != std::string::npos && colonPos > 0) {
            // Find the name before ::
            size_t nameEnd = colonPos;
            size_t nameStart = nameEnd;
            while (nameStart > 0 && (std::isalnum(precedingText[nameStart - 1]) ||
                                      precedingText[nameStart - 1] == '_' ||
                                      precedingText[nameStart - 1] == ':')) {
                --nameStart;
            }

            std::string qualifiedName = precedingText.substr(nameStart, nameEnd - nameStart);

            // Check if it's a namespace (e.g., "System::Console::")
            // or a class (e.g., "String::")
            if (result && result->analyzer) {
                const auto& classRegistry = result->analyzer->getClassRegistry();

                // Try to find as class first
                std::string foundClassName;
                for (const auto& [name, info] : classRegistry) {
                    size_t lastColon = name.rfind("::");
                    std::string shortName = (lastColon != std::string::npos) ? name.substr(lastColon + 2) : name;
                    if (shortName == qualifiedName || name == qualifiedName) {
                        foundClassName = name;
                        break;
                    }
                }

                if (!foundClassName.empty()) {
                    auto it = classRegistry.find(foundClassName);
                    if (it != classRegistry.end()) {
                        const auto& classInfo = it->second;

                        // Add Constructor
                        CompletionItem ctorItem;
                        ctorItem.label = "Constructor";
                        ctorItem.kind = CompletionItemKind::Constructor;
                        ctorItem.detail = "Create new " + qualifiedName;
                        ctorItem.insertText = "Constructor()";
                        completions.push_back(ctorItem);

                        // Add static methods
                        for (const auto& [methodName, methodInfo] : classInfo.methods) {
                            CompletionItem item;
                            item.label = methodName;
                            item.kind = CompletionItemKind::Method;
                            item.detail = "→ " + methodInfo.returnType;
                            item.insertText = methodName + "()";
                            completions.push_back(item);
                        }
                    }
                } else {
                    // It might be a namespace - show ONLY classes/namespaces in that namespace
                    std::string nsPrefix = qualifiedName + "::";
                    std::set<std::string> addedNames;
                    for (const auto& [name, info] : classRegistry) {
                        if (name.find(nsPrefix) == 0) {
                            // Extract class name after the namespace prefix
                            std::string remainder = name.substr(nsPrefix.size());
                            size_t nextColon = remainder.find("::");
                            std::string nextPart = (nextColon != std::string::npos) ?
                                                    remainder.substr(0, nextColon) : remainder;

                            // Deduplicate
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
                }
            }

            // Add Constructor as fallback only if it's likely a class
            if (completions.empty() && !qualifiedName.empty() && std::isupper(qualifiedName[0])) {
                CompletionItem ctorItem;
                ctorItem.label = "Constructor";
                ctorItem.kind = CompletionItemKind::Constructor;
                ctorItem.detail = "Class constructor";
                ctorItem.insertText = "Constructor()";
                completions.push_back(ctorItem);
            }
        }
        return completions;
    }

    // After "." - show instance members
    if (afterDot) {
        // Find variable name before the dot
        size_t dotPos = precedingText.size() - 1;
        size_t nameEnd = dotPos;
        size_t nameStart = nameEnd;
        while (nameStart > 0 && (std::isalnum(precedingText[nameStart - 1]) ||
                                  precedingText[nameStart - 1] == '_')) {
            --nameStart;
        }

        std::string varName = precedingText.substr(nameStart, nameEnd - nameStart);

        // Try to find variable type
        if (result && result->context) {
            auto& symbolTable = result->context->symbolTable();
            auto* symbol = symbolTable.resolve(varName);

            if (symbol && !symbol->typeName.empty()) {
                std::string typeName = symbol->typeName;
                // Remove ownership modifier
                if (!typeName.empty() && (typeName.back() == '^' || typeName.back() == '&' || typeName.back() == '%')) {
                    typeName.pop_back();
                }
                // Remove generic parameters for lookup
                size_t genericStart = typeName.find('<');
                if (genericStart != std::string::npos) {
                    typeName = typeName.substr(0, genericStart);
                }

                // Look up methods for this type
                if (result->analyzer) {
                    const auto& classRegistry = result->analyzer->getClassRegistry();
                    for (const auto& [name, classInfo] : classRegistry) {
                        size_t lastColon = name.rfind("::");
                        std::string shortName = (lastColon != std::string::npos) ? name.substr(lastColon + 2) : name;
                        if (shortName == typeName || name == typeName) {
                            for (const auto& [methodName, methodInfo] : classInfo.methods) {
                                // Skip Constructor for instance calls
                                if (methodName == "Constructor") continue;
                                CompletionItem item;
                                item.label = methodName;
                                item.kind = CompletionItemKind::Method;
                                item.detail = "→ " + methodInfo.returnType;
                                item.insertText = methodName + "()";
                                completions.push_back(item);
                            }
                            break;
                        }
                    }
                }
            }
        }

        // Add common methods as fallback
        if (completions.empty()) {
            std::vector<std::pair<std::string, std::string>> commonMethods = {
                {"toString", "→ String^"},
                {"equals", "→ Bool^"},
                {"hash", "→ Integer^"},
                {"dispose", "→ Void"},
            };
            for (const auto& [name, detail] : commonMethods) {
                CompletionItem item;
                item.label = name;
                item.kind = CompletionItemKind::Method;
                item.detail = detail;
                item.insertText = name + "()";
                completions.push_back(item);
            }
        }
        return completions;
    }

    // Default: show keywords and available symbols
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

    // Add types
    addAllTypes();

    // Add local variables
    addLocalVariables();

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

} // namespace xxml::lsp
