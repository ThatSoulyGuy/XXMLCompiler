#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../Parser/AST.h"
#include "../Semantic/DeriveHandler.h"

// Platform-specific DLL types
#ifdef _WIN32
#include <windows.h>
typedef HMODULE DLLHandle;
#else
typedef void* DLLHandle;
#endif

namespace XXML {
namespace Derive {

/**
 * Wrapper for an in-language derive loaded from a DLL
 *
 * This adapts the C ABI of the DLL to the DeriveHandler interface
 */
class InLanguageDeriveHandler : public Semantic::DeriveHandler {
public:
    InLanguageDeriveHandler(const std::string& name, DLLHandle handle);
    ~InLanguageDeriveHandler() override;

    std::string getDeriveName() const override { return deriveName_; }

    /**
     * Generate methods for a class by calling the DLL's generate function
     * Returns string templates that need to be parsed into AST
     */
    Semantic::DeriveResult generate(
        Parser::ClassDecl* classDecl,
        Semantic::SemanticAnalyzer& analyzer) override;

    /**
     * Validate if derive can be applied by calling the DLL's canDerive function
     */
    std::string canDerive(
        Parser::ClassDecl* classDecl,
        Semantic::SemanticAnalyzer& analyzer) override;

private:
    std::string deriveName_;
    DLLHandle dllHandle_;

    // Function pointers loaded from DLL
    using CanDeriveFn = const char* (*)(void*);  // DeriveContext* -> error or null
    using GenerateFn = void (*)(void*);          // DeriveContext* -> void (modifies context)
    using CodeGenCreateFn = void* (*)(void);     // DeriveCodeGen_create
    using CodeGenDestroyFn = void (*)(void*);    // DeriveCodeGen_destroy

    // DeriveResult structure for passing generated code - must match xxml_derive_api.h
    struct DeriveResultC {
        int methodCount;
        void* methods;
        int propertyCount;
        void* properties;
        int hasErrors;
    };
    using CodeGenGetResultFn = DeriveResultC (*)(void*);  // DeriveCodeGen_getResult

    CanDeriveFn canDeriveFn_ = nullptr;
    GenerateFn generateFn_ = nullptr;
    CodeGenCreateFn codeGenCreateFn_ = nullptr;
    CodeGenDestroyFn codeGenDestroyFn_ = nullptr;
    CodeGenGetResultFn codeGenGetResultFn_ = nullptr;

    /**
     * Parse a method body string into AST statements
     */
    std::vector<std::unique_ptr<Parser::Statement>> parseMethodBody(
        const std::string& body,
        Semantic::SemanticAnalyzer& analyzer);
};

/**
 * Registry for in-language derives loaded from DLLs
 *
 * This manages loaded derive DLLs and provides lookup by name.
 * It integrates with the existing DeriveRegistry system.
 */
class InLanguageDeriveRegistry {
public:
    InLanguageDeriveRegistry() = default;
    ~InLanguageDeriveRegistry();

    /**
     * Load a derive from a DLL file
     * @param dllPath Path to the compiled derive DLL
     * @return True if loaded successfully
     */
    bool loadDerive(const std::string& dllPath);

    /**
     * Load all derives from a directory
     * @param directoryPath Path to directory containing derive DLLs
     * @return Number of derives loaded
     */
    int loadFromDirectory(const std::string& directoryPath);

    /**
     * Get a derive handler by name
     * @return The handler, or nullptr if not found
     */
    InLanguageDeriveHandler* getDerive(const std::string& name);

    /**
     * Check if a derive is registered
     */
    bool hasDerive(const std::string& name) const;

    /**
     * Get list of all registered derive names
     */
    std::vector<std::string> getRegisteredDerives() const;

    /**
     * Unload all derives and free DLL handles
     */
    void clear();

private:
    std::unordered_map<std::string, std::unique_ptr<InLanguageDeriveHandler>> derives_;
    std::vector<DLLHandle> loadedDLLs_;  // Track for cleanup
};

} // namespace Derive
} // namespace XXML
