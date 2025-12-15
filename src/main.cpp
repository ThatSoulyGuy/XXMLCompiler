#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include "Common/Version.h"
#include "Lexer/Lexer.h"
#include "Parser/Parser.h"
#include "Semantic/SemanticAnalyzer.h"
#include "Semantic/SemanticVerifier.h"
#include "Semantic/PassResults.h"
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
#include "AnnotationProcessor/ProcessorLoader.h"
#include "AnnotationProcessor/ProcessorCompiler.h"
#include "Derive/InLanguageDeriveRegistry.h"

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

bool parseModule(XXML::Import::Module* module, XXML::Common::ErrorReporter& errorReporter, bool isSTLFile = false) {
    if (module->isParsed) return true;

    // Set current file context for STL warning suppression
    errorReporter.setCurrentFile(module->filePath, isSTLFile);
    module->isSTLFile = isSTLFile;

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
    std::cerr << XXML_VERSION_FULL << "\n";
    std::cerr << "Usage: " << programName << " [options] <input.XXML> -o <output>\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  -o <file>              Output file (.ll for IR, .exe/.dll for binary)\n";
    std::cerr << "  -I <dir>               Add directory to import search path (can be used multiple times)\n";
    std::cerr << "  --include-dir=<dir>    Same as -I <dir>\n";
    std::cerr << "  --ir                   Generate LLVM IR only (same as mode 2)\n";
    std::cerr << "  --emit-llvm            Alias for --ir\n";
    std::cerr << "  --processor            Compile annotation processor to DLL\n";
    std::cerr << "  --use-processor=<dll>  Load annotation processor DLL (can be used multiple times)\n";
    std::cerr << "  --derive               Compile in-language derive to DLL\n";
    std::cerr << "  --use-derive=<dll>     Load in-language derive DLL (can be used multiple times)\n";
    std::cerr << "  --stl-warnings         Show warnings for standard library files (off by default)\n";
    std::cerr << "  --dump-ownership       Dump ownership analysis information for debugging\n";
    std::cerr << "  2                      Legacy mode: LLVM IR only\n\n";
    std::cerr << "Optimization Options:\n";
    std::cerr << "  -O0                    No optimization (default, fastest compilation)\n";
    std::cerr << "  -O1                    Basic optimization\n";
    std::cerr << "  -O2                    Standard optimization (recommended for release)\n";
    std::cerr << "  -O3                    Aggressive optimization (may increase code size)\n";
    std::cerr << "  -Os                    Optimize for size\n";
    std::cerr << "  -Oz                    Aggressively optimize for size\n";
    std::cerr << "  -g                     Include debug symbols (disabled by default with -O1+)\n";
    std::cerr << "  --no-debug             Explicitly disable debug symbols\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << programName << " Hello.XXML -o hello.exe                    # Compile to executable\n";
    std::cerr << "  " << programName << " Hello.XXML -o hello.exe -O2                # Optimized release build\n";
    std::cerr << "  " << programName << " Hello.XXML -o hello.exe -O2 -g             # Optimized with debug info\n";
    std::cerr << "  " << programName << " Hello.XXML -o hello.ll --ir                # Generate LLVM IR only\n";
    std::cerr << "  " << programName << " --processor MyAnnot.XXML -o MyAnnot.dll    # Compile processor DLL\n";
    std::cerr << "  " << programName << " --use-processor=MyAnnot.dll App.XXML -o app.exe  # Use processor\n";
    std::cerr << "  " << programName << " --derive MyDerive.XXML -o MyDerive.dylib  # Compile derive DLL\n";
    std::cerr << "  " << programName << " --use-derive=MyDerive.dylib App.XXML -o app.exe  # Use derive\n";
}

