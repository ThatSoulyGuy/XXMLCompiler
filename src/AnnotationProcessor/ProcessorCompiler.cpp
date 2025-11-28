#include "AnnotationProcessor/ProcessorCompiler.h"
#include "Utils/ProcessUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <chrono>

namespace XXML {
namespace AnnotationProcessor {

ProcessorCompiler::ProcessorCompiler(const std::string& compilerPath, const std::string& tempDir)
    : compilerPath_(compilerPath), tempDir_(tempDir) {
    if (tempDir_.empty()) {
        // Use system temp directory
        tempDir_ = std::filesystem::temp_directory_path().string();
    }
    // Seed random number generator
    srand(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
}

std::string ProcessorCompiler::getTempFilePath(const std::string& baseName, const std::string& extension) {
    std::filesystem::path path = std::filesystem::path(tempDir_) / (baseName + "_" +
                       std::to_string(std::hash<std::string>{}(baseName + std::to_string(rand()))) +
                       extension);
    std::string pathStr = path.string();
    tempFiles_.push_back(pathStr);
    return pathStr;
}

void ProcessorCompiler::cleanup() {
    for (const auto& file : tempFiles_) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
            }
        } catch (...) {
            // Ignore cleanup errors
        }
    }
    tempFiles_.clear();
}

std::string ProcessorCompiler::generateProcessorSource(const ProcessorInfo& info) {
    std::ostringstream out;

    // Generate minimal XXML source with just the annotation and processor
    out << "// Auto-generated processor source for @" << info.annotationName << "\n";
    out << "// This file is compiled to a DLL for annotation processing\n\n";

    // Include Language::Core for basic types (always needed)
    out << "#import Language::Core;\n";

    // Include all imports from the original source file
    // This allows processors to access user-defined classes and other modules
    for (const auto& import : info.imports) {
        // Skip Language::Core since we already included it
        if (import != "Language::Core") {
            out << "#import " << import << ";\n";
        }
    }
    out << "\n";

    // Include user-defined classes from the same file
    // This allows processors to reference classes defined alongside the annotation
    for (const auto& classDecl : info.userClasses) {
        out << serializeClass(classDecl);
    }

    // Generate annotation definition with processor
    out << "[ Annotation <" << info.annotationName << "> Allows (";

    // Serialize allowed targets
    bool first = true;
    for (const auto& target : info.annotDecl->allowedTargets) {
        if (!first) out << ", ";
        first = false;
        switch (target) {
            case Parser::AnnotationTarget::Classes:
                out << "AnnotationAllow::Classes";
                break;
            case Parser::AnnotationTarget::Methods:
                out << "AnnotationAllow::Methods";
                break;
            case Parser::AnnotationTarget::Properties:
                out << "AnnotationAllow::Properties";
                break;
            case Parser::AnnotationTarget::Variables:
                out << "AnnotationAllow::Variables";
                break;
        }
    }
    out << ")";

    if (info.annotDecl->retainAtRuntime) {
        out << " Retain";
    }
    out << "\n";

    // Generate Annotate declarations
    for (const auto& param : info.annotDecl->parameters) {
        out << "    Annotate (" << param->type->typeName;
        // Add ownership marker
        switch (param->type->ownership) {
            case Parser::OwnershipType::Owned:
                out << "^";
                break;
            case Parser::OwnershipType::Reference:
                out << "&";
                break;
            case Parser::OwnershipType::Copy:
                out << "%";
                break;
            default:
                break;
        }
        out << ")(" << param->name << ")";

        // Handle default value if present
        if (param->defaultValue) {
            out << " = " << serializeExpression(param->defaultValue.get());
        }
        out << ";\n";
    }

    // Generate processor block
    out << "\n" << serializeProcessor(info.processorDecl);
    out << "]\n\n";

    // Generate a minimal entrypoint (required for compilation)
    out << "[ Entrypoint { Exit(0); } ]\n";

    return out.str();
}

std::string ProcessorCompiler::serializeProcessor(Parser::ProcessorDecl* proc) {
    std::ostringstream out;
    out << "    [ Processor\n";

    for (const auto& section : proc->sections) {
        // Determine access modifier
        std::string modifier;
        switch (section->modifier) {
            case Parser::AccessModifier::Public:
                modifier = "Public";
                break;
            case Parser::AccessModifier::Private:
                modifier = "Private";
                break;
            case Parser::AccessModifier::Protected:
                modifier = "Protected";
                break;
        }

        out << "        [ " << modifier << " <>\n";

        // Generate declarations in this section
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                out << serializeMethod(method, 12);
            }
        }

        out << "        ]\n";
    }

    out << "    ]\n";
    return out.str();
}

