#include "Derive/InLanguageDeriveRegistry.h"
#include "Derive/ASTSerializer.h"
#include "Semantic/SemanticAnalyzer.h"
#include "Lexer/Lexer.h"
#include "Parser/Parser.h"
#include <iostream>
#include <filesystem>
#include <cctype>
#include <algorithm>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace XXML {
namespace Derive {

// =============================================================================
// Type System Callback Functions (C ABI)
// These are passed to derives via the DeriveTypeSystem struct
// =============================================================================

// Helper struct to match runtime's DeriveTypeSystem layout
struct TypeSystemCallbackData {
    Semantic::SemanticAnalyzer* analyzer;
};

static const Semantic::ClassInfo* findClassInRegistry(Semantic::SemanticAnalyzer* analyzer, const std::string& typeName) {
    if (!analyzer) return nullptr;
    const auto& registry = analyzer->getClassRegistry();

    // Try exact match first
    auto it = registry.find(typeName);
    if (it != registry.end()) return &it->second;

    // Try with Language::Core:: prefix
    it = registry.find("Language::Core::" + typeName);
    if (it != registry.end()) return &it->second;

    return nullptr;
}

static int typeSystemHasMethod(void* compilerState, const char* typeName, const char* methodName) {
    if (!compilerState || !typeName || !methodName) return 0;
    auto* data = static_cast<TypeSystemCallbackData*>(compilerState);
    if (!data->analyzer) return 0;

    // Use the analyzer's class registry to check if the type has the method
    const Semantic::ClassInfo* classInfo = findClassInRegistry(data->analyzer, typeName);
    if (!classInfo) return 0;

    // Check if method exists
    return classInfo->methods.count(methodName) > 0 ? 1 : 0;
}

static int typeSystemHasProperty(void* compilerState, const char* typeName, const char* propertyName) {
    if (!compilerState || !typeName || !propertyName) return 0;
    auto* data = static_cast<TypeSystemCallbackData*>(compilerState);
    if (!data->analyzer) return 0;

    const Semantic::ClassInfo* classInfo = findClassInRegistry(data->analyzer, typeName);
    if (!classInfo) return 0;

    return classInfo->properties.count(propertyName) > 0 ? 1 : 0;
}

static int typeSystemImplementsTrait(void* compilerState, const char* typeName, const char* traitName) {
    if (!compilerState || !typeName || !traitName) return 0;
    auto* data = static_cast<TypeSystemCallbackData*>(compilerState);
    if (!data->analyzer) return 0;

    const Semantic::ClassInfo* classInfo = findClassInRegistry(data->analyzer, typeName);
    if (!classInfo) return 0;

    // Check if the AST node has @Derive annotations for the trait
    if (classInfo->astNode) {
        for (const auto& annotation : classInfo->astNode->annotations) {
            if (annotation && annotation->annotationName == "Derive") {
                // Check if this derive annotation has the requested trait
                for (const auto& arg : annotation->arguments) {
                    // arg.first is the parameter name (string)
                    if (arg.first == traitName) {
                        return 1;
                    }
                    // arg.second is an Expression - check if it's an identifier matching the trait
                    if (arg.second) {
                        if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(arg.second.get())) {
                            if (ident->name == traitName) {
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static int typeSystemIsBuiltin(void* compilerState, const char* typeName) {
    if (!typeName) return 0;
    // Check against known builtin types
    std::string name(typeName);
    if (name == "Integer" || name == "Float" || name == "Double" ||
        name == "Bool" || name == "Boolean" || name == "String" ||
        name == "Char" || name == "Byte" || name == "Short" ||
        name == "Long" || name == "Void" || name == "None") {
        return 1;
    }
    return 0;
}

// Type system structure matching runtime's DeriveTypeSystem layout
struct DeriveTypeSystem {
    int (*hasMethod)(void* compilerState, const char* typeName, const char* methodName);
    int (*hasProperty)(void* compilerState, const char* typeName, const char* propertyName);
    int (*implementsTrait)(void* compilerState, const char* typeName, const char* traitName);
    int (*isBuiltin)(void* compilerState, const char* typeName);
    void* compilerState;
};

// Diagnostics structure matching runtime's DeriveDiagnostics layout
struct DeriveDiagnostics {
    int* errorFlag;
    const char* deriveName;
    const char* targetClass;
    const char* sourceFile;
    int lineNumber;
    int columnNumber;
};

// =============================================================================
// Parameter String Parsing Helper
// Parses strings like "name: String^, age: Integer^" into ParameterDecl objects
// =============================================================================

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static std::vector<std::unique_ptr<Parser::ParameterDecl>> parseParameterString(const std::string& paramStr) {
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    if (paramStr.empty()) return params;

    // Split on commas, respecting nested template brackets
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : paramStr) {
        if (c == '<') {
            depth++;
            current += c;
        } else if (c == '>') {
            depth--;
            current += c;
        } else if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    // Parse each "name: Type^" or "name: Type" part
    for (const auto& part : parts) {
        std::string trimmedPart = trim(part);
        if (trimmedPart.empty()) continue;

        auto colonPos = trimmedPart.find(':');
        if (colonPos == std::string::npos) continue;

        std::string name = trim(trimmedPart.substr(0, colonPos));
        std::string typeStr = trim(trimmedPart.substr(colonPos + 1));
        if (name.empty() || typeStr.empty()) continue;

        // Extract ownership from type string
        Parser::OwnershipType ownership = Parser::OwnershipType::None;
        if (!typeStr.empty()) {
            char last = typeStr.back();
            if (last == '^') {
                ownership = Parser::OwnershipType::Owned;
                typeStr = typeStr.substr(0, typeStr.length() - 1);
            } else if (last == '&') {
                ownership = Parser::OwnershipType::Reference;
                typeStr = typeStr.substr(0, typeStr.length() - 1);
            } else if (last == '%') {
                ownership = Parser::OwnershipType::Copy;
                typeStr = typeStr.substr(0, typeStr.length() - 1);
            }
        }

        // Create type reference
        auto typeRef = std::make_unique<Parser::TypeRef>(
            typeStr,
            ownership,
            Common::SourceLocation()
        );

        // Create parameter declaration
        auto param = std::make_unique<Parser::ParameterDecl>(
            name,
            std::move(typeRef),
            Common::SourceLocation()
        );

        params.push_back(std::move(param));
    }
    return params;
}

// Forward declarations for DeriveContext C API structs
extern "C" {
    // These must match the structures in runtime/xxml_derive_api.h exactly!
    struct DeriveGeneratedMethod {
        const char* name;
        const char* returnType;
        const char* parameters;
        const char* body;
        int isStatic;  // 1 if static method, 0 otherwise
        int isAST;     // 1 if body is JSON AST, 0 if XXML source code
    };

    struct DeriveGeneratedProperty {
        const char* name;
        const char* type;
        const char* ownership;
        const char* defaultValue;
    };

    struct DeriveResult {
        int methodCount;
        DeriveGeneratedMethod* methods;
        int propertyCount;
        DeriveGeneratedProperty* properties;
        int hasErrors;
    };

    // Must match DeriveClassInfo in xxml_derive_api.c EXACTLY
    struct DeriveClassInfo {
        /* Property information */
        int propertyCount;
        const char** propertyNames;
        const char** propertyTypes;
        const char** propertyOwnerships;

        /* Method information */
        int methodCount;
        const char** methodNames;
        const char** methodReturnTypes;

        /* Class information */
        const char* baseClassName;
        int isFinal;
    };

    // DeriveContext structure - must match xxml_derive_api.h
    struct DeriveContext {
        const char* className;
        const char* namespaceName;
        const char* sourceFile;
        int lineNumber;
        int columnNumber;
        void* _classInfo;
        void* _typeSystem;
        void* _codeGen;
        void* _diagnostics;
    };

}

// ============================================================================
// InLanguageDeriveHandler Implementation
// ============================================================================

InLanguageDeriveHandler::InLanguageDeriveHandler(const std::string& name, DLLHandle handle)
    : deriveName_(name), dllHandle_(handle) {

    // Load function pointers from DLL
#ifdef _WIN32
    canDeriveFn_ = (CanDeriveFn)GetProcAddress(handle, "__xxml_derive_canDerive");
    generateFn_ = (GenerateFn)GetProcAddress(handle, "__xxml_derive_generate");
    codeGenCreateFn_ = (CodeGenCreateFn)GetProcAddress(handle, "DeriveCodeGen_create");
    codeGenDestroyFn_ = (CodeGenDestroyFn)GetProcAddress(handle, "DeriveCodeGen_destroy");
    codeGenGetResultFn_ = (CodeGenGetResultFn)GetProcAddress(handle, "DeriveCodeGen_getResult");
#else
    canDeriveFn_ = (CanDeriveFn)dlsym(handle, "__xxml_derive_canDerive");
    generateFn_ = (GenerateFn)dlsym(handle, "__xxml_derive_generate");
    codeGenCreateFn_ = (CodeGenCreateFn)dlsym(handle, "DeriveCodeGen_create");
    codeGenDestroyFn_ = (CodeGenDestroyFn)dlsym(handle, "DeriveCodeGen_destroy");
    codeGenGetResultFn_ = (CodeGenGetResultFn)dlsym(handle, "DeriveCodeGen_getResult");
#endif

    if (!generateFn_) {
        std::cerr << "Warning: Derive '" << name << "' has no generate function\n";
    }
    if (!codeGenCreateFn_ || !codeGenDestroyFn_ || !codeGenGetResultFn_) {
        std::cerr << "Warning: Derive '" << name << "' missing DeriveCodeGen functions\n";
    }
}

InLanguageDeriveHandler::~InLanguageDeriveHandler() {
    // DLL handle is managed by InLanguageDeriveRegistry
}

std::string InLanguageDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    Semantic::SemanticAnalyzer& analyzer) {

    if (!canDeriveFn_) {
        return "";  // No validation function = always valid
    }

    // Build DeriveClassInfo from class (must match struct in xxml_derive_api.c)
    DeriveClassInfo classInfo = {};

    // Gather properties
    std::vector<const char*> propNames, propTypes, propOwnerships;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                propNames.push_back(prop->name.c_str());
                propTypes.push_back(prop->type ? prop->type->typeName.c_str() : "");

                const char* ownership = "";
                if (prop->type) {
                    switch (prop->type->ownership) {
                        case Parser::OwnershipType::Owned: ownership = "^"; break;
                        case Parser::OwnershipType::Reference: ownership = "&"; break;
                        case Parser::OwnershipType::Copy: ownership = "%"; break;
                        default: break;
                    }
                }
                propOwnerships.push_back(ownership);
            }
        }
    }
    classInfo.propertyCount = static_cast<int>(propNames.size());
    classInfo.propertyNames = propNames.empty() ? nullptr : propNames.data();
    classInfo.propertyTypes = propTypes.empty() ? nullptr : propTypes.data();
    classInfo.propertyOwnerships = propOwnerships.empty() ? nullptr : propOwnerships.data();

    // Gather methods
    std::vector<const char*> methodNames, methodReturnTypes;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                methodNames.push_back(method->name.c_str());
                methodReturnTypes.push_back(method->returnType ? method->returnType->typeName.c_str() : "");
            }
        }
    }
    classInfo.methodCount = static_cast<int>(methodNames.size());
    classInfo.methodNames = methodNames.empty() ? nullptr : methodNames.data();
    classInfo.methodReturnTypes = methodReturnTypes.empty() ? nullptr : methodReturnTypes.data();

    // Class info
    classInfo.baseClassName = classDecl->baseClass.empty() ? nullptr : classDecl->baseClass.c_str();
    classInfo.isFinal = classDecl->isFinal ? 1 : 0;

    // Create context with proper structure
    DeriveContext ctx = {};
    ctx.className = classDecl->name.c_str();
    ctx.namespaceName = "";
    ctx.sourceFile = "";
    ctx.lineNumber = 0;
    ctx.columnNumber = 0;
    ctx._classInfo = &classInfo;
    ctx._typeSystem = nullptr;
    ctx._codeGen = nullptr;  // Not needed for canDerive
    ctx._diagnostics = nullptr;

    // Call canDerive function
    const char* error = canDeriveFn_(&ctx);
    return error ? std::string(error) : "";
}

Semantic::DeriveResult InLanguageDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    Semantic::SemanticAnalyzer& analyzer) {

    Semantic::DeriveResult result;

    if (!generateFn_) {
        result.errors.push_back("Derive '" + deriveName_ + "' has no generate function");
        return result;
    }

    // Build DeriveClassInfo from class (must match struct in xxml_derive_api.c)
    DeriveClassInfo classInfo = {};

    // Gather properties
    std::vector<const char*> propNames, propTypes, propOwnerships;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                propNames.push_back(prop->name.c_str());
                propTypes.push_back(prop->type ? prop->type->typeName.c_str() : "");

                const char* ownership = "";
                if (prop->type) {
                    switch (prop->type->ownership) {
                        case Parser::OwnershipType::Owned: ownership = "^"; break;
                        case Parser::OwnershipType::Reference: ownership = "&"; break;
                        case Parser::OwnershipType::Copy: ownership = "%"; break;
                        default: break;
                    }
                }
                propOwnerships.push_back(ownership);
            }
        }
    }
    classInfo.propertyCount = static_cast<int>(propNames.size());
    classInfo.propertyNames = propNames.empty() ? nullptr : propNames.data();
    classInfo.propertyTypes = propTypes.empty() ? nullptr : propTypes.data();
    classInfo.propertyOwnerships = propOwnerships.empty() ? nullptr : propOwnerships.data();

    // Gather methods
    std::vector<const char*> methodNames, methodReturnTypes;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                methodNames.push_back(method->name.c_str());
                methodReturnTypes.push_back(method->returnType ? method->returnType->typeName.c_str() : "");
            }
        }
    }
    classInfo.methodCount = static_cast<int>(methodNames.size());
    classInfo.methodNames = methodNames.empty() ? nullptr : methodNames.data();
    classInfo.methodReturnTypes = methodReturnTypes.empty() ? nullptr : methodReturnTypes.data();

    // Class info
    classInfo.baseClassName = classDecl->baseClass.empty() ? nullptr : classDecl->baseClass.c_str();
    classInfo.isFinal = classDecl->isFinal ? 1 : 0;

    // Create code generator state using dynamically loaded function
    if (!codeGenCreateFn_) {
        result.errors.push_back("DeriveCodeGen_create not available");
        return result;
    }
    void* codeGen = codeGenCreateFn_();
    if (!codeGen) {
        result.errors.push_back("Failed to create derive code generator");
        return result;
    }

    // Set up type system callbacks
    TypeSystemCallbackData typeSystemData;
    typeSystemData.analyzer = &analyzer;

    DeriveTypeSystem typeSystem;
    typeSystem.hasMethod = typeSystemHasMethod;
    typeSystem.hasProperty = typeSystemHasProperty;
    typeSystem.implementsTrait = typeSystemImplementsTrait;
    typeSystem.isBuiltin = typeSystemIsBuiltin;
    typeSystem.compilerState = &typeSystemData;

    // Set up diagnostics
    int errorFlag = 0;
    DeriveDiagnostics diagnostics;
    diagnostics.errorFlag = &errorFlag;
    diagnostics.deriveName = deriveName_.c_str();
    diagnostics.targetClass = classDecl->name.c_str();
    diagnostics.sourceFile = classDecl->location.filename.c_str();
    diagnostics.lineNumber = static_cast<int>(classDecl->location.line);
    diagnostics.columnNumber = static_cast<int>(classDecl->location.column);

    // Create context with proper structure
    DeriveContext ctx = {};
    ctx.className = classDecl->name.c_str();
    ctx.namespaceName = "";
    ctx.sourceFile = classDecl->location.filename.c_str();
    ctx.lineNumber = static_cast<int>(classDecl->location.line);
    ctx.columnNumber = static_cast<int>(classDecl->location.column);
    ctx._classInfo = &classInfo;
    ctx._typeSystem = &typeSystem;
    ctx._codeGen = codeGen;
    ctx._diagnostics = &diagnostics;

    // Call generate function
    generateFn_(&ctx);

    // Check if derive reported errors via diagnostics
    if (errorFlag) {
        result.errors.push_back("Derive '" + deriveName_ + "' reported an error");
    }

    // Get result from code generator using dynamically loaded function
    if (!codeGenGetResultFn_) {
        if (codeGenDestroyFn_) codeGenDestroyFn_(codeGen);
        result.errors.push_back("DeriveCodeGen_getResult not available");
        return result;
    }
    DeriveResultC deriveResultC;
    codeGenGetResultFn_(codeGen, &deriveResultC);

    // Check for errors
    if (deriveResultC.hasErrors) {
        result.errors.push_back("Derive '" + deriveName_ + "' reported an error");
        if (codeGenDestroyFn_) codeGenDestroyFn_(codeGen);
        return result;
    }

    // Collect property names for this. qualification in generated code
    std::unordered_set<std::string> propertyNameSet;
    for (const char* name : propNames) {
        if (name) propertyNameSet.insert(std::string(name));
    }

    // Parse generated methods
    DeriveGeneratedMethod* methods = (DeriveGeneratedMethod*)deriveResultC.methods;
    for (int i = 0; i < deriveResultC.methodCount; i++) {
        const auto& genMethod = methods[i];

        std::vector<std::unique_ptr<Parser::Statement>> body;

        // Check if body is AST JSON or XXML source code
        if (genMethod.isAST) {
            // Deserialize AST from JSON
            try {
                body = ASTSerializer::deserializeStatements(genMethod.body ? genMethod.body : "");
            } catch (const std::exception& e) {
                result.errors.push_back("Failed to deserialize AST for method '" +
                                       std::string(genMethod.name) + "': " + e.what());
                continue;
            }
        } else {
            // Parse XXML source code into AST (with property name qualification)
            body = parseMethodBody(genMethod.body, propertyNameSet, analyzer);
            if (body.empty() && genMethod.body && strlen(genMethod.body) > 0) {
                result.errors.push_back("Failed to parse generated method body for '" +
                                       std::string(genMethod.name) + "'");
                continue;
            }
        }

        // Parse ownership from return type string (e.g., "String^" -> "String" with Owned)
        std::string returnTypeStr = genMethod.returnType ? genMethod.returnType : "";
        Parser::OwnershipType returnOwnership = Parser::OwnershipType::None;

        if (!returnTypeStr.empty()) {
            char lastChar = returnTypeStr.back();
            if (lastChar == '^') {
                returnOwnership = Parser::OwnershipType::Owned;
                returnTypeStr = returnTypeStr.substr(0, returnTypeStr.length() - 1);
            } else if (lastChar == '&') {
                returnOwnership = Parser::OwnershipType::Reference;
                returnTypeStr = returnTypeStr.substr(0, returnTypeStr.length() - 1);
            } else if (lastChar == '%') {
                returnOwnership = Parser::OwnershipType::Copy;
                returnTypeStr = returnTypeStr.substr(0, returnTypeStr.length() - 1);
            }
        }

        // Create type reference for return type
        auto returnType = std::make_unique<Parser::TypeRef>(
            returnTypeStr,
            returnOwnership,
            Common::SourceLocation()
        );

        // Parse parameters from parameter string (e.g., "name: String^, age: Integer^")
        std::string parameterStr = genMethod.parameters ? genMethod.parameters : "";
        std::vector<std::unique_ptr<Parser::ParameterDecl>> params = parseParameterString(parameterStr);

        // Create method declaration
        auto method = std::make_unique<Parser::MethodDecl>(
            genMethod.name,
            std::move(returnType),
            std::move(params),
            std::move(body),
            Common::SourceLocation()
        );

        result.methods.push_back(std::move(method));
    }

    // Parse generated properties
    DeriveGeneratedProperty* properties = (DeriveGeneratedProperty*)deriveResultC.properties;
    for (int i = 0; i < deriveResultC.propertyCount; i++) {
        const auto& genProp = properties[i];

        Parser::OwnershipType ownership = Parser::OwnershipType::Owned;
        if (genProp.ownership) {
            if (strcmp(genProp.ownership, "&") == 0) ownership = Parser::OwnershipType::Reference;
            else if (strcmp(genProp.ownership, "%") == 0) ownership = Parser::OwnershipType::Copy;
        }

        auto propType = std::make_unique<Parser::TypeRef>(
            genProp.type,
            ownership,
            Common::SourceLocation()
        );

        auto prop = std::make_unique<Parser::PropertyDecl>(
            genProp.name,
            std::move(propType),
            Common::SourceLocation()
        );

        result.properties.push_back(std::move(prop));
    }

    // Cleanup using dynamically loaded function
    if (codeGenDestroyFn_) {
        codeGenDestroyFn_(codeGen);
    }

    return result;
}

