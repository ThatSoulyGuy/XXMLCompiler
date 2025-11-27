#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include "Lexer/Lexer.h"
#include "Parser/Parser.h"
#include "Semantic/SemanticAnalyzer.h"
#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/BackendRegistry.h"
#include "Backends/LLVMBackend.h"
#include "Common/Error.h"
#include "Import/Module.h"
#include "Import/ImportResolver.h"
#include "Import/DependencyGraph.h"
#include "Linker/LinkerInterface.h"
#include "Utils/ProcessUtils.h"

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

std::vector<std::string> extractImports(const XXML::Parser::Program& ast) {
    std::vector<std::string> imports;
    for (const auto& decl : ast.declarations) {
        if (auto importDecl = dynamic_cast<XXML::Parser::ImportDecl*>(decl.get())) {
            imports.push_back(importDecl->modulePath);
        }
    }
    return imports;
}

bool parseModule(XXML::Import::Module* module, XXML::Common::ErrorReporter& errorReporter) {
    if (module->isParsed) return true;

    XXML::Lexer::Lexer lexer(module->fileContent, module->filePath, errorReporter);
    auto tokens = lexer.tokenize();

    if (errorReporter.hasErrors()) {
        std::cerr << "  Lexical analysis failed for " << module->moduleName << "\n";
        return false;
    }

    XXML::Parser::Parser parser(tokens, errorReporter);
    module->ast = parser.parse();

    if (errorReporter.hasErrors()) {
        std::cerr << "  Syntax analysis failed for " << module->moduleName << "\n";
        return false;
    }

    module->isParsed = true;
    module->imports = extractImports(*module->ast);
    return true;
}

void printUsage(const char* programName) {
    std::cerr << "XXML Compiler v2.0\n";
    std::cerr << "Usage: " << programName << " <input.XXML> <output> [mode]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  <input.XXML>  - XXML source file to compile\n";
    std::cerr << "  <output>      - Output file (.ll for IR, .exe/.out for executable)\n";
    std::cerr << "  [mode]        - Optional: 2 = LLVM IR only (default compiles to executable)\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << programName << " Hello.XXML hello.exe      # Compile to executable\n";
    std::cerr << "  " << programName << " Hello.XXML hello.ll 2     # Generate LLVM IR only\n";
}