std::string ProcessorCompiler::serializeClass(Parser::ClassDecl* classDecl) {
    std::ostringstream out;

    // Class header
    out << "[ Class <" << classDecl->name << ">";
    if (classDecl->isFinal) {
        out << " Final";
    }
    if (!classDecl->baseClass.empty()) {
        out << " Extends " << classDecl->baseClass;
    } else {
        out << " Extends None";
    }
    out << "\n\n";

    // Process each section
    for (const auto& section : classDecl->sections) {
        std::string modifier;
        switch (section->modifier) {
            case Parser::AccessModifier::Public:
                modifier = "Public";
                break;
            case Parser::AccessModifier::Private:
                modifier = "Private";
                break;
            case Parser::AccessModifier::Protected:
                modifier = "Protected";
                break;
        }

        out << "[ " << modifier << "<>\n";

        // Serialize declarations
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                out << serializeMethod(method, 4);
            }
            else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Serialize constructor
                out << "    Constructor Parameters (";
                bool first = true;
                for (const auto& param : ctor->parameters) {
                    if (!first) out << ", ";
                    first = false;
                    out << "Parameter <" << param->name << "> Types ";
                    if (param->type) {
                        out << param->type->typeName;
                        switch (param->type->ownership) {
                            case Parser::OwnershipType::Owned: out << "^"; break;
                            case Parser::OwnershipType::Reference: out << "&"; break;
                            case Parser::OwnershipType::Copy: out << "%"; break;
                            default: break;
                        }
                    }
                }
                out << ") -> {\n";
                for (const auto& stmt : ctor->body) {
                    out << serializeStatement(stmt.get(), 8);
                }
                out << "    }\n";
            }
            else if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                // Serialize property
                out << "    Property <" << prop->name << "> Types ";
                if (prop->type) {
                    out << prop->type->typeName;
                    switch (prop->type->ownership) {
                        case Parser::OwnershipType::Owned: out << "^"; break;
                        case Parser::OwnershipType::Reference: out << "&"; break;
                        case Parser::OwnershipType::Copy: out << "%"; break;
                        default: break;
                    }
                }
                out << ";\n";
            }
        }

        out << "]\n";
    }

    out << "\n]\n\n";
    return out.str();
}

std::string ProcessorCompiler::serializeMethod(Parser::MethodDecl* method, int indent) {
    std::ostringstream out;
    std::string spaces(indent, ' ');

    out << spaces << "Method <" << method->name << "> Returns ";

    // Return type
    if (method->returnType) {
        out << method->returnType->typeName;
        switch (method->returnType->ownership) {
            case Parser::OwnershipType::Owned:
                out << "^";
                break;
            case Parser::OwnershipType::Reference:
                out << "&";
                break;
            case Parser::OwnershipType::Copy:
                out << "%";
                break;
            default:
                break;
        }
    } else {
        out << "None";
    }

    out << " Parameters (";

    // Parameters
    bool first = true;
    for (const auto& param : method->parameters) {
        if (!first) out << ",\n" << spaces << "    ";
        first = false;
        out << "Parameter <" << param->name << "> Types ";
        if (param->type) {
            out << param->type->typeName;
            switch (param->type->ownership) {
                case Parser::OwnershipType::Owned:
                    out << "^";
                    break;
                case Parser::OwnershipType::Reference:
                    out << "&";
                    break;
                case Parser::OwnershipType::Copy:
                    out << "%";
                    break;
                default:
                    break;
            }
        }
    }
    out << "\n" << spaces << ") -> {\n";

    // Method body
    for (const auto& stmt : method->body) {
        out << serializeStatement(stmt.get(), indent + 4);
    }

    out << spaces << "}\n";
    return out.str();
}

std::string ProcessorCompiler::serializeStatement(Parser::Statement* stmt, int indent) {
    std::ostringstream out;
    std::string spaces(indent, ' ');

    if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        out << spaces << "Run " << serializeExpression(runStmt->expression.get()) << ";\n";
    }
    else if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        out << spaces << "Instantiate " << instStmt->type->typeName;
        switch (instStmt->type->ownership) {
            case Parser::OwnershipType::Owned:
                out << "^";
                break;
            case Parser::OwnershipType::Reference:
                out << "&";
                break;
            case Parser::OwnershipType::Copy:
                out << "%";
                break;
            default:
                break;
        }
        out << " As <" << instStmt->variableName << ">";
        if (instStmt->initializer) {
            out << " = " << serializeExpression(instStmt->initializer.get());
        }
        out << ";\n";
    }
    else if (auto* retStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        out << spaces << "Return";
        if (retStmt->value) {
            out << " " << serializeExpression(retStmt->value.get());
        }
        out << ";\n";
    }
    else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        out << spaces << "If (" << serializeExpression(ifStmt->condition.get()) << ") -> {\n";
        for (const auto& s : ifStmt->thenBranch) {
            out << serializeStatement(s.get(), indent + 4);
        }
        out << spaces << "}";
        if (!ifStmt->elseBranch.empty()) {
            out << " Else -> {\n";
            for (const auto& s : ifStmt->elseBranch) {
                out << serializeStatement(s.get(), indent + 4);
            }
            out << spaces << "}";
        }
        out << "\n";
    }
    else if (auto* assignStmt = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        out << spaces << "Set " << serializeExpression(assignStmt->target.get()) << " = "
            << serializeExpression(assignStmt->value.get()) << ";\n";
    }
    else {
        // Generic fallback - emit a comment for unsupported statements
        out << spaces << "// [unsupported statement type]\n";
    }

    return out.str();
}