int main(int argc, char* argv[]) {
    std::cout << XXML_VERSION_FULL << " (LLVM Backend)\n";
    std::cout << "==================================\n\n";

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputFile;
    std::string outputFile;
    bool llvmIROnly = false;
    bool processorMode = false;
    bool deriveMode = false;
    bool showSTLWarnings = false;
    bool dumpOwnership = false;
    std::vector<std::string> processorDLLs;
    std::vector<std::string> deriveDLLs;
    std::vector<std::string> includeDirs;  // Additional import search paths

    // Optimization settings
    int optimizationLevel = 0;         // 0-3, or special values for Os/Oz
    std::string optimizationMode = ""; // "", "s", or "z" for size optimizations
    bool includeDebugSymbols = true;   // Default to debug symbols (will be disabled by -O1+ unless -g)
    bool explicitDebugFlag = false;    // Track if user explicitly set -g or --no-debug

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[i + 1];
            i++; // Skip the next argument
        } else if (arg == "2" || arg == "--ir" || arg == "--emit-llvm") {
            llvmIROnly = true;
        } else if (arg == "--processor") {
            processorMode = true;
        } else if (arg == "--derive") {
            deriveMode = true;
        } else if (arg.rfind("--use-derive=", 0) == 0) {
            // Extract DLL path after '='
            std::string dllPath = arg.substr(13);
            if (!dllPath.empty()) {
                deriveDLLs.push_back(dllPath);
            }
        } else if (arg.rfind("--use-processor=", 0) == 0) {
            // Extract DLL path after '='
            std::string dllPath = arg.substr(16);
            if (!dllPath.empty()) {
                processorDLLs.push_back(dllPath);
            }
        } else if (arg == "--stl-warnings") {
            showSTLWarnings = true;
        } else if (arg == "--dump-ownership") {
            dumpOwnership = true;
        } else if (arg == "-I" && i + 1 < argc) {
            // -I <dir> - add import search path
            includeDirs.push_back(argv[i + 1]);
            i++; // Skip the next argument
        } else if (arg.rfind("--include-dir=", 0) == 0) {
            // --include-dir=<dir> - add import search path
            std::string dir = arg.substr(14);
            if (!dir.empty()) {
                includeDirs.push_back(dir);
            }
        } else if (arg == "-O0") {
            optimizationLevel = 0;
            optimizationMode = "";
        } else if (arg == "-O1") {
            optimizationLevel = 1;
            optimizationMode = "";
        } else if (arg == "-O2") {
            optimizationLevel = 2;
            optimizationMode = "";
        } else if (arg == "-O3") {
            optimizationLevel = 3;
            optimizationMode = "";
        } else if (arg == "-Os") {
            optimizationLevel = 2;  // Size optimization uses O2 as base
            optimizationMode = "s";
        } else if (arg == "-Oz") {
            optimizationLevel = 2;  // Aggressive size optimization uses O2 as base
            optimizationMode = "z";
        } else if (arg == "-g") {
            includeDebugSymbols = true;
            explicitDebugFlag = true;
        } else if (arg == "--no-debug") {
            includeDebugSymbols = false;
            explicitDebugFlag = true;
        } else if (arg[0] != '-') {
            // Positional argument - input file
            if (inputFile.empty()) {
                inputFile = arg;
            } else if (outputFile.empty()) {
                // Legacy: second positional arg is output
                outputFile = arg;
            }
        }
    }

    // By default, disable debug symbols for optimized builds unless explicitly requested
    if (!explicitDebugFlag && optimizationLevel > 0) {
        includeDebugSymbols = false;
    }

    if (inputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    if (outputFile.empty()) {
        std::cerr << "Error: No output file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    // Processor mode: compile annotation to DLL
    if (processorMode) {
        std::cout << "Processor compilation mode\n\n";
    }

    // Configure STL warning suppression (default: suppress, enabled with --stl-warnings)
    XXML::Common::ErrorReporter::setSuppressSTLWarnings(!showSTLWarnings);

    try {
        // Create compilation context with LLVM backend
        XXML::Core::CompilerConfig config;
        config.defaultBackend = XXML::Core::BackendTarget::LLVM_IR;
        XXML::Core::CompilationContext compilationContext(config);

        XXML::Common::ErrorReporter errorReporter;
        XXML::Import::ImportResolver resolver;
        XXML::AnnotationProcessor::ProcessorRegistry processorRegistry;
        XXML::Derive::InLanguageDeriveRegistry deriveRegistry;

        // Initialize file discovery with compiler path and source file path
        resolver.initializeWithCompilerPath(argv[0]);
        resolver.initializeWithSourceFile(inputFile);

        // Add user-specified include directories (highest priority for imports)
        // Use addPrioritySearchPath to insert at the beginning of search paths
        // so they are searched BEFORE stdlib paths
        for (const auto& dir : includeDirs) {
            std::cout << "Adding include path (priority): " << dir << "\n";
            resolver.addPrioritySearchPath(dir);
        }

        // Auto-discover and load processor DLLs from standard locations
        std::vector<std::string> processorSearchPaths;

        // Add source file's directory processors/
        std::filesystem::path inputPath(inputFile);
        if (inputPath.has_parent_path()) {
            processorSearchPaths.push_back((inputPath.parent_path() / "processors").string());
        }

        // Add compiler's directory processors/
        std::string exeDir = XXML::Utils::ProcessUtils::getExecutableDirectory();
        processorSearchPaths.push_back(exeDir + "/processors");
        processorSearchPaths.push_back(exeDir + "/../processors");

        // Add current directory processors/
        processorSearchPaths.push_back("./processors");
        processorSearchPaths.push_back("processors");

        // Auto-load processors from discovered directories
        for (const auto& searchPath : processorSearchPaths) {
            try {
                if (std::filesystem::exists(searchPath) && std::filesystem::is_directory(searchPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension().string();
#ifdef _WIN32
                            if (ext == ".dll" || ext == ".DLL") {
#else
                            if (ext == ".so") {
#endif
                                std::string dllPath = entry.path().string();
                                // Check if not already loaded
                                bool alreadyLoaded = false;
                                for (const auto& loaded : processorDLLs) {
                                    if (loaded == dllPath) {
                                        alreadyLoaded = true;
                                        break;
                                    }
                                }
                                if (!alreadyLoaded) {
                                    processorDLLs.push_back(dllPath);
                                }
                            }
                        }
                    }
                }
            } catch (const std::filesystem::filesystem_error&) {
                // Ignore directory access errors
            }
        }

        // Load user-specified and auto-discovered processor DLLs
        if (!processorDLLs.empty()) {
            std::cout << "Loading annotation processors...\n";
            for (const auto& dllPath : processorDLLs) {
                std::cout << "  Loading: " << dllPath << "\n";
                if (!processorRegistry.loadProcessor(dllPath, errorReporter)) {
                    std::cerr << "  Warning: Failed to load processor: " << dllPath << "\n";
                    // Don't exit on failure - processor might be optional
                    errorReporter.clear();  // Clear the error to continue
                } else {
                    std::cout << "  Loaded successfully\n";
                }
            }
        }

        // Load user-specified derive DLLs
        if (!deriveDLLs.empty()) {
            std::cout << "Loading in-language derives...\n";
            for (const auto& dllPath : deriveDLLs) {
                std::cout << "  Loading: " << dllPath << "\n";
                if (!deriveRegistry.loadDerive(dllPath)) {
                    std::cerr << "  Warning: Failed to load derive: " << dllPath << "\n";
                }
            }
        }

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
        bool mainIsSTL = resolver.isSTLFile(inputFile);
        if (!parseModule(mainModule.get(), errorReporter, mainIsSTL)) {
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
                bool isSTL = resolver.isSTLFile(importPath);
                if (!parseModule(module.get(), errorReporter, isSTL)) {
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
            } else {
                // Namespace import (e.g., "Language::Reflection") or simple package name (e.g., "GLFW")
                // Use ImportResolver to find all files in that namespace/directory
                auto resolvedModules = resolver.resolveImport(importPath);
                for (auto* resolvedModule : resolvedModules) {
                    if (!resolvedModule->isParsed) {
                        bool isSTL = resolver.isSTLFile(resolvedModule->filePath);
                        if (!parseModule(resolvedModule, errorReporter, isSTL)) {
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
            std::cerr << "  Cycle: ";
            for (size_t i = 0; i < cycle.size(); ++i) {
                if (i > 0) std::cerr << " -> ";
                std::cerr << cycle[i];
            }
            std::cerr << std::endl;
            return 1;
        }

        auto compilationOrder = depGraph.topologicalSort();
        std::map<std::string, XXML::Import::Module*> moduleMap;
        for (auto module : allModules) moduleMap[module->moduleName] = module;

        std::map<std::string, std::unique_ptr<XXML::Semantic::SemanticAnalyzer>> analyzerMap;

        // Phase 1: Registration
        // ✅ FIXED: Share templates progressively so imported templates are available
        std::unordered_map<std::string, XXML::Semantic::SemanticAnalyzer::TemplateClassInfo> allTemplateClasses;

        std::cout << "Phase 1: Registering types...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end()) {
                // Set file context for STL warning suppression during analysis
                errorReporter.setCurrentFile(it->second->filePath, it->second->isSTLFile);
                auto analyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                analyzer->setValidationEnabled(false);
                // Register templates from previously processed modules BEFORE analysis
                for (const auto& [name, templateInfo] : allTemplateClasses) {
                    analyzer->registerTemplateClass(name, templateInfo);
                }
                // Set in-language derive registry for @Derive annotations
                analyzer->getDeriveRegistry().setInLanguageRegistry(&deriveRegistry);
                analyzer->analyze(*it->second->ast);
                if (errorReporter.hasErrors()) {
                    errorReporter.printErrors();
                    return 1;
                }
                // Collect templates from this module for subsequent modules
                for (const auto& [name, templateInfo] : analyzer->getTemplateClasses()) {
                    allTemplateClasses[name] = templateInfo;
                }
                analyzerMap[moduleName] = std::move(analyzer);
            }
        }

        // Set file context for main module (not STL)
        errorReporter.setCurrentFile(mainModule->filePath, mainModule->isSTLFile);
        auto mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setValidationEnabled(false);
        // Register templates from all imported modules BEFORE analyzing main module
        for (const auto& [name, templateInfo] : allTemplateClasses) {
            mainAnalyzer->registerTemplateClass(name, templateInfo);
        }
        mainAnalyzer->analyze(*mainModule->ast);
        if (errorReporter.hasErrors()) {
            errorReporter.printErrors();
            return 1;
        }

        // Templates are already collected in allTemplateClasses
        // ✅ SAFE: Use TemplateClassInfo instead of raw ClassDecl pointers

        // Collect annotations from all modules (needed for @annotations defined in library files)
        std::unordered_map<std::string, XXML::Semantic::SemanticAnalyzer::AnnotationInfo> allAnnotations;
        std::vector<XXML::Semantic::SemanticAnalyzer::PendingProcessorCompilation> allPendingProcessors;
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, annotInfo] : analyzer->getAnnotationRegistry()) {
                allAnnotations[name] = annotInfo;
            }
            for (const auto& pending : analyzer->getPendingProcessorCompilations()) {
                allPendingProcessors.push_back(pending);
            }
        }

        // Build unified class registry from all Phase 1 analyzers BEFORE Phase 2
        // This ensures cross-module type resolution works during validation
        // Use the first analyzer's registry type for the unified map
        auto allClasses = analyzerMap.begin()->second->getClassRegistry();
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, info] : analyzer->getClassRegistry()) {
                if (allClasses.find(name) == allClasses.end()) {
                    allClasses[name] = info;
                }
            }
        }

        // Build unified enum registry from all Phase 1 analyzers
        auto allEnums = analyzerMap.begin()->second->getEnumRegistry();
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, info] : analyzer->getEnumRegistry()) {
                if (allEnums.find(name) == allEnums.end()) {
                    allEnums[name] = info;
                }
            }
        }

        // Phase 2: Validation
        std::cout << "Phase 2: Semantic analysis...\n";
        for (const auto& moduleName : compilationOrder) {
            auto it = moduleMap.find(moduleName);
            if (it != moduleMap.end() && it->second != mainModule.get()) {
                // Set file context for STL warning suppression during analysis
                errorReporter.setCurrentFile(it->second->filePath, it->second->isSTLFile);
                // Process imported modules (not the main module)
                auto validator = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
                validator->setValidationEnabled(true);
                validator->setModuleName(moduleName);
                for (const auto& [name, templateInfo] : allTemplateClasses) {
                    validator->registerTemplateClass(name, templateInfo);
                }
                // Register annotations from other modules
                for (const auto& [name, annotInfo] : allAnnotations) {
                    validator->registerAnnotation(name, annotInfo);
                }
                // Merge class and enum registries from all modules for cross-module type resolution
                validator->mergeClassRegistry(allClasses);
                validator->mergeEnumRegistry(allEnums);
                // Set in-language derive registry for @Derive annotations
                validator->getDeriveRegistry().setInLanguageRegistry(&deriveRegistry);
                validator->analyze(*it->second->ast);
                if (errorReporter.hasErrors()) {
                    errorReporter.printErrors();
                    return 1;
                }
                // Don't process annotations for library modules - they don't define annotations
                // that need compile-time processing
                it->second->isAnalyzed = true;
                analyzerMap[moduleName] = std::move(validator);
            }
        }

        // Process main module - set file context for user code warnings
        errorReporter.setCurrentFile(mainModule->filePath, mainModule->isSTLFile);
        mainAnalyzer = std::make_unique<XXML::Semantic::SemanticAnalyzer>(compilationContext, errorReporter);
        mainAnalyzer->setModuleName("__main__");
        for (const auto& [name, templateInfo] : allTemplateClasses) {
            mainAnalyzer->registerTemplateClass(name, templateInfo);
        }
        // Register annotations from imported modules so main module can use them
        for (const auto& [name, annotInfo] : allAnnotations) {
            mainAnalyzer->registerAnnotation(name, annotInfo);
        }
        // Merge class registries, expression types, and enum registries from imported modules BEFORE runPipeline()
        // so that type resolution can find types from other modules
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            mainAnalyzer->mergeClassRegistry(analyzer->getClassRegistry());
            mainAnalyzer->mergeExpressionTypes(analyzer->getExpressionTypes());
            mainAnalyzer->mergeEnumRegistry(analyzer->getEnumRegistry());
        }
        // Set in-language derive registry BEFORE analysis (derives are processed during class analysis)
        mainAnalyzer->getDeriveRegistry().setInLanguageRegistry(&deriveRegistry);
        // Run the full multi-stage pipeline (TypeCanonicalizer -> SemanticAnalysis ->
        // TemplateExpander -> OwnershipAnalyzer -> LayoutComputer -> ABILowering)
        auto passResults = mainAnalyzer->runPipeline(*mainModule->ast);

        // Dump ownership information if requested
        if (dumpOwnership) {
            std::cout << "\n=== Ownership Analysis Dump ===\n\n";

            // Dump variable states
            const auto& ownershipResult = passResults.ownershipAnalysis;
            std::cout << "Variable States:\n";
            for (const auto& [varName, state] : ownershipResult.variableStates) {
                std::string stateStr;
                switch (state) {
                    case XXML::Semantic::OwnershipState::Owned: stateStr = "Owned"; break;
                    case XXML::Semantic::OwnershipState::Moved: stateStr = "Moved"; break;
                    case XXML::Semantic::OwnershipState::Borrowed: stateStr = "Borrowed"; break;
                    case XXML::Semantic::OwnershipState::Invalid: stateStr = "Invalid"; break;
                }
                std::cout << "  " << varName << " -> " << stateStr << "\n";
            }

            // Dump violations if any
            if (!ownershipResult.violations.empty()) {
                std::cout << "\nOwnership Violations (" << ownershipResult.violations.size() << "):\n";
                for (const auto& violation : ownershipResult.violations) {
                    std::string kindStr;
                    switch (violation.kind) {
                        case XXML::Semantic::OwnershipViolation::Kind::UseAfterMove:
                            kindStr = "UseAfterMove"; break;
                        case XXML::Semantic::OwnershipViolation::Kind::DoubleMoveViolation:
                            kindStr = "DoubleMove"; break;
                        case XXML::Semantic::OwnershipViolation::Kind::DanglingReference:
                            kindStr = "DanglingReference"; break;
                        case XXML::Semantic::OwnershipViolation::Kind::InvalidCapture:
                            kindStr = "InvalidCapture"; break;
                        case XXML::Semantic::OwnershipViolation::Kind::BorrowWhileMoved:
                            kindStr = "BorrowWhileMoved"; break;
                    }
                    std::cout << "  [" << kindStr << "] " << violation.variableName
                              << ": " << violation.message << "\n";
                    if (!violation.useLocation.filename.empty()) {
                        std::cout << "    at " << violation.useLocation.filename
                                  << ":" << violation.useLocation.line
                                  << ":" << violation.useLocation.column << "\n";
                    }
                    if (!violation.moveLocation.filename.empty()) {
                        std::cout << "    (moved at " << violation.moveLocation.filename
                                  << ":" << violation.moveLocation.line << ")\n";
                    }
                }
            } else {
                std::cout << "\nNo ownership violations detected.\n";
            }

            // Dump layout information with ownership
            std::cout << "\nClass Layouts with Ownership:\n";
            for (const auto& [className, layout] : passResults.layoutComputation.layouts) {
                std::cout << "  " << className << " (" << layout.totalSize << " bytes, align " << layout.alignment << "):\n";
                for (const auto& field : layout.fields) {
                    std::string ownershipStr;
                    switch (field.ownership) {
                        case XXML::Parser::OwnershipType::None: ownershipStr = ""; break;
                        case XXML::Parser::OwnershipType::Owned: ownershipStr = "^"; break;
                        case XXML::Parser::OwnershipType::Reference: ownershipStr = "&"; break;
                        case XXML::Parser::OwnershipType::Copy: ownershipStr = "%"; break;
                    }
                    std::cout << "    +" << field.offset << ": " << field.name
                              << " : " << field.typeName << ownershipStr
                              << " (" << field.size << " bytes)\n";
                }
            }
            std::cout << "\n=== End Ownership Dump ===\n\n";
        }

        if (errorReporter.hasErrors() || !passResults.allSuccessful()) {
            errorReporter.printErrors();
            return 1;
        }

        // Merge pending processors from imported modules with main module's processors
        // This allows annotations defined in library files to have their processors compiled
        mainAnalyzer->mergePendingProcessorCompilations(allPendingProcessors);

        // Check for inline annotation processors and auto-compile them
        // (Skip this when in processor mode to avoid infinite recursion)
        const auto& pendingProcessors = mainAnalyzer->getPendingProcessorCompilations();
        if (!pendingProcessors.empty() && !processorMode) {
            std::cout << "Auto-compiling " << pendingProcessors.size() << " inline processor(s)...\n";

            // Get compiler path for subprocess invocation
            std::string exeDir = XXML::Utils::ProcessUtils::getExecutableDirectory();
#ifdef _WIN32
            std::string compilerPath = exeDir + "/xxml.exe";
#else
            std::string compilerPath = exeDir + "/xxml";
#endif
            XXML::AnnotationProcessor::ProcessorCompiler procCompiler(compilerPath);

            for (const auto& pending : pendingProcessors) {
                std::cout << "  Compiling @" << pending.annotationName << " processor...\n";

                // Create ProcessorInfo for compilation
                XXML::AnnotationProcessor::ProcessorCompiler::ProcessorInfo info;
                info.annotationName = pending.annotationName;
                info.annotDecl = pending.annotDecl;
                info.processorDecl = pending.processorDecl;
                info.imports = pending.imports;  // Pass imports so processor can access imported modules
                info.userClasses = pending.userClasses;  // Pass user classes so processor can reference them

                // Attempt to compile
                auto result = procCompiler.compileProcessor(info, errorReporter);

                if (result.success) {
                    std::cout << "    ✓ Compiled successfully: " << result.dllPath << "\n";

                    // Load the compiled processor
                    if (processorRegistry.loadProcessor(result.dllPath, errorReporter)) {
                        std::cout << "    ✓ Loaded into registry\n";
                    } else {
                        std::cerr << "    ✗ Warning: Failed to load compiled processor\n";
                        errorReporter.clear();
                    }
                } else {
                    std::cerr << "    ✗ Compilation failed: " << result.errorMessage << "\n";
                    // Don't fail the overall compilation - just skip this processor
                }
            }
            std::cout << "\n";

            // Clean up temp files at end of compilation (optional)
            // procCompiler.cleanup();
        }

        // Process annotations after semantic analysis
        // Always set processor registry (even if no user DLLs loaded)
        // This allows built-in processors to work without flags
        mainAnalyzer->getAnnotationProcessor().setProcessorRegistry(&processorRegistry);
        mainAnalyzer->getAnnotationProcessor().processAll();
        if (errorReporter.hasErrors()) {
            errorReporter.printErrors();
            return 1;
        }
        // Print any warnings from annotation processing
        if (errorReporter.hasWarnings()) {
            errorReporter.printErrors();  // printErrors prints both errors and warnings
        }

        // Merge template info and class registry from all imported modules
        for (const auto& [moduleName, analyzer] : analyzerMap) {
            for (const auto& [name, templateInfo] : analyzer->getTemplateClasses()) {
                mainAnalyzer->registerTemplateClass(name, templateInfo);
            }
            for (const auto& inst : analyzer->getTemplateInstantiations()) {
                mainAnalyzer->mergeTemplateInstantiation(inst);
            }
            // Merge class registry for cross-module type resolution
            mainAnalyzer->mergeClassRegistry(analyzer->getClassRegistry());
        }

        // Run comprehensive pre-codegen verification
        // This is the SINGLE GATE that ensures all invariants are satisfied
        // before any LLVM IR generation begins
        std::cout << "Verifying pre-codegen invariants...\n";
        try {
            XXML::Semantic::SemanticVerifier::assertPreCodegenInvariants(
                passResults, *mainModule->ast, *mainAnalyzer);
            std::cout << "  All pre-codegen invariants satisfied\n";
        } catch (const std::runtime_error& e) {
            std::cerr << "Compilation aborted: " << e.what() << "\n";
            return 1;
        }

        // Generate LLVM IR
        std::cout << "Generating LLVM IR...\n";
        auto* backend = compilationContext.getActiveBackend();
        auto* llvmBackend = dynamic_cast<XXML::Backends::LLVMBackend*>(backend);
        if (llvmBackend) {
            if (mainAnalyzer) {
                llvmBackend->setSemanticAnalyzer(mainAnalyzer.get());
            }

            // Pass imported modules to backend for code generation with their names
            // The names are needed for proper namespace handling in modules without explicit wrappers
            std::vector<std::pair<std::string, XXML::Parser::Program*>> importedModulesWithNames;
            for (const auto& moduleName : compilationOrder) {
                auto it = moduleMap.find(moduleName);
                if (it != moduleMap.end() && it->second != mainModule.get() && it->second->ast) {
                    importedModulesWithNames.push_back({moduleName, it->second->ast.get()});
                }
            }
            llvmBackend->setImportedModulesWithNames(importedModulesWithNames);

            // Set processor mode if compiling annotation processor to DLL
            if (processorMode) {
                // Find the annotation name from the AST
                std::string annotationName;
                for (const auto& decl : mainModule->ast->declarations) {
                    if (auto* annotDecl = dynamic_cast<XXML::Parser::AnnotationDecl*>(decl.get())) {
                        if (annotDecl->processor) {
                            annotationName = annotDecl->name;
                            break;
                        }
                    }
                }
                llvmBackend->setProcessorMode(true, annotationName);
            }

            // Set derive mode if compiling in-language derive to DLL
            if (deriveMode) {
                // Find the derive name from the AST
                std::string deriveName;
                for (const auto& decl : mainModule->ast->declarations) {
                    if (auto* deriveDecl = dynamic_cast<XXML::Parser::DeriveDecl*>(decl.get())) {
                        deriveName = deriveDecl->name;
                        break;
                    }
                }
                llvmBackend->setDeriveMode(true, deriveName);
            }
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
            // Compile to executable (or DLL in processor/derive mode)
            std::string executablePath = outputPath.string();
#ifdef _WIN32
            if (processorMode || deriveMode) {
                // Processor/Derive mode: create DLL
                if (outputPath.extension() != ".dll") {
                    executablePath = outputPath.stem().string() + ".dll";
                }
            } else {
                // Normal mode: create executable
                if (outputPath.extension() != ".exe") {
                    executablePath = outputPath.stem().string() + ".exe";
                }
            }
#endif

            // Print optimization settings
            if (optimizationLevel > 0 || !optimizationMode.empty()) {
                std::string optStr;
                if (!optimizationMode.empty()) {
                    optStr = "-O" + optimizationMode;
                } else {
                    optStr = "-O" + std::to_string(optimizationLevel);
                }
                std::cout << "Compiling to object file (" << optStr;
                if (includeDebugSymbols) {
                    std::cout << " -g";
                }
                std::cout << ")...\n";
            } else {
                std::cout << "Compiling to object file...\n";
            }

#ifdef _WIN32
            std::string objPath = outputPath.stem().string() + ".obj";
#else
            std::string objPath = outputPath.stem().string() + ".o";
#endif
            if (!llvmBackend->generateObjectFile(llvmIR, objPath, optimizationLevel, optimizationMode, includeDebugSymbols)) {
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
                // Relative to executable (most common for installed layout)
                exeDir + "/../lib/libXXMLLLVMRuntime.a",
                exeDir + "/lib/libXXMLLLVMRuntime.a",
                exeDir + "/../lib/xxml/libXXMLLLVMRuntime.a",
                // Build directory paths
                "build/lib/libXXMLLLVMRuntime.a",
                "build/release/lib/libXXMLLLVMRuntime.a",
                "build/debug/lib/libXXMLLLVMRuntime.a",
                "build_mingw/lib/libXXMLLLVMRuntime.a",
                // macOS PKG installer paths
                "/usr/local/lib/xxml/libXXMLLLVMRuntime.a",
                "/usr/local/share/xxml/lib/libXXMLLLVMRuntime.a",
                // Homebrew paths (Apple Silicon and Intel)
                "/opt/homebrew/lib/xxml/libXXMLLLVMRuntime.a",
                "/usr/local/Cellar/xxml/lib/libXXMLLLVMRuntime.a",
                // Linux system paths
                "/usr/lib/xxml/libXXMLLLVMRuntime.a",
                "/usr/share/xxml/lib/libXXMLLLVMRuntime.a"
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

            // Warn if runtime library not found
            if (runtimeLibPath.empty()) {
                std::cerr << "⚠ Warning: Runtime library (libXXMLLLVMRuntime.a) not found!\n";
                std::cerr << "  Searched in:\n";
                std::cerr << "    - " << exeDir << "/../lib/\n";
                std::cerr << "    - " << exeDir << "/lib/\n";
                std::cerr << "    - /usr/local/lib/xxml/\n";
                std::cerr << "  Linking will likely fail with undefined symbols.\n";
            } else {
                std::cout << "  Runtime library: " << runtimeLibPath << "\n";
            }

            XXML::Linker::LinkConfig linkConfig;
            linkConfig.objectFiles.push_back(objPath);
            if (!runtimeLibPath.empty()) linkConfig.libraries.push_back(runtimeLibPath);
            linkConfig.outputPath = executablePath;
            linkConfig.createConsoleApp = !(processorMode || deriveMode);  // Console app unless creating DLL
            linkConfig.createDLL = processorMode || deriveMode;            // Create DLL for processor/derive mode
            linkConfig.optimizationLevel = optimizationLevel;
            linkConfig.stripSymbols = !includeDebugSymbols;  // Strip symbols when debug info disabled

            auto linkResult = linker->link(linkConfig);
            if (!linkResult.success) {
                std::cerr << "✗ Linking failed: " << linkResult.error << "\n";
                return 1;
            }

            std::cout << "\n✓ Compilation successful!\n";
            if (processorMode) {
                std::cout << "  Processor DLL: " << executablePath << "\n";
            } else if (deriveMode) {
                std::cout << "  Derive DLL: " << executablePath << "\n";
            } else {
                std::cout << "  Executable: " << executablePath << "\n";
            }
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