int main(int argc, char* argv[]) {
    std::cout << "XXML Compiler v2.0 (LLVM Backend)\n";
    std::cout << "==================================\n\n";

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile;
    bool llvmIROnly = false;

    // Parse command-line arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[i + 1];
            i++; // Skip the next argument
        } else if (arg == "2") {
            llvmIROnly = true;
        } else if (outputFile.empty()) {
            // If no -o flag, treat as direct output file
            outputFile = arg;
        }
    }

    if (outputFile.empty()) {
        std::cerr << "Error: No output file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    try {
        // Create compilation context with LLVM backend
        XXML::Core::CompilerConfig config;
        config.defaultBackend = XXML::Core::BackendTarget::LLVM_IR;
        XXML::Core::CompilationContext compilationContext(config);

        XXML::Common::ErrorReporter errorReporter;
        XXML::Import::ImportResolver resolver;

        // Add source file directory to search paths
        resolver.addSourceFileDirectory(inputFile);

        // Create and parse main module
        std::cout << "Reading: " << inputFile << "\n";
        auto mainModule = std::make_unique<XXML::Import::Module>("__main__", inputFile);
        if (!mainModule->loadFromFile()) {
            std::cerr << "Error: Could not read file: " << inputFile << "\n";
            return 1;
        }

        std::cout << "Parsing...\n";
        if (!parseModule(mainModule.get(), errorReporter)) {
            errorReporter.printErrors();
            return 1;
        }

        // Auto-import standard library
        std::vector<std::string> stdLibBasePaths = {
            "Language/Core/", "../Language/Core/",
            "Language/System/", "../Language/System/",
            "Language/Collections/", "../Language/Collections/"
        };
        std::vector<std::string> stdLibCoreFiles = {"None.XXML", "String.XXML", "Integer.XXML", "Bool.XXML", "Float.XXML", "Double.XXML"};
        std::vector<std::string> stdLibSystemFiles = {"Console.XXML"};
        std::vector<std::string> stdLibCollectionFiles = {"List.XXML", "Array.XXML", "HashMap.XXML"};

        std::vector<std::string> autoImports;
        auto findFiles = [&](const std::vector<std::string>& fileNames, const std::string& subdir) {
            for (const auto& fileName : fileNames) {
                for (const auto& basePath : stdLibBasePaths) {
                    if (basePath.find(subdir) != std::string::npos) {
                        std::string fullPath = basePath + fileName;
                        std::ifstream check(fullPath);
                        if (check.good()) {
                            autoImports.push_back(fullPath);
                            break;
                        }
                    }
                }
            }
        };
        findFiles(stdLibCoreFiles, "/Core/");
        findFiles(stdLibSystemFiles, "/System/");
        findFiles(stdLibCollectionFiles, "/Collections/");

        // Resolve all imports
        std::set<std::string> processedImports;
        std::vector<XXML::Import::Module*> allModules;
        std::vector<std::unique_ptr<XXML::Import::Module>> ownedModules;
        allModules.push_back(mainModule.get());

        std::vector<std::string> toProcess = mainModule->imports;
        for (const auto& autoImport : autoImports) {
            toProcess.push_back(autoImport);
            mainModule->imports.push_back(autoImport);
        }

        std::cout << "Resolving imports...\n";
        while (!toProcess.empty()) {
            std::string importPath = toProcess.back();
            toProcess.pop_back();

            if (processedImports.find(importPath) != processedImports.end()) continue;
            processedImports.insert(importPath);

            if (importPath.find(".XXML") != std::string::npos || importPath.find("/") != std::string::npos) {
                // Direct file path import
                auto module = std::make_unique<XXML::Import::Module>(importPath, importPath);
                if (!module->loadFromFile()) continue;
                if (!parseModule(module.get(), errorReporter)) {
                    errorReporter.printErrors();
                    return 1;
                }

                XXML::Import::Module* modulePtr = module.get();
                allModules.push_back(modulePtr);
                ownedModules.push_back(std::move(module));

                for (const auto& subImport : modulePtr->imports) {
                    if (processedImports.find(subImport) == processedImports.end()) {
                        toProcess.push_back(subImport);
                    }
                }
            } else if (importPath.find("::") != std::string::npos) {
                // Namespace import (e.g., "Language::Reflection")
                // Use ImportResolver to find all files in that namespace
                auto resolvedModules = resolver.resolveImport(importPath);
                for (auto* resolvedModule : resolvedModules) {
                    if (!resolvedModule->isParsed) {
                        if (!parseModule(resolvedModule, errorReporter)) {
                            errorReporter.printErrors();
                            return 1;
                        }
                    }

                    // Check if this module is already in our list
                    bool alreadyAdded = false;
                    for (auto* existing : allModules) {
                        if (existing->moduleName == resolvedModule->moduleName) {
                            alreadyAdded = true;
                            break;
                        }
                    }

                    if (!alreadyAdded) {
                        allModules.push_back(resolvedModule);

                        // Process sub-imports
                        for (const auto& subImport : resolvedModule->imports) {
                            if (processedImports.find(subImport) == processedImports.end()) {
                                toProcess.push_back(subImport);
                            }
                        }
                    }
                }
            }
        }

        std::cout << "  Loaded " << allModules.size() << " module(s)\n";

        // Build dependency graph
        XXML::Import::DependencyGraph depGraph;
        for (auto module : allModules) depGraph.addModule(module->moduleName);
        for (auto module : allModules) {
            for (const auto& importPath : module->imports) {
                for (auto otherModule : allModules) {
                    if (otherModule != module &&
                        (otherModule->moduleName.find(importPath) == 0 || importPath.find(otherModule->moduleName) == 0)) {
                        depGraph.addDependency(module->moduleName, otherModule->moduleName);
                    }
                }
            }
        }

        std::vector<std::string> cycle;
        if (depGraph.hasCycle(cycle)) {
            std::cerr << "Error: Circular dependency detected!\n";
            return 1;
        }

        auto compilationOrder = depGraph.topologicalSort();
        std::map<std::string, XXML::Import::Module*> moduleMap;
        for (auto module : allModules) moduleMap[module->moduleName] = module;

        std::map<std::string, std::unique_ptr<XXML::Semantic::SemanticAnalyzer>> analyzerMap;

        // Phase 1: Registration
        std::cout << "Phase 1: Registering types...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto analyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                analyzer->setValidationEnabled(false);
                analyzer->analyze(*it->second->ast);
                if (errorReporter.hasErrors()) {
                    errorReporter.printErrors();
                    return 1;
                }
                analyzerMap[moduleName] = std::move(analyzer);
            }
        }

        auto mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setValidationEnabled(false);
        mainAnalyzer->analyze(*mainModule->ast);
        if (errorReporter.hasErrors()) {
            errorReporter.printErrors();
            return 1;
        }

        // Collect template classes
        // ✅ SAFE: Use TemplateClassInfo instead of raw ClassDecl pointers
        std::unordered_map<std::string, XXML::Semantic::SemanticAnalyzer::TemplateClassInfo> allTemplateClasses;
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, templateInfo] : analyzer->getTemplateClasses()) {
                allTemplateClasses[name] = templateInfo;
            }
        }

        // Phase 2: Validation
        std::cout << "Phase 2: Semantic analysis...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                auto validator = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                validator->setValidationEnabled(true);
                validator->setModuleName(moduleName);
                for (const auto& [name, templateInfo] : allTemplateClasses) {
                    validator->registerTemplateClass(name, templateInfo);
                }
                validator->analyze(*it->second->ast);
                if (errorReporter.hasErrors()) {
                    errorReporter.printErrors();
                    return 1;
                }
                it->second->isAnalyzed = true;
                analyzerMap[moduleName] = std::move(validator);
            }
        }

        mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setValidationEnabled(true);
        mainAnalyzer->setModuleName("__main__");
        for (const auto& [name, templateInfo] : allTemplateClasses) {
            mainAnalyzer->registerTemplateClass(name, templateInfo);
        }
        mainAnalyzer->analyze(*mainModule->ast);
        if (errorReporter.hasErrors()) {
            errorReporter.printErrors();
            return 1;
        }

        // Merge template info
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, templateInfo] : analyzer->getTemplateClasses()) {
                mainAnalyzer->registerTemplateClass(name, templateInfo);
            }
            for (const auto& inst : analyzer->getTemplateInstantiations()) {
                mainAnalyzer->mergeTemplateInstantiation(inst);
            }
        }

        // Generate LLVM IR
        std::cout << "Generating LLVM IR...\n";
        auto* backend = compilationContext.getActiveBackend();
        auto* llvmBackend = dynamic_cast<XXML::Backends::LLVMBackend*>(backend);
        if (llvmBackend) {
            if (mainAnalyzer) {
                llvmBackend->setSemanticAnalyzer(mainAnalyzer.get());
            }

            // Pass imported modules to backend for code generation
            // This ensures standard library classes are included in the output
            std::vector<XXML::Parser::Program*> importedASTs;
            for (const auto& moduleName : compilationOrder) {
                auto it = moduleMap.find(moduleName);
                if (it != moduleMap.end() && it->second != mainModule.get() && it->second->ast) {
                    importedASTs.push_back(it->second->ast.get());
                }
            }
            llvmBackend->setImportedModules(importedASTs);
        }

        std::string llvmIR = backend->generate(*mainModule->ast);

        std::filesystem::path outputPath(outputFile);

        if (llvmIROnly || outputPath.extension() == ".ll") {
            // Write LLVM IR only
            std::cout << "Writing LLVM IR to: " << outputFile << "\n";
            writeFile(outputFile, llvmIR);
            std::cout << "\n✓ Generated " << llvmIR.length() << " bytes of LLVM IR\n";
            std::cout << "\nTo compile: clang " << outputFile << " -o output\n";
        } else {
            // Compile to executable
            std::string executablePath = outputPath.string();
#ifdef _WIN32
            if (outputPath.extension() != ".exe") {
                executablePath = outputPath.stem().string() + ".exe";
            }
#endif

            std::cout << "Compiling to object file...\n";
            std::string objPath = outputPath.stem().string() + ".obj";
            if (!llvmBackend->generateObjectFile(llvmIR, objPath, 0)) {
                std::cerr << "✗ Object file generation failed\n";
                return 1;
            }

            // Find runtime library
            std::string exeDir = XXML::Utils::ProcessUtils::getExecutableDirectory();
            std::string runtimeLibPath;
#ifdef _WIN32
            std::vector<std::string> tryPaths = {
                // Relative paths first (finds the correct library for the current build)
                // Ninja generator: bin/xxml.exe -> lib/XXMLLLVMRuntime.lib (most common)
                exeDir + "\\..\\lib\\XXMLLLVMRuntime.lib",              // MSVC/Ninja
                exeDir + "\\..\\lib\\libXXMLLLVMRuntime.a",             // MinGW/Ninja
                exeDir + "\\..\\lib\\Release\\XXMLLLVMRuntime.lib",     // MSVC/Ninja Release
                // Visual Studio generator: bin/Release/xxml.exe or bin/Debug/xxml.exe
                exeDir + "\\..\\..\\lib\\Release\\XXMLLLVMRuntime.lib", // VS Release
                exeDir + "\\..\\..\\lib\\Debug\\XXMLLLVMRuntime.lib",   // VS Debug
                exeDir + "\\..\\..\\lib\\XXMLLLVMRuntime.lib",          // VS
                // Fallback to absolute paths
                "build_mingw\\lib\\libXXMLLLVMRuntime.a",
                "build\\lib\\libXXMLLLVMRuntime.a",
                "build\\x64-release\\lib\\libXXMLLLVMRuntime.a",
                "build\\vs-x64-release\\lib\\Release\\XXMLLLVMRuntime.lib",
                "build\\vs-x64-release\\lib\\Debug\\XXMLLLVMRuntime.lib",
                "build\\lib\\Release\\XXMLLLVMRuntime.lib",
                "build\\lib\\XXMLLLVMRuntime.lib",
                "build\\x64-release\\lib\\XXMLLLVMRuntime.lib"
            };
#else
            std::vector<std::string> tryPaths = {
                exeDir + "/../lib/libXXMLLLVMRuntime.a",
                "build/lib/libXXMLLLVMRuntime.a",
                "build_mingw/lib/libXXMLLLVMRuntime.a"
            };
#endif
            for (const auto& path : tryPaths) {
                if (XXML::Utils::ProcessUtils::fileExists(path)) {
                    runtimeLibPath = path;
                    break;
                }
            }

            // Link
            std::cout << "Linking...\n";
            auto linker = XXML::Linker::LinkerFactory::createLinker();
            if (!linker) {
                std::cerr << "✗ No linker found. Object file: " << objPath << "\n";
                return 1;
            }

            XXML::Linker::LinkConfig linkConfig;
            linkConfig.objectFiles.push_back(objPath);
            if (!runtimeLibPath.empty()) linkConfig.libraries.push_back(runtimeLibPath);
            linkConfig.outputPath = executablePath;
            linkConfig.createConsoleApp = true;

            auto linkResult = linker->link(linkConfig);
            if (!linkResult.success) {
                std::cerr << "✗ Linking failed: " << linkResult.error << "\n";
                return 1;
            }

            std::cout << "\n✓ Compilation successful!\n";
            std::cout << "  Executable: " << executablePath << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