std::string ProcessorCompiler::serializeExpression(Parser::Expression* expr) {
    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        return "String::Constructor(\"" + strLit->value + "\")";
    }
    else if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        return std::to_string(intLit->value) + "i";
    }
    else if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        return boolLit->value ? "true" : "false";
    }
    else if (auto* floatLit = dynamic_cast<Parser::FloatLiteralExpr*>(expr)) {
        return std::to_string(floatLit->value) + "f";
    }
    else if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) {
        return std::to_string(doubleLit->value) + "d";
    }
    else if (auto* id = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        return id->name;
    }
    else if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        std::string member = memberAccess->member;
        // Check if member starts with "::" (static method call like String::Constructor)
        if (member.find("::") == 0) {
            // Static method call - member already includes "::"
            return serializeExpression(memberAccess->object.get()) + member;
        } else if (member.find(".") == 0) {
            // Instance method/property access - member already includes "."
            return serializeExpression(memberAccess->object.get()) + member;
        } else {
            // Plain member name - add "."
            return serializeExpression(memberAccess->object.get()) + "." + member;
        }
    }
    else if (auto* call = dynamic_cast<Parser::CallExpr*>(expr)) {
        std::ostringstream out;
        std::string calleeStr = serializeExpression(call->callee.get());
        out << calleeStr << "(";

        // Check if this is String::Constructor - if so, don't double-wrap string literals
        bool isStringConstructor = (calleeStr == "String::Constructor");

        bool first = true;
        for (const auto& arg : call->arguments) {
            if (!first) out << ", ";
            first = false;

            // For String::Constructor with a StringLiteralExpr arg, output raw string literal
            if (isStringConstructor) {
                if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.get())) {
                    out << "\"" << strLit->value << "\"";
                    continue;
                }
            }
            out << serializeExpression(arg.get());
        }
        out << ")";
        return out.str();
    }
    else if (auto* binary = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return serializeExpression(binary->left.get()) + " " + binary->op + " " +
               serializeExpression(binary->right.get());
    }
    else if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(expr)) {
        return "this";
    }

    return "/* unknown expression */";
}

ProcessorCompiler::CompilationResult ProcessorCompiler::compileProcessor(
    const ProcessorInfo& info, Common::ErrorReporter& errorReporter) {

    CompilationResult result;
    result.success = false;

    // Generate source
    std::string source = generateProcessorSource(info);

    // Write to temp file
    std::string sourceFile = getTempFilePath(info.annotationName + "_processor", ".XXML");
    std::string dllFile = getTempFilePath(info.annotationName + "_processor",
#ifdef _WIN32
        ".dll"
#else
        ".so"
#endif
    );

    try {
        // Write source file
        std::ofstream outFile(sourceFile);
        if (!outFile.is_open()) {
            result.errorMessage = "Failed to create temp source file: " + sourceFile;
            return result;
        }
        outFile << source;
        outFile.close();

        // Debug: print generated source location
        std::cout << "    Generated source: " << sourceFile << "\n";

        // Normalize paths for command line (use native separators)
        std::string normalizedCompilerPath = std::filesystem::path(compilerPath_).make_preferred().string();
        std::string normalizedSourceFile = std::filesystem::path(sourceFile).make_preferred().string();
        std::string normalizedDllFile = std::filesystem::path(dllFile).make_preferred().string();

        // Compile using subprocess
        std::string command = "\"" + normalizedCompilerPath + "\" --processor \"" + normalizedSourceFile + "\" -o \"" + normalizedDllFile + "\"";

        // Debug: print command
        std::cout << "    Running: " << command << "\n";

        // Execute compiler and capture output
        std::string outputFile = std::filesystem::path(getTempFilePath("proc_output", ".txt")).make_preferred().string();

#ifdef _WIN32
        // Use cmd /c with proper output redirection
        std::string fullCommand = "cmd /c \"" + command + " > \"" + outputFile + "\" 2>&1\"";
#else
        std::string fullCommand = command + " > \"" + outputFile + "\" 2>&1";
#endif

        int exitCode = std::system(fullCommand.c_str());

        // Read and display output
        std::ifstream outputStream(outputFile);
        if (outputStream.is_open()) {
            std::string line;
            while (std::getline(outputStream, line)) {
                std::cout << "      " << line << "\n";
            }
            outputStream.close();
        }

        // On Windows, check if DLL was created even if exitCode is non-zero
        // (system() may return non-zero even on success due to cmd.exe behavior)
        if (std::filesystem::exists(dllFile)) {
            result.success = true;
            result.dllPath = dllFile;
            return result;
        }

        // DLL was not created - report failure
        if (exitCode != 0) {
            result.errorMessage = "Processor compilation failed with exit code " + std::to_string(exitCode);
        } else {
            result.errorMessage = "Processor DLL was not created: " + dllFile;
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Exception during processor compilation: ") + e.what();
    }

    return result;
}

} // namespace AnnotationProcessor
} // namespace XXML
