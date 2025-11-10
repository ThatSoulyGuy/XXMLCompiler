#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include "../include/Lexer/Lexer.h"
#include "../include/Parser/Parser.h"
#include "../include/Semantic/SemanticAnalyzer.h"
#include "../include/CodeGen/CodeGenerator.h"
#include "../include/Core/CompilationContext.h"  // ✅ NEW: Use CompilationContext
#include "../include/Core/TypeRegistry.h"        // ✅ NEW: For TypeRegistry methods
#include "../include/Core/BackendRegistry.h"     // ✅ NEW: For BackendRegistry methods
#include "../include/Backends/Cpp20Backend.h"    // ✅ NEW: Use new backend
#include "../include/Common/Error.h"
#include "../include/Import/Module.h"
#include "../include/Import/ImportResolver.h"
#include "../include/Import/DependencyGraph.h"

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeFile(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not write to file: " + filename);
    }

    file << content;
}

// Extract import paths from an AST
std::vector<std::string> extractImports(const XXML::Parser::Program& ast) {
    std::vector<std::string> imports;
    for (const auto& decl : ast.declarations) {
        if (auto importDecl = dynamic_cast<XXML::Parser::ImportDecl*>(decl.get())) {
            imports.push_back(importDecl->modulePath);
        }
    }
    return imports;
}