std::vector<std::unique_ptr<Parser::Statement>> InLanguageDeriveHandler::parseMethodBody(
    const std::string& body,
    const std::unordered_set<std::string>& propertyNames,
    Semantic::SemanticAnalyzer& analyzer) {

    std::vector<std::unique_ptr<Parser::Statement>> statements;

    if (body.empty()) {
        return statements;
    }

    // Preprocess body to add this. before property names
    // This handles cases like "x.toString()" -> "this.x.toString()"
    std::string processedBody = body;

    for (const std::string& propName : propertyNames) {
        // Replace "propName." with "this.propName." where not already qualified
        std::string search = propName + ".";
        std::string replace = "this." + propName + ".";

        size_t pos = 0;
        while ((pos = processedBody.find(search, pos)) != std::string::npos) {
            // Check if already qualified with this. (look back 5 chars for "this.")
            if (pos >= 5 && processedBody.substr(pos - 5, 5) == "this.") {
                pos += search.length();
                continue;
            }
            // Check if preceded by letter/digit/underscore (part of another identifier)
            if (pos > 0) {
                char prev = processedBody[pos - 1];
                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
                    pos += search.length();
                    continue;
                }
            }
            processedBody.replace(pos, search.length(), replace);
            pos += replace.length();
        }
    }

    // Wrap the body in a minimal parseable structure
    std::string source = "[ Class <__DeriveTemp__> Final Extends None\n"
                         "    [ Public <>\n"
                         "        Method <__temp__> Returns None Parameters () Do {\n"
                         + processedBody + "\n"
                         "        }\n"
                         "    ]\n"
                         "]\n";

    // Lex and parse
    Common::ErrorReporter errorReporter;
    Lexer::Lexer lexer(source, "__derive_generated__", errorReporter);
    auto tokens = lexer.tokenize();

    if (errorReporter.hasErrors()) {
        std::cerr << "Lexer error parsing derive body\n";
        return statements;
    }

    Parser::Parser parser(tokens, errorReporter);
    auto program = parser.parse();

    if (errorReporter.hasErrors() || !program) {
        std::cerr << "Parser error parsing derive body\n";
        return statements;
    }

    // Extract the method body from the parsed program
    for (auto& decl : program->declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            for (auto& section : classDecl->sections) {
                for (auto& memberDecl : section->declarations) {
                    if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                        // Move the body out
                        for (auto& stmt : method->body) {
                            statements.push_back(std::move(stmt));
                        }
                        return statements;
                    }
                }
            }
        }
    }

    return statements;
}

