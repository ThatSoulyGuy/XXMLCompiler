#include "Derive/InLanguageDeriveRegistry.h"
#include "Semantic/SemanticAnalyzer.h"
#include "Lexer/Lexer.h"
#include "Parser/Parser.h"
#include <iostream>
#include <filesystem>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace XXML {
namespace Derive {

// Forward declarations for DeriveContext C API structs
extern "C" {
    // These must match the structures in runtime/xxml_derive_api.h exactly!
    struct DeriveGeneratedMethod {
        const char* name;
        const char* returnType;
        const char* parameters;
        const char* body;
        int isStatic;  // 1 if static method, 0 otherwise
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

    struct DeriveClassInfo {
        const char* className;
        const char* namespaceName;
        const char** propertyNames;
        const char** propertyTypes;
        const char** propertyOwnerships;
        int propertyCount;
        const char** methodNames;
        int methodCount;
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

    // Build DeriveContext from class
    DeriveClassInfo classInfo = {};
    classInfo.className = classDecl->name.c_str();
    // Note: namespaceName would need to be tracked elsewhere
    classInfo.namespaceName = "";

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
    classInfo.propertyNames = propNames.empty() ? nullptr : propNames.data();
    classInfo.propertyTypes = propTypes.empty() ? nullptr : propTypes.data();
    classInfo.propertyOwnerships = propOwnerships.empty() ? nullptr : propOwnerships.data();
    classInfo.propertyCount = static_cast<int>(propNames.size());

    // Gather methods
    std::vector<const char*> methodNames;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                methodNames.push_back(method->name.c_str());
            }
        }
    }
    classInfo.methodNames = methodNames.empty() ? nullptr : methodNames.data();
    classInfo.methodCount = static_cast<int>(methodNames.size());

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

    // Build DeriveContext from class (similar to canDerive)
    DeriveClassInfo classInfo = {};
    classInfo.className = classDecl->name.c_str();
    classInfo.namespaceName = "";

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
    classInfo.propertyNames = propNames.empty() ? nullptr : propNames.data();
    classInfo.propertyTypes = propTypes.empty() ? nullptr : propTypes.data();
    classInfo.propertyOwnerships = propOwnerships.empty() ? nullptr : propOwnerships.data();
    classInfo.propertyCount = static_cast<int>(propNames.size());

    // Gather methods
    std::vector<const char*> methodNames;
    for (const auto& section : classDecl->sections) {
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                methodNames.push_back(method->name.c_str());
            }
        }
    }
    classInfo.methodNames = methodNames.empty() ? nullptr : methodNames.data();
    classInfo.methodCount = static_cast<int>(methodNames.size());

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

    // Create context with proper structure
    DeriveContext ctx = {};
    ctx.className = classDecl->name.c_str();
    ctx.namespaceName = "";
    ctx.sourceFile = "";
    ctx.lineNumber = 0;
    ctx.columnNumber = 0;
    ctx._classInfo = &classInfo;
    ctx._typeSystem = nullptr;  // TODO: implement type system queries
    ctx._codeGen = codeGen;
    ctx._diagnostics = nullptr;  // TODO: implement diagnostics

    // Call generate function
    generateFn_(&ctx);

    // Get result from code generator using dynamically loaded function
    if (!codeGenGetResultFn_) {
        if (codeGenDestroyFn_) codeGenDestroyFn_(codeGen);
        result.errors.push_back("DeriveCodeGen_getResult not available");
        return result;
    }
    auto deriveResultC = codeGenGetResultFn_(codeGen);

    // Check for errors
    if (deriveResultC.hasErrors) {
        result.errors.push_back("Derive '" + deriveName_ + "' reported an error");
        if (codeGenDestroyFn_) codeGenDestroyFn_(codeGen);
        return result;
    }

    // Parse generated methods
    DeriveGeneratedMethod* methods = (DeriveGeneratedMethod*)deriveResultC.methods;
    for (int i = 0; i < deriveResultC.methodCount; i++) {
        const auto& genMethod = methods[i];

        // Parse the method body into AST
        auto body = parseMethodBody(genMethod.body, analyzer);
        if (body.empty() && genMethod.body && strlen(genMethod.body) > 0) {
            result.errors.push_back("Failed to parse generated method body for '" +
                                   std::string(genMethod.name) + "'");
            continue;
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

        // Parse parameters
        std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
        // TODO: Parse parameter string like "name: String^, age: Integer^"

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
    Semantic::SemanticAnalyzer& analyzer) {

    std::vector<std::unique_ptr<Parser::Statement>> statements;

    if (body.empty()) {
        return statements;
    }

    // Wrap the body in a minimal parseable structure
    std::string source = "[ Class <__DeriveTemp__> Final Extends None\n"
                         "    [ Public <>\n"
                         "        Method <__temp__> Returns None Parameters () Do {\n"
                         + body + "\n"
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