// Parse a module (tokenize + parse)
bool parseModule(XXML::Import::Module* module, XXML::Common::ErrorReporter& errorReporter) {
    if (module->isParsed) return true;

    // Tokenize
    XXML::Lexer::Lexer lexer(module->fileContent, module->filePath, errorReporter);
    auto tokens = lexer.tokenize();

    if (errorReporter.hasErrors()) {
        std::cerr << "  Lexical analysis failed for " << module->moduleName << "\n";
        return false;
    }

    // Parse
    XXML::Parser::Parser parser(tokens, errorReporter);
    module->ast = parser.parse();

    if (errorReporter.hasErrors()) {
        std::cerr << "  Syntax analysis failed for " << module->moduleName << "\n";
        return false;
    }

    module->isParsed = true;

    // Extract imports from this module
    module->imports = extractImports(*module->ast);

    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "XXML Compiler v2.0 (Multi-file, Multi-backend)\n";
    std::cout << "================================================\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.xxml> [output.cpp]\n";
        std::cerr << "  <input.xxml>  - XXML source file to compile\n";
        std::cerr << "  [output.cpp]  - Output C++ file (optional, defaults to output.cpp)\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = (argc >= 3) ? argv[2] : "output.cpp";

    try {
        // ✅ NEW: Create compilation context (replaces static state!)
        XXML::Core::CompilationContext compilationContext;
        std::cout << "✅ Initialized compilation context (C++20 backend)\n";
        std::cout << "   - Types registered: " << compilationContext.types().size() << "\n";
        std::cout << "   - Backends available: " << compilationContext.backends().size() << "\n\n";

        // Error reporter
        XXML::Common::ErrorReporter errorReporter;

        // Import resolver
        XXML::Import::ImportResolver resolver;

        // Create main module
        std::cout << "Reading main file: " << inputFile << "\n";
        auto mainModule = std::make_unique<XXML::Import::Module>("__main__", inputFile);
        if (!mainModule->loadFromFile()) {
            std::cerr << "Error: Could not read file: " << inputFile << "\n";
            return 1;
        }

        // Parse main module
        std::cout << "Parsing main module...\n";
        if (!parseModule(mainModule.get(), errorReporter)) {
            errorReporter.printErrors();
            return 1;
        }
        std::cout << "  Found " << mainModule->imports.size() << " import(s)\n";

        // Auto-import standard library types from Language folder
        std::vector<std::string> stdLibFiles = {
            "Language/Core/String.XXML",
            "Language/Core/Integer.XXML",
            "Language/Core/Bool.XXML",
            "Language/Core/Float.XXML",
            "Language/Core/Double.XXML",
            "Language/System/Console.XXML"
        };

        std::cout << "Auto-importing standard library...\n";
        std::vector<std::string> autoImports;
        for (const auto& stdFile : stdLibFiles) {
            // Check if file exists
            std::ifstream check(stdFile);
            if (check.good()) {
                autoImports.push_back(stdFile);
                std::cout << "  Found: " << stdFile << "\n";
            }
        }

        // Collect all modules through recursive import resolution
        std::set<std::string> processedImports;
        std::vector<XXML::Import::Module*> allModules;
        std::vector<std::unique_ptr<XXML::Import::Module>> ownedModules;  // Store owned modules
        allModules.push_back(mainModule.get());

        std::vector<std::string> toProcess = mainModule->imports;

        // Add auto-imported standard library files to processing queue
        // Also add them to mainModule's imports so they're recorded as dependencies
        for (const auto& autoImport : autoImports) {
            toProcess.push_back(autoImport);
            mainModule->imports.push_back(autoImport);
        }

        std::cout << "Resolving imports...\n";
        while (!toProcess.empty()) {
            std::string importPath = toProcess.back();
            toProcess.pop_back();

            if (processedImports.find(importPath) != processedImports.end()) {
                continue;
            }
            processedImports.insert(importPath);

            std::cout << "  Resolving: " << importPath << "\n";

            // Check if this is a direct file path (for Language folder files)
            std::vector<XXML::Import::Module*> importedModules;
            if (importPath.find(".XXML") != std::string::npos || importPath.find("/") != std::string::npos) {
                // It's a file path - load it directly
                auto module = std::make_unique<XXML::Import::Module>(importPath, importPath);
                if (!module->loadFromFile()) {
                    std::cerr << "Warning: Could not load file: " << importPath << "\n";
                    continue;
                }

                if (!parseModule(module.get(), errorReporter)) {
                    errorReporter.printErrors();
                    return 1;
                }

                XXML::Import::Module* modulePtr = module.get();
                allModules.push_back(modulePtr);
                ownedModules.push_back(std::move(module));

                // Add this module's imports to the queue
                for (const auto& subImport : modulePtr->imports) {
                    if (processedImports.find(subImport) == processedImports.end()) {
                        toProcess.push_back(subImport);
                    }
                }
            } else {
                // Use normal import resolution
                importedModules = resolver.resolveImport(importPath);

                for (auto module : importedModules) {
                    // Parse the module
                    if (!parseModule(module, errorReporter)) {
                        errorReporter.printErrors();
                        return 1;
                    }

                    allModules.push_back(module);

                    // Add this module's imports to the queue
                    for (const auto& subImport : module->imports) {
                        if (processedImports.find(subImport) == processedImports.end()) {
                            toProcess.push_back(subImport);
                        }
                    }
                }
            }
        }

        std::cout << "  Total modules loaded: " << allModules.size() << "\n";

        // Build dependency graph
        std::cout << "Building dependency graph...\n";
        XXML::Import::DependencyGraph depGraph;

        for (auto module : allModules) {
            depGraph.addModule(module->moduleName);
        }

        for (auto module : allModules) {
            for (const auto& importPath : module->imports) {
                // Find all modules that match this import
                for (auto otherModule : allModules) {
                    if (otherModule->moduleName.find(importPath) == 0 ||
                        importPath.find(otherModule->moduleName) == 0) {
                        if (otherModule != module) {
                            depGraph.addDependency(module->moduleName, otherModule->moduleName);
                        }
                    }
                }
            }
        }

        // Check for circular dependencies
        std::vector<std::string> cycle;
        if (depGraph.hasCycle(cycle)) {
            std::cerr << "Error: Circular dependency detected!\n";
            std::cerr << "  Cycle: ";
            for (size_t i = 0; i < cycle.size(); ++i) {
                if (i > 0) std::cerr << " -> ";
                std::cerr << cycle[i];
            }
            std::cerr << "\n";
            return 1;
        }

        // Get compilation order via topological sort
        auto compilationOrder = depGraph.topologicalSort();
        std::cout << "  Compilation order established (" << compilationOrder.size() << " modules)\n";

        // Create a map for quick module lookup
        std::map<std::string, XXML::Import::Module*> moduleMap;
        for (auto module : allModules) {
            moduleMap[module->moduleName] = module;
        }

        // Store analyzers for each module (needed for template code generation)
        std::map<std::string, std::unique_ptr<XXML::Semantic::SemanticAnalyzer>> analyzerMap;

        // PHASE 1: Registration phase - Register all classes and methods without validation
        std::cout << "Phase 1: Registering classes and methods...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto module = it->second;
                std::cout << "  Registering: " << moduleName << "\n";

                // ✅ NEW: Pass CompilationContext to analyzer
                auto analyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                analyzer->setValidationEnabled(false);  // Disable validation for registration phase
                analyzer->analyze(*module->ast);

                if (errorReporter.hasErrors()) {
                    std::cerr << "\nRegistration phase failed for " << moduleName << ":\n";
                    errorReporter.printErrors();
                    return 1;
                }

                analyzerMap[moduleName] = std::move(analyzer);
            }
        }

        // Register main module
        std::unique_ptr<XXML::Semantic::SemanticAnalyzer> mainAnalyzer;
        std::cout << "  Registering: __main__\n";
        // ✅ NEW: Pass CompilationContext to analyzer
        mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setValidationEnabled(false);  // Disable validation for registration phase
        mainAnalyzer->analyze(*mainModule->ast);

        if (errorReporter.hasErrors()) {
            std::cerr << "\nRegistration phase failed for main module:\n";
            errorReporter.printErrors();
            return 1;
        }

        // PHASE 2: Validation phase - Run semantic analysis with validation enabled
        std::cout << "Phase 2: Running semantic analysis with validation...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto module = it->second;
                std::cout << "  Analyzing: " << moduleName << "\n";

                // Create a new analyzer for validation
                auto validator = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                validator->setValidationEnabled(true);  // Enable validation
                validator->analyze(*module->ast);

                if (errorReporter.hasErrors()) {
                    std::cerr << "\nSemantic analysis failed for " << moduleName << ":\n";
                    errorReporter.printErrors();
                    return 1;
                }

                module->isAnalyzed = true;
                // Keep the validator as the main analyzer for code generation
                analyzerMap[moduleName] = std::move(validator);
            }
        }

        // Validate main module
        std::cout << "  Analyzing: __main__\n";
        mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setValidationEnabled(true);  // Enable validation
        mainAnalyzer->analyze(*mainModule->ast);

        if (errorReporter.hasErrors()) {
            std::cerr << "\nSemantic analysis failed for main module:\n";
            errorReporter.printErrors();
            return 1;
        }
        mainModule->isAnalyzed = true;

        std::cout << "  Semantic analysis passed\n";

        // Code generation for all modules
        std::cout << "Generating C++ code...\n";
        std::stringstream fullOutput;

        // Write standard includes once at the top
        fullOutput << "// Generated by XXML Compiler\n\n";
        fullOutput << "#include <iostream>\n";
        fullOutput << "#include <string>\n";
        fullOutput << "#include <memory>\n";
        fullOutput << "#include <cstdint>\n";
        fullOutput << "#include <cstring>\n\n";

        // Add Syscall intrinsic functions
        fullOutput << "// Syscall intrinsic functions\n";
        fullOutput << "class Syscall {\n";
        fullOutput << "public:\n";
        fullOutput << "    static void* string_create(const void* cstr) {\n";
        fullOutput << "        return (void*)new std::string((const char*)cstr);\n";
        fullOutput << "    }\n";
        fullOutput << "    static const char* string_cstr(void* ptr) {\n";
        fullOutput << "        return ((std::string*)ptr)->c_str();\n";
        fullOutput << "    }\n";
        fullOutput << "    static int64_t string_length(void* ptr) {\n";
        fullOutput << "        return ((std::string*)ptr)->length();\n";
        fullOutput << "    }\n";
        fullOutput << "    static void* string_concat(void* ptr1, void* ptr2) {\n";
        fullOutput << "        std::string* s1 = (std::string*)ptr1;\n";
        fullOutput << "        std::string* s2 = (std::string*)ptr2;\n";
        fullOutput << "        return (void*)new std::string(*s1 + *s2);\n";
        fullOutput << "    }\n";
        fullOutput << "    static void* string_copy(void* ptr) {\n";
        fullOutput << "        return (void*)new std::string(*(std::string*)ptr);\n";
        fullOutput << "    }\n";
        fullOutput << "    static int64_t string_equals(void* ptr1, void* ptr2) {\n";
        fullOutput << "        return (*(std::string*)ptr1 == *(std::string*)ptr2) ? 1 : 0;\n";
        fullOutput << "    }\n";
        fullOutput << "    static void string_destroy(void* ptr) {\n";
        fullOutput << "        delete (std::string*)ptr;\n";
        fullOutput << "    }\n";
        fullOutput << "    static void memcpy(void* dest, const void* src, size_t n) {\n";
        fullOutput << "        std::memcpy(dest, src, n);\n";
        fullOutput << "    }\n";
        fullOutput << "};\n\n";

        // Add StringArray stub (referenced by Console but not yet implemented)
        fullOutput << "// StringArray stub\n";
        fullOutput << "class StringArray {\n";
        fullOutput << "public:\n";
        fullOutput << "    StringArray() {}\n";
        fullOutput << "};\n\n";

        // Add forward declarations for all classes to resolve circular dependencies
        fullOutput << "// Forward declarations\n";
        fullOutput << "namespace Language::Core {\n";
        fullOutput << "    class Bool;\n";
        fullOutput << "    class Integer;\n";
        fullOutput << "    class Float;\n";
        fullOutput << "    class Double;\n";
        fullOutput << "    class String;\n";
        fullOutput << "}\n";
        fullOutput << "namespace System {\n";
        fullOutput << "    class Console;\n";
        fullOutput << "}\n\n";

        // Define core types that need declaration/implementation separation
        std::vector<std::string> coreValueTypes = {
            "Language/Core/Integer.XXML",
            "Language/Core/Bool.XXML",
            "Language/Core/Float.XXML",
            "Language/Core/Double.XXML",
            "Language/Core/String.XXML"
        };

        // PHASE 1: Generate class declarations for core value types (to avoid circular dependencies)
        std::set<std::string> generatedModules;
        fullOutput << "// ============================================\n";
        fullOutput << "// Core Library - Class Declarations\n";
        fullOutput << "// ============================================\n\n";

        for (const auto& moduleName : coreValueTypes) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto module = it->second;
                std::cout << "  Generating declarations: " << moduleName << "\n";

                XXML::CodeGen::CodeGenerator codeGen(errorReporter);
                codeGen.setGeneratingDeclarationsOnly(true);  // Declarations only

                // Pass semantic analyzer for template support
                auto analyzerIt = analyzerMap.find(moduleName);
                if (analyzerIt != analyzerMap.end()) {
                    codeGen.setSemanticAnalyzer(analyzerIt->second.get());
                }

                std::string moduleCode = codeGen.generate(*module->ast, false);

                if (errorReporter.hasErrors()) {
                    std::cerr << "\nCode generation failed for " << moduleName << ":\n";
                    errorReporter.printErrors();
                    return 1;
                }

                fullOutput << moduleCode;
            }
        }

        // PHASE 2: Generate method implementations for core value types
        fullOutput << "// ============================================\n";
        fullOutput << "// Core Library - Method Implementations\n";
        fullOutput << "// ============================================\n\n";

        for (const auto& moduleName : coreValueTypes) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto module = it->second;
                std::cout << "  Generating implementations: " << moduleName << "\n";

                XXML::CodeGen::CodeGenerator codeGen(errorReporter);
                codeGen.setGeneratingImplementationsOnly(true);  // Implementations only

                // Pass semantic analyzer for template support
                auto analyzerIt = analyzerMap.find(moduleName);
                if (analyzerIt != analyzerMap.end()) {
                    codeGen.setSemanticAnalyzer(analyzerIt->second.get());
                }

                std::string moduleCode = codeGen.generate(*module->ast, false);

                if (errorReporter.hasErrors()) {
                    std::cerr << "\nCode generation failed for " << moduleName << ":\n";
                    errorReporter.printErrors();
                    return 1;
                }

                fullOutput << moduleCode;
                it->second->isCompiled = true;
                generatedModules.insert(moduleName);
            }
        }

        // PHASE 3: Generate Console (with inline implementations)
        std::string consoleModule = "Language/System/Console.XXML";
        auto consoleIt = moduleMap.find(consoleModule);
        if (consoleIt != moduleMap.end()) {
            auto module = consoleIt->second;
            std::cout << "  Generating: " << consoleModule << "\n";

            XXML::CodeGen::CodeGenerator codeGen(errorReporter);

            auto analyzerIt = analyzerMap.find(consoleModule);
            if (analyzerIt != analyzerMap.end()) {
                codeGen.setSemanticAnalyzer(analyzerIt->second.get());
            }

            std::string moduleCode = codeGen.generate(*module->ast, false);

            if (errorReporter.hasErrors()) {
                std::cerr << "\nCode generation failed for " << consoleModule << ":\n";
                errorReporter.printErrors();
                return 1;
            }

            fullOutput << "// ============================================\n";
            fullOutput << "// Module: " << consoleModule << "\n";
            fullOutput << "// ============================================\n";
            fullOutput << moduleCode << "\n\n";
            module->isCompiled = true;
            generatedModules.insert(consoleModule);
        }

        // Generate remaining modules in dependency order (non-core library)
        for (const auto& moduleName : compilationOrder) {
            if (generatedModules.find(moduleName) != generatedModules.end()) {
                continue; // Already generated
            }
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto module = it->second;
                std::cout << "  Generating: " << moduleName << "\n";

                XXML::CodeGen::CodeGenerator codeGen(errorReporter);

                // Pass semantic analyzer for template support
                auto analyzerIt = analyzerMap.find(moduleName);
                if (analyzerIt != analyzerMap.end()) {
                    codeGen.setSemanticAnalyzer(analyzerIt->second.get());
                }

                std::string moduleCode = codeGen.generate(*module->ast, false); // Don't include headers

                if (errorReporter.hasErrors()) {
                    std::cerr << "\nCode generation failed for " << moduleName << ":\n";
                    errorReporter.printErrors();
                    return 1;
                }

                fullOutput << "// ============================================\n";
                fullOutput << "// Module: " << moduleName << "\n";
                fullOutput << "// ============================================\n";
                fullOutput << moduleCode << "\n\n";
                module->isCompiled = true;
            }
        }

        // Generate main module code if it wasn't already generated
        if (!mainModule->isCompiled) {
            std::cout << "  Generating: __main__\n";
            XXML::CodeGen::CodeGenerator mainCodeGen(errorReporter);

            // Pass semantic analyzer for template support
            if (mainAnalyzer) {
                mainCodeGen.setSemanticAnalyzer(mainAnalyzer.get());
            }

            std::string mainCode = mainCodeGen.generate(*mainModule->ast, false); // Don't include headers

            if (errorReporter.hasErrors()) {
                std::cerr << "\nCode generation failed for main module:\n";
                errorReporter.printErrors();
                return 1;
            }

            fullOutput << "// ============================================\n";
            fullOutput << "// Module: __main__\n";
            fullOutput << "// ============================================\n";
            fullOutput << mainCode << "\n";
            mainModule->isCompiled = true;
        }

        // Write output
        std::string finalCode = fullOutput.str();
        std::cout << "Writing output to: " << outputFile << "\n";
        writeFile(outputFile, finalCode);

        std::cout << "\n✓ Multi-file compilation successful!\n";
        std::cout << "  Compiled " << allModules.size() << " module(s) + main file\n";
        std::cout << "  Generated " << finalCode.length() << " bytes of C++ code\n";
        std::cout << "\nTo compile the generated C++ code:\n";
        std::cout << "  g++ -std=c++17 " << outputFile << " -o output.exe\n";
        std::cout << "  or\n";
        std::cout << "  cl /EHsc /std:c++17 /I. " << outputFile << "\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
