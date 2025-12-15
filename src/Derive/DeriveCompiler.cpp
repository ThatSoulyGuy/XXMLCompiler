#include "Derive/DeriveCompiler.h"
#include "Utils/ProcessUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <chrono>

namespace XXML {
namespace Derive {

DeriveCompiler::DeriveCompiler(const std::string& compilerPath, const std::string& tempDir)
    : compilerPath_(compilerPath), tempDir_(tempDir) {
    if (tempDir_.empty()) {
        // Use system temp directory
        tempDir_ = std::filesystem::temp_directory_path().string();
    }
    // Seed random number generator
    srand(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
}

std::string DeriveCompiler::getTempFilePath(const std::string& baseName, const std::string& extension) {
    std::filesystem::path path = std::filesystem::path(tempDir_) / (baseName + "_" +
                       std::to_string(std::hash<std::string>{}(baseName + std::to_string(rand()))) +
                       extension);
    std::string pathStr = path.string();
    tempFiles_.push_back(pathStr);
    return pathStr;
}

void DeriveCompiler::cleanup() {
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

std::string DeriveCompiler::generateDeriveSource(const DeriveInfo& info) {
    std::ostringstream out;

    // Generate minimal XXML source with just the derive
    out << "// Auto-generated derive source for " << info.deriveName << "\n";
    out << "// This file is compiled to a DLL for derive processing\n\n";

    // Include Language::Core for basic types (always needed)
    out << "#import Language::Core;\n";
    // Include Language::Derives for DeriveContext
    out << "#import Language::Derives;\n";

    // Include all imports from the original source file
    for (const auto& import : info.imports) {
        // Skip Language::Core and Language::Derives since we already included them
        if (import != "Language::Core" && import != "Language::Derives") {
            out << "#import " << import << ";\n";
        }
    }
    out << "\n";

    // Include user-defined classes from the same file
    for (const auto& classDecl : info.userClasses) {
        out << serializeClass(classDecl);
    }

    // Generate derive definition
    out << serializeDerive(info.deriveDecl);
    out << "\n";

    // Generate a minimal entrypoint (required for compilation)
    out << "[ Entrypoint { Exit(0); } ]\n";

    return out.str();
}

std::string DeriveCompiler::serializeDerive(Parser::DeriveDecl* derive) {
    std::ostringstream out;
    out << "[ Derive <" << derive->name << ">\n";

    for (const auto& section : derive->sections) {
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

        out << "    [ " << modifier << " <>\n";

        // Generate declarations in this section
        for (const auto& decl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                out << serializeMethod(method, 8);
            }
        }

        out << "    ]\n";
    }

    out << "]\n";
    return out.str();
}

std::string DeriveCompiler::serializeClass(Parser::ClassDecl* classDecl) {
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

std::string DeriveCompiler::serializeMethod(Parser::MethodDecl* method, int indent) {
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
    out << "\n" << spaces << ") Do {\n";

    // Method body
    for (const auto& stmt : method->body) {
        out << serializeStatement(stmt.get(), indent + 4);
    }

    out << spaces << "}\n";
    return out.str();
}

std::string DeriveCompiler::serializeStatement(Parser::Statement* stmt, int indent) {
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
    else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        // For loop: For (init; condition; increment) -> { body }
        out << spaces << "For (Instantiate " << forStmt->iteratorType->typeName;
        switch (forStmt->iteratorType->ownership) {
            case Parser::OwnershipType::Owned: out << "^"; break;
            case Parser::OwnershipType::Reference: out << "&"; break;
            case Parser::OwnershipType::Copy: out << "%"; break;
            default: break;
        }
        out << " As <" << forStmt->iteratorName << "> = "
            << serializeExpression(forStmt->rangeStart.get()) << "; "
            << serializeExpression(forStmt->rangeEnd.get()) << ") -> {\n";
        for (const auto& s : forStmt->body) {
            out << serializeStatement(s.get(), indent + 4);
        }
        out << spaces << "}\n";
    }
    else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        out << spaces << "While (" << serializeExpression(whileStmt->condition.get()) << ") -> {\n";
        for (const auto& s : whileStmt->body) {
            out << serializeStatement(s.get(), indent + 4);
        }
        out << spaces << "}\n";
    }
    else if (auto* assignStmt = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        out << spaces << "Set " << serializeExpression(assignStmt->target.get()) << " = "
            << serializeExpression(assignStmt->value.get()) << ";\n";
    }
    else if (dynamic_cast<Parser::ContinueStmt*>(stmt)) {
        out << spaces << "Continue;\n";
    }
    else if (dynamic_cast<Parser::BreakStmt*>(stmt)) {
        out << spaces << "Break;\n";
    }
    else {
        // Generic fallback - emit a comment for unsupported statements
        out << spaces << "// [unsupported statement type]\n";
    }

    return out.str();
}

std::string DeriveCompiler::escapeString(const std::string& str) {
    std::ostringstream out;
    for (char c : str) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string DeriveCompiler::serializeExpression(Parser::Expression* expr) {
    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        return "String::Constructor(\"" + escapeString(strLit->value) + "\")";
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
                    out << "\"" << escapeString(strLit->value) << "\"";
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

DeriveCompiler::CompilationResult DeriveCompiler::compileDerive(
    const DeriveInfo& info, Common::ErrorReporter& errorReporter) {

    CompilationResult result;
    result.success = false;

    // Generate source
    std::string source = generateDeriveSource(info);

    // Write to temp file
    std::string sourceFile = getTempFilePath(info.deriveName + "_derive", ".XXML");
    std::string dllFile = getTempFilePath(info.deriveName + "_derive",
#ifdef _WIN32
        ".dll"
#elif __APPLE__
        ".dylib"
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
        std::cout << "    Generated derive source: " << sourceFile << "\n";

        // Normalize paths for command line (use native separators)
        std::string normalizedCompilerPath = std::filesystem::path(compilerPath_).make_preferred().string();
        std::string normalizedSourceFile = std::filesystem::path(sourceFile).make_preferred().string();
        std::string normalizedDllFile = std::filesystem::path(dllFile).make_preferred().string();

        // Compile using subprocess with --derive flag
        std::string command = "\"" + normalizedCompilerPath + "\" --derive \"" + normalizedSourceFile + "\" -o \"" + normalizedDllFile + "\"";

        // Debug: print command
        std::cout << "    Running: " << command << "\n";

        // Execute compiler and capture output
        std::string outputFile = std::filesystem::path(getTempFilePath("derive_output", ".txt")).make_preferred().string();

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

        // Check if DLL was created
        if (std::filesystem::exists(dllFile)) {
            result.success = true;
            result.dllPath = dllFile;
            return result;
        }

        // DLL was not created - report failure
        if (exitCode != 0) {
            result.errorMessage = "Derive compilation failed with exit code " + std::to_string(exitCode);
        } else {
            result.errorMessage = "Derive library was not created: " + dllFile;
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Exception during derive compilation: ") + e.what();
    }

    return result;
}

} // namespace Derive
} // namespace XXML