// ============================================================================
// InLanguageDeriveRegistry Implementation
// ============================================================================

InLanguageDeriveRegistry::~InLanguageDeriveRegistry() {
    clear();
}

void InLanguageDeriveRegistry::clear() {
    derives_.clear();

    for (auto handle : loadedDLLs_) {
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
    loadedDLLs_.clear();
}

bool InLanguageDeriveRegistry::loadDerive(const std::string& dllPath) {
    // Load the DLL
#ifdef _WIN32
    DLLHandle handle = LoadLibraryA(dllPath.c_str());
#else
    DLLHandle handle = dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif

    if (!handle) {
#ifdef _WIN32
        std::cerr << "Failed to load derive DLL: " << dllPath << " (error: " << GetLastError() << ")\n";
#else
        std::cerr << "Failed to load derive DLL: " << dllPath << " (" << dlerror() << ")\n";
#endif
        return false;
    }

    loadedDLLs_.push_back(handle);

    // Get the derive name
    using NameFn = const char* (*)();
#ifdef _WIN32
    auto nameFn = (NameFn)GetProcAddress(handle, "__xxml_derive_name");
#else
    auto nameFn = (NameFn)dlsym(handle, "__xxml_derive_name");
#endif

    if (!nameFn) {
        std::cerr << "Derive DLL missing __xxml_derive_name: " << dllPath << "\n";
        return false;
    }

    std::string deriveName = nameFn();

    if (derives_.find(deriveName) != derives_.end()) {
        std::cerr << "Derive '" << deriveName << "' already registered\n";
        return false;
    }

    // Create handler wrapper
    auto handler = std::make_unique<InLanguageDeriveHandler>(deriveName, handle);

    derives_[deriveName] = std::move(handler);

    std::cout << "    Loaded in-language derive: " << deriveName << "\n";
    return true;
}

int InLanguageDeriveRegistry::loadFromDirectory(const std::string& directoryPath) {
    int count = 0;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            if (!entry.is_regular_file()) continue;

            auto ext = entry.path().extension().string();
#ifdef _WIN32
            if (ext != ".dll") continue;
#elif __APPLE__
            if (ext != ".dylib") continue;
#else
            if (ext != ".so") continue;
#endif

            if (loadDerive(entry.path().string())) {
                count++;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning derive directory: " << e.what() << "\n";
    }

    return count;
}

InLanguageDeriveHandler* InLanguageDeriveRegistry::getDerive(const std::string& name) {
    auto it = derives_.find(name);
    return it != derives_.end() ? it->second.get() : nullptr;
}

bool InLanguageDeriveRegistry::hasDerive(const std::string& name) const {
    return derives_.find(name) != derives_.end();
}

std::vector<std::string> InLanguageDeriveRegistry::getRegisteredDerives() const {
    std::vector<std::string> names;
    names.reserve(derives_.size());
    for (const auto& [name, _] : derives_) {
        names.push_back(name);
    }
    return names;
}

} // namespace Derive
} // namespace XXML
