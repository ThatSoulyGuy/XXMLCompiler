#include "Backends/Codegen/ModuleCodegen/ModuleCodegen.h"
#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include <iostream>

namespace XXML::Backends::Codegen {

ModuleCodegen::ModuleCodegen(CodegenContext& ctx, DeclCodegen& declCodegen)
    : ctx_(ctx), declCodegen_(declCodegen) {
}

std::string ModuleCodegen::ownershipToString(Parser::OwnershipType ownership) {
    switch (ownership) {
        case Parser::OwnershipType::Owned: return "^";
        case Parser::OwnershipType::Reference: return "&";
        case Parser::OwnershipType::Copy: return "%";
        case Parser::OwnershipType::None: return "";
        default: return "";
    }
}

void ModuleCodegen::collectClassMetadata(Parser::ClassDecl* classDecl, const std::string& namespaceName) {
    if (!classDecl) return;

    ReflectionClassMetadata metadata;
    metadata.name = classDecl->name;
    metadata.namespaceName = namespaceName;
    if (!namespaceName.empty()) {
        metadata.fullName = namespaceName + "::" + classDecl->name;
    } else {
        metadata.fullName = classDecl->name;
    }
    metadata.isTemplate = !classDecl->templateParams.empty();
    for (const auto& param : classDecl->templateParams) {
        metadata.templateParams.push_back(param.name);
    }
    metadata.instanceSize = 0;  // Will be calculated later if needed
    metadata.astNode = classDecl;

    // Collect properties with ownership
    for (auto& section : classDecl->sections) {
        for (auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                metadata.properties.push_back({prop->name, prop->type->typeName});
                metadata.propertyOwnerships.push_back(ownershipToString(prop->type->ownership));
            }
        }
    }

    // Collect methods with parameters
    for (auto& section : classDecl->sections) {
        for (auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                metadata.methods.push_back({method->name, method->returnType ? method->returnType->typeName : "None"});
                metadata.methodReturnOwnerships.push_back(method->returnType ? ownershipToString(method->returnType->ownership) : "");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : method->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);
            } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Constructors are special methods
                metadata.methods.push_back({"Constructor", classDecl->name});
                metadata.methodReturnOwnerships.push_back("^");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : ctor->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);
            }
        }
    }

    // Store metadata in CodegenContext (don't overwrite if already exists)
    ctx_.addReflectionMetadata(metadata.fullName, metadata);
}

void ModuleCodegen::processNamespaceForMetadata(Parser::NamespaceDecl* ns, const std::string& parentNamespace) {
    if (!ns) return;

    std::string currentNs = parentNamespace.empty() ? ns->name : (parentNamespace + "::" + ns->name);

    for (auto& decl : ns->declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            collectClassMetadata(classDecl, currentNs);
        } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            processNamespaceForMetadata(nestedNs, currentNs);
        }
    }
}

void ModuleCodegen::collectReflectionMetadata(Parser::Program& program) {
    // Collect reflection metadata from a module WITHOUT generating any code.
    // This is used for runtime modules (Language::*, System::*, etc.) where we need
    // reflection info but the actual implementations are provided by the runtime library.

    // Process all declarations in the program
    for (auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            collectClassMetadata(classDecl, "");
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            processNamespaceForMetadata(ns, "");
        }
    }
}

void ModuleCodegen::processNamespaceForCodegen(Parser::NamespaceDecl* ns) {
    if (!ns) {
        return;
    }

    // Save and set namespace context
    std::string previousNamespace(ctx_.currentNamespace());
    if (ctx_.currentNamespace().empty()) {
        ctx_.setCurrentNamespace(ns->name);
    } else {
        ctx_.setCurrentNamespace(std::string(ctx_.currentNamespace()) + "::" + ns->name);
    }

    for (auto& decl : ns->declarations) {
        if (!decl) {
            continue;
        }

        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            // Process class using DeclCodegen
            ctx_.setCurrentClassName("");
            try {
                declCodegen_.generate(classDecl);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting class " << classDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* nestedNs = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            // Recursively process nested namespace
            processNamespaceForCodegen(nestedNs);
        }
        // Skip ImportDecl and EntrypointDecl
    }

    // Restore namespace context
    ctx_.setCurrentNamespace(previousNamespace);
}

void ModuleCodegen::generateImportedModuleCode(Parser::Program& program, const std::string& moduleName) {
    // Generate code for imported modules safely by processing classes directly.
    // This avoids issues with the full visitor pattern by:
    // 1. Skipping entrypoints (imported modules shouldn't have them)
    // 2. Properly managing state between class generations
    // 3. Not relying on the visitor pattern for top-level iteration
    // 4. Using moduleName as namespace for modules without explicit namespace wrappers

    // Save current state
    std::string savedNamespace(ctx_.currentNamespace());
    std::string savedClassName(ctx_.currentClassName());

    // Determine the default namespace for this module
    // For modules like "GLFW::GLFW" (subdir/file naming), use just the first component "GLFW"
    std::string defaultModuleNamespace = moduleName;
    size_t colonPos = moduleName.find("::");
    if (colonPos != std::string::npos) {
        defaultModuleNamespace = moduleName.substr(0, colonPos);
    }

    // Process all declarations in the program
    for (auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }

        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            // Process top-level class using DeclCodegen
            ctx_.setCurrentNamespace(defaultModuleNamespace);
            ctx_.setCurrentClassName("");
            try {
                declCodegen_.generate(classDecl);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting class " << classDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* ns = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            // Process namespace - explicit namespace in file, start fresh
            ctx_.setCurrentNamespace("");
            processNamespaceForCodegen(ns);
        } else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
            // Process top-level method using DeclCodegen
            ctx_.setCurrentNamespace(defaultModuleNamespace);
            ctx_.setCurrentClassName("");
            try {
                declCodegen_.generate(methodDecl);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting method " << methodDecl->name << ": " << e.what() << "\n";
            }
        } else if (auto* nativeStruct = dynamic_cast<Parser::NativeStructureDecl*>(decl.get())) {
            // Process NativeStructure using DeclCodegen
            ctx_.setCurrentNamespace(defaultModuleNamespace);
            ctx_.setCurrentClassName("");
            try {
                declCodegen_.generate(nativeStruct);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting NativeStructure " << nativeStruct->name << ": " << e.what() << "\n";
            }
        } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl.get())) {
            // Process Enumeration using DeclCodegen
            ctx_.setCurrentNamespace(defaultModuleNamespace);
            ctx_.setCurrentClassName("");
            try {
                declCodegen_.generate(enumDecl);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Exception visiting Enumeration " << enumDecl->name << ": " << e.what() << "\n";
            }
        }
        // Skip ImportDecl and EntrypointDecl
    }

    // Restore saved state
    ctx_.setCurrentNamespace(savedNamespace);
    ctx_.setCurrentClassName(savedClassName);
}

} // namespace XXML::Backends::Codegen
