#include "../../include/CodeGen/CodeGenerator.h"
#include "../../include/Semantic/SemanticAnalyzer.h"
#include <algorithm>

namespace XXML {
namespace CodeGen {

CodeGenerator::CodeGenerator(Common::ErrorReporter& reporter)
    : indentLevel(0), errorReporter(reporter), semanticAnalyzer(nullptr), inClassDefinition(false),
      currentNamespace(""), generatingDeclarationsOnly(false), generatingImplementationsOnly(false) {}

void CodeGenerator::setSemanticAnalyzer(Semantic::SemanticAnalyzer* analyzer) {
    semanticAnalyzer = analyzer;
}

void CodeGenerator::indent() {
    for (int i = 0; i < indentLevel; ++i) {
        output << "    ";
    }
}

void CodeGenerator::writeLine(const std::string& line) {
    indent();
    output << line << "\n";
}

void CodeGenerator::write(const std::string& text) {
    output << text;
}

bool CodeGenerator::isPrimitiveType(const std::string& typeName) {
    return typeName == "Integer" || typeName == "Bool" || typeName.find("NativeType<") == 0;
}

bool CodeGenerator::isBuiltinType(const std::string& typeName) {
    return typeName == "Integer" || typeName == "Bool" || typeName == "String" ||
           typeName == "Float" || typeName == "Double" || typeName == "StringArray" ||
           typeName.find("NativeType<") == 0;
}

bool CodeGenerator::isSmartPointerType(const std::string& typeName, Parser::OwnershipType ownership) {
    // All types with Owned ownership (T^) generate Owned<T> which has smart pointer semantics
    return ownership == Parser::OwnershipType::Owned && typeName != "None";
}

std::string CodeGenerator::getOwnershipType(Parser::OwnershipType ownership, const std::string& typeName) {
    std::string cppType = convertType(typeName);

    // Special case: None/void
    if (typeName == "None") {
        return "void";
    }

    switch (ownership) {
        case Parser::OwnershipType::None:
            // Bare type (only valid in templates or for primitives in specific contexts)
            return cppType;

        case Parser::OwnershipType::Owned:
            // T^ - Owned value, wrapped in Owned<T> for move tracking
            return "Language::Runtime::Owned<" + cppType + ">";

        case Parser::OwnershipType::Reference:
            // T& - Reference/borrow
            return cppType + "&";

        case Parser::OwnershipType::Copy:
            // T% - Explicit copy (only in parameters/returns)
            return cppType;

        default:
            return cppType;
    }
}

std::string CodeGenerator::getParameterType(Parser::OwnershipType ownership, const std::string& typeName) {
    std::string cppType = convertType(typeName);

    switch (ownership) {
        case Parser::OwnershipType::None:
            // Bare type in parameter (shouldn't typically happen, but treat as value)
            return cppType;

        case Parser::OwnershipType::Owned:
            // T^ - Takes ownership (move parameter)
            return "Language::Runtime::Owned<" + cppType + ">";

        case Parser::OwnershipType::Reference:
            // T& - Borrows (reference parameter)
            return cppType + "&";

        case Parser::OwnershipType::Copy:
            // T% - Explicit copy (pass by value)
            return cppType;

        default:
            return cppType;
    }
}

std::string CodeGenerator::convertType(const std::string& xxmlType) {
    // Convert XXML types to C++ types
    // NOTE: String, Integer, Bool are now actual XXML classes defined in runtime

    // Check for template instantiation syntax: TemplateName<Arg1, Arg2>
    size_t templateStart = xxmlType.find('<');
    if (templateStart != std::string::npos && xxmlType.back() == '>') {
        // This might be a template instantiation or NativeType
        if (xxmlType.find("NativeType<") == 0) {
            // Fall through to NativeType handling below
        } else {
            // It's a template instantiation - parse and mangle it
            std::string templateName = xxmlType.substr(0, templateStart);
            std::string argsStr = xxmlType.substr(templateStart + 1, xxmlType.length() - templateStart - 2);

            // Parse template arguments (simple comma-separated parsing)
            std::vector<std::string> args;
            std::string currentArg;
            int angleDepth = 0;

            for (char c : argsStr) {
                if (c == '<') {
                    angleDepth++;
                    currentArg += c;
                } else if (c == '>') {
                    angleDepth--;
                    currentArg += c;
                } else if (c == ',' && angleDepth == 0) {
                    // Trim whitespace
                    size_t start = currentArg.find_first_not_of(" \t");
                    size_t end = currentArg.find_last_not_of(" \t");
                    if (start != std::string::npos && end != std::string::npos) {
                        args.push_back(currentArg.substr(start, end - start + 1));
                    }
                    currentArg.clear();
                } else {
                    currentArg += c;
                }
            }

            // Add last argument
            if (!currentArg.empty()) {
                size_t start = currentArg.find_first_not_of(" \t");
                size_t end = currentArg.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    args.push_back(currentArg.substr(start, end - start + 1));
                }
            }

            // Return mangled name
            return mangleTemplateName(templateName, args);
        }
    }

    if (xxmlType == "Integer") {
        return "Language::Core::Integer";
    } else if (xxmlType == "String") {
        return "Language::Core::String";
    } else if (xxmlType == "Bool") {
        return "Language::Core::Bool";
    } else if (xxmlType == "Float") {
        return "Language::Core::Float";
    } else if (xxmlType == "Double") {
        return "Language::Core::Double";
    } else if (xxmlType == "None") {
        return "void";
    } else if (xxmlType.find("NativeType<") == 0) {
        // Extract the native type from NativeType<typename>
        size_t start = xxmlType.find('<') + 1;
        size_t end = xxmlType.find('>');
        if (start != std::string::npos && end != std::string::npos) {
            std::string nativeType = xxmlType.substr(start, end - start);

            // Map XXML native types to C++ types
            if (nativeType == "ptr") {
                return "void*";
            } else if (nativeType == "\"string_ptr\"" || nativeType == "string_ptr") {
                return "void*";  // Pointer to std::string
            } else if (nativeType == "\"cstr\"" || nativeType == "cstr") {
                return "const void*";  // C string pointer (const char*)
            } else if (nativeType == "int64" || nativeType == "\"int64\"") {
                return "int64_t";
            } else if (nativeType == "int32") {
                return "int32_t";
            } else if (nativeType == "int16") {
                return "int16_t";
            } else if (nativeType == "int8") {
                return "int8_t";
            } else if (nativeType == "uint64") {
                return "uint64_t";
            } else if (nativeType == "uint32") {
                return "uint32_t";
            } else if (nativeType == "uint16") {
                return "uint16_t";
            } else if (nativeType == "uint8") {
                return "uint8_t";
            } else if (nativeType == "float") {
                return "float";
            } else if (nativeType == "double") {
                return "double";
            } else if (nativeType == "bool") {
                return "bool";
            } else if (nativeType == "char") {
                return "char";
            } else {
                return "void*"; // Default to void* for unknown types
            }
        }
        return "void*";
    } else {
        // For user-defined types, use qualified names
        std::string cppType = xxmlType;
        // Replace :: with _ for C++ compatibility if needed
        // For now, keep :: as C++ also uses it
        return cppType;
    }
}

std::string CodeGenerator::sanitizeIdentifier(const std::string& name) {
    // Remove angle brackets from identifiers if present
    std::string sanitized = name;
    sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '<'), sanitized.end());
    sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '>'), sanitized.end());

    // Handle C++ keywords that conflict with method names
    if (sanitized == "and") return "and_";
    if (sanitized == "or") return "or_";
    if (sanitized == "not") return "not_";
    if (sanitized == "xor") return "xor_";

    return sanitized;
}

std::string CodeGenerator::generate(Parser::Program& program, bool includeHeaders) {
    output.str("");
    output.clear();

    // Generate header only if requested
    if (includeHeaders) {
        writeLine("// Generated by XXML Compiler");
        writeLine("// Do not edit this file manually");
        writeLine("");
        writeLine("#include <iostream>");
        writeLine("#include <string>");
        writeLine("#include <memory>");
        writeLine("#include <cstdint>");
        writeLine("");
    }

    // Generate template instantiations first
    generateTemplateInstantiations();

    // Then generate user code
    program.accept(*this);

    return output.str();
}

std::string CodeGenerator::getOutput() const {
    return output.str();
}

// Visitor implementations
void CodeGenerator::visit(Parser::Program& node) {
    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }
}

void CodeGenerator::visit(Parser::ImportDecl& node) {
    // For imports, we'll assume the runtime library files will be included
    writeLine("// Import: " + node.modulePath);
}

void CodeGenerator::visit(Parser::NamespaceDecl& node) {
    std::string previousNamespace = currentNamespace;
    if (!currentNamespace.empty()) {
        currentNamespace += "::";
    }
    currentNamespace += node.name;

    writeLine("namespace " + sanitizeIdentifier(node.name) + " {");
    writeLine("");
    indentLevel++;

    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }

    indentLevel--;
    writeLine("} // namespace " + sanitizeIdentifier(node.name));
    writeLine("");

    currentNamespace = previousNamespace;
}

void CodeGenerator::visit(Parser::ClassDecl& node) {
    // Skip template class definitions - they will be generated through instantiations
    if (!node.templateParams.empty()) {
        return;
    }

    currentClassName = node.name;

    if (generatingImplementationsOnly) {
        // Just process members without class wrapper
        inClassDefinition = false;  // Mark as out-of-class for qualified names

        for (auto& section : node.sections) {
            section->accept(*this);
        }

        return;
    }

    inClassDefinition = true;

    std::string className = sanitizeIdentifier(node.name);

    indent();
    write("class " + className);

    if (!node.baseClass.empty() && node.baseClass != "None") {
        write(" : public " + convertType(node.baseClass));
    }

    write(" {");
    output << "\n";

    indentLevel++;

    // Process access sections
    for (auto& section : node.sections) {
        section->accept(*this);
    }

    indentLevel--;
    writeLine("};");
    writeLine("");

    inClassDefinition = false;
}

void CodeGenerator::visit(Parser::AccessSection& node) {
    // Only write access modifiers when inside a class definition
    if (!generatingImplementationsOnly) {
        switch (node.modifier) {
            case Parser::AccessModifier::Public:
                writeLine("public:");
                break;
            case Parser::AccessModifier::Private:
                writeLine("private:");
                break;
            case Parser::AccessModifier::Protected:
                writeLine("protected:");
                break;
        }
        indentLevel++;
    }

    for (auto& decl : node.declarations) {
        decl->accept(*this);
    }

    if (!generatingImplementationsOnly) {
        indentLevel--;
    }
}

void CodeGenerator::visit(Parser::PropertyDecl& node) {
    // Skip properties when generating implementations only
    if (generatingImplementationsOnly) {
        return;
    }

    std::string propertyName = sanitizeIdentifier(node.name);
    std::string type = getOwnershipType(node.type->ownership, node.type->typeName);

    writeLine(type + " " + propertyName + ";");
}

void CodeGenerator::visit(Parser::ConstructorDecl& node) {
    // Skip constructors when generating implementations only
    if (generatingImplementationsOnly) {
        return;
    }

    if (node.isDefault) {
        writeLine(currentClassName + "() = default;");
    } else {
        // Custom constructor
        indent();
        write(currentClassName + "(");

        // Parameters
        for (size_t i = 0; i < node.parameters.size(); ++i) {
            if (i > 0) write(", ");
            auto& param = node.parameters[i];
            write(getOwnershipType(param->type->ownership, param->type->typeName) +
                  " " + sanitizeIdentifier(param->name));
        }

        write(") {");
        output << "\n";

        indentLevel++;
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
        indentLevel--;

        writeLine("}");
    }
}

void CodeGenerator::visit(Parser::MethodDecl& node) {
    std::string methodName = sanitizeIdentifier(node.name);
    std::string returnType = getOwnershipType(node.returnType->ownership, node.returnType->typeName);

    indent();

    // Check if this is a Constructor method - generate as actual C++ constructor
    if (methodName == "Constructor" && !currentClassName.empty()) {
        // Skip constructors when generating implementations only (can't define outside class)
        if (generatingImplementationsOnly) {
            return;
        }

        // Check if this is a primitive wrapper class (Integer, Bool, Float, Double)
        bool isPrimitiveWrapper = (currentClassName == "Integer" || currentClassName == "Bool" ||
                                   currentClassName == "Float" || currentClassName == "Double");

        // Generate as C++ constructor
        write(currentClassName + "(");

        // Parameters
        for (size_t i = 0; i < node.parameters.size(); ++i) {
            if (i > 0) write(", ");
            auto& param = node.parameters[i];
            std::string paramType = getParameterType(param->type->ownership, param->type->typeName);
            // String constructor should use const void* for string literals
            if (currentClassName == "String" && paramType == "void*") {
                paramType = "const void*";
            }
            write(paramType + " " + sanitizeIdentifier(param->name));
        }

        write(")");

        // Track parameters as smart pointers and ownership type
        for (auto& param : node.parameters) {
            bool isSmartPtr = isSmartPointerType(param->type->typeName, param->type->ownership);
            variableIsSmartPointer[param->name] = isSmartPtr;
            variableOwnership[param->name] = param->type->ownership;
        }

        // For primitive wrappers with one parameter, use initialization list
        if (isPrimitiveWrapper && node.parameters.size() == 1) {
            std::string paramName = sanitizeIdentifier(node.parameters[0]->name);
            write(" : value(" + paramName + ") {}");
            output << "\n";
        } else {
            write(" {");
            output << "\n";

            indentLevel++;
            // Process body statements, but skip final "Return this;" statement and memcpy calls
            for (auto& stmt : node.body) {
                // Check if this is a return statement
                if (auto* returnStmt = dynamic_cast<Parser::ReturnStmt*>(stmt.get())) {
                    // Check if it's returning 'this' - if so, skip it
                    if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(returnStmt->value.get())) {
                        continue;  // Skip "Return this;" in constructors
                    }
                }
                // Skip memcpy calls in primitive wrapper constructors
                if (isPrimitiveWrapper) {
                    if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt.get())) {
                        // Skip this statement (it's likely a memcpy call)
                        continue;
                    }
                }
                stmt->accept(*this);
            }
            indentLevel--;

            writeLine("}");
        }

        // Also generate a static factory method for calling as ClassName::Constructor(args)
        indent();
        write("static " + currentClassName + " Constructor(");

        // Parameters - same as constructor
        for (size_t i = 0; i < node.parameters.size(); ++i) {
            if (i > 0) write(", ");
            auto& param = node.parameters[i];
            std::string paramType = getParameterType(param->type->ownership, param->type->typeName);
            if (currentClassName == "String" && paramType == "void*") {
                paramType = "const void*";
            }
            write(paramType + " " + sanitizeIdentifier(param->name));
        }

        write(") { return " + currentClassName + "(");
        // Pass parameters to constructor
        for (size_t i = 0; i < node.parameters.size(); ++i) {
            if (i > 0) write(", ");
            write(sanitizeIdentifier(node.parameters[i]->name));
        }
        output << "); }\n";
    } else {
        // Regular method
        // Console methods should be static since they're called as System::Console::methodName()
        bool isConsoleMethod = (currentClassName == "Console");

        // When generating implementations only, skip declarations and generate qualified names
        if (generatingImplementationsOnly) {
            // Skip if this is a Console method (those are inline)
            if (isConsoleMethod) {
                return;
            }

            // Generate method signature with qualified class name
            std::string qualifiedMethodName = currentNamespace + "::" + currentClassName + "::" + methodName;
            write(returnType + " " + qualifiedMethodName + "(");
        } else if (isConsoleMethod) {
            write("static ");
            // Use fully qualified type names for Console methods
            std::string qualifiedReturnType = returnType;
            if (returnType == "String") qualifiedReturnType = "Language::Core::String";
            else if (returnType == "Integer") qualifiedReturnType = "Language::Core::Integer";
            else if (returnType == "Bool") qualifiedReturnType = "Language::Core::Bool";
            write(qualifiedReturnType + " " + methodName + "(");
        } else {
            write(returnType + " " + methodName + "(");
        }

        // Parameters - use getParameterType for proper parameter handling
        for (size_t i = 0; i < node.parameters.size(); ++i) {
            if (i > 0) write(", ");
            auto& param = node.parameters[i];
            std::string paramType = getParameterType(param->type->ownership, param->type->typeName);
            // Qualify parameter types for Console methods
            if (isConsoleMethod && !generatingImplementationsOnly) {
                if (paramType == "String") paramType = "Language::Core::String";
                else if (paramType == "Integer") paramType = "Language::Core::Integer";
                else if (paramType == "Bool") paramType = "Language::Core::Bool";
            }
            write(paramType + " " + sanitizeIdentifier(param->name));
        }

        // If generating declarations only (and not Console which we keep inline), end with semicolon
        bool shouldGenerateInline = isConsoleMethod || !generatingDeclarationsOnly || generatingImplementationsOnly;

        if (!shouldGenerateInline) {
            write(");");
            output << "\n";
            return;  // Don't generate body
        }

        write(") {");
        output << "\n";

        // Track parameters as smart pointers and ownership type
        for (auto& param : node.parameters) {
            bool isSmartPtr = isSmartPointerType(param->type->typeName, param->type->ownership);
            variableIsSmartPointer[param->name] = isSmartPtr;
            variableOwnership[param->name] = param->type->ownership;
        }

        indentLevel++;

        // Generate actual implementations for Console methods
        if (isConsoleMethod) {
            if (methodName == "print") {
                writeLine("std::cout << (const char*)message.toCString();");
            } else if (methodName == "printLine") {
                writeLine("std::cout << (const char*)message.toCString() << std::endl;");
            } else if (methodName == "printError") {
                writeLine("std::cerr << (const char*)message.toCString() << std::endl;");
            } else if (methodName == "clear") {
                writeLine("#ifdef _WIN32");
                writeLine("    system(\"cls\");");
                writeLine("#else");
                writeLine("    system(\"clear\");");
                writeLine("#endif");
            } else if (methodName == "readLine") {
                writeLine("return String::Constructor();");  // Intrinsic - actual implementation in main.cpp
            } else if (methodName == "readChar") {
                writeLine("return String::Constructor();");  // Intrinsic
            } else if (methodName == "readInt") {
                writeLine("return Integer::Constructor(0);");  // Intrinsic
            } else if (methodName == "readFloat") {
                writeLine("return Float::Constructor(0);");  // Intrinsic
            } else if (methodName == "readDouble") {
                writeLine("return Double::Constructor(0);");  // Intrinsic
            } else if (methodName == "readBool") {
                writeLine("return Bool::Constructor(false);");  // Intrinsic
            } else if (methodName == "getTime") {
                writeLine("return Language::Core::Integer(0);");
            } else if (methodName == "getTimeMillis") {
                writeLine("return Language::Core::Integer(0);");
            } else if (methodName == "getEnv") {
                writeLine("return Language::Core::String();");
            } else if (methodName == "setEnv") {
                writeLine("return Language::Core::Bool(false);");
            } else {
                // For other Console methods, use the body from XXML
                for (auto& stmt : node.body) {
                    stmt->accept(*this);
                }
            }
        } else {
            // For non-Console methods, use the body from XXML
            for (auto& stmt : node.body) {
                stmt->accept(*this);
            }
        }

        indentLevel--;

        writeLine("}");
    }
}

void CodeGenerator::visit(Parser::ParameterDecl& node) {
    // Parameters are handled by their parent (method/constructor)
}

void CodeGenerator::visit(Parser::EntrypointDecl& node) {
    writeLine("int main() {");
    indentLevel++;

    // Add using directive for standard library
    writeLine("using namespace Language::Core;");
    writeLine("");

    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    indentLevel--;
    writeLine("}");
    writeLine("");
}

// Statement visitors
void CodeGenerator::visit(Parser::InstantiateStmt& node) {
    indent();

    std::string varName = sanitizeIdentifier(node.variableName);

    // Reconstruct full type name with template arguments if present
    std::string fullTypeName = node.type->typeName;
    if (!node.type->templateArgs.empty()) {
        fullTypeName += "<";
        for (size_t i = 0; i < node.type->templateArgs.size(); ++i) {
            if (i > 0) fullTypeName += ", ";
            const auto& arg = node.type->templateArgs[i];
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                fullTypeName += arg.typeArg;
            } else {
                // For value arguments, convert to string
                // Note: This is simplified - full implementation would evaluate the expression
                fullTypeName += "/* value arg */";
            }
        }
        fullTypeName += ">";
    }

    std::string type = getOwnershipType(node.type->ownership, fullTypeName);

    // Track if this variable is a smart pointer for later member access
    bool isSmartPtr = isSmartPointerType(fullTypeName, node.type->ownership);
    variableIsSmartPointer[node.variableName] = isSmartPtr;

    // Track ownership type for this variable (for copy vs reference validation)
    variableOwnership[node.variableName] = node.type->ownership;

    // Special handling for Owned<T> initialization
    // When initializing Owned<T> from a member access or identifier that might be Owned<T>,
    // we need to call .get() to extract the underlying value
    bool needsGet = false;
    if (node.type->ownership == Parser::OwnershipType::Owned) {
        // Check if initializer is a member access or identifier (likely accessing an Owned property)
        if (dynamic_cast<Parser::MemberAccessExpr*>(node.initializer.get()) ||
            dynamic_cast<Parser::IdentifierExpr*>(node.initializer.get())) {
            needsGet = true;
        }
    }

    if (needsGet) {
        // Use constructor syntax with .get() call
        write(type + " " + varName + "(");
        node.initializer->accept(*this);
        write(".get())");
    } else {
        // Regular assignment
        write(type + " " + varName + " = ");
        node.initializer->accept(*this);
    }

    write(";");
    output << "\n";
}

void CodeGenerator::visit(Parser::AssignmentStmt& node) {
    indent();

    std::string varName = sanitizeIdentifier(node.variableName);

    // Check ownership type of the variable (for parameters)
    // - Reference (&): Assignment affects the referenced object (intended behavior)
    // - Copy (%): Assignment only affects the local copy (C++ pass-by-value semantics)
    // - Owned (^): Assignment moves ownership
    auto ownershipIt = variableOwnership.find(node.variableName);
    if (ownershipIt != variableOwnership.end()) {
        Parser::OwnershipType ownership = ownershipIt->second;
        // Copy parameters: assignments only affect local copy, not the original
        // Reference parameters: assignments affect the original object
        // Owned parameters: use move semantics
        // This is handled automatically by the C++ type system via getParameterType()
    }

    // Check if this variable is a smart pointer (Owned<T>)
    bool isSmartPtr = variableIsSmartPointer[node.variableName];

    if (isSmartPtr) {
        // For Owned<T>, we need to use the assignment operator or extract/construct
        // Generate: varName = Owned<T>(value.get())
        write(varName + " = ");

        // Check if value is a member access or identifier that might be Owned<T>
        if (dynamic_cast<Parser::MemberAccessExpr*>(node.value.get()) ||
            dynamic_cast<Parser::IdentifierExpr*>(node.value.get())) {
            // Wrap in constructor with .get() call
            write("std::move(");
            node.value->accept(*this);
            write(")");
        } else {
            // Regular expression
            node.value->accept(*this);
        }
    } else {
        // Regular assignment
        write(varName + " = ");
        node.value->accept(*this);
    }

    write(";");
    output << "\n";
}

void CodeGenerator::visit(Parser::RunStmt& node) {
    indent();
    node.expression->accept(*this);
    write(";");
    output << "\n";
}

void CodeGenerator::visit(Parser::ForStmt& node) {
    indent();
    write("for (");

    std::string iteratorType = convertType(node.iteratorType->typeName);
    std::string iteratorName = sanitizeIdentifier(node.iteratorName);

    // Use the proper XXML type for the iterator
    write(iteratorType + " " + iteratorName + " = ");
    node.rangeStart->accept(*this);
    write("; " + iteratorName + " < ");
    node.rangeEnd->accept(*this);
    write("; " + iteratorName + "++) {");
    output << "\n";

    indentLevel++;
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    indentLevel--;

    writeLine("}");
}

void CodeGenerator::visit(Parser::ExitStmt& node) {
    indent();
    write("return ");
    node.exitCode->accept(*this);
    write(";");
    output << "\n";
}

void CodeGenerator::visit(Parser::ReturnStmt& node) {
    indent();
    write("return");

    if (node.value) {
        write(" ");

        // Check if we're returning 'this' - if so, move from it for value returns
        if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(node.value.get())) {
            write("std::move(*this)");  // Move from this to transfer ownership
        } else if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.value.get())) {
            // Check if this is an owned variable that needs to be moved
            auto ownershipIt = variableOwnership.find(identExpr->name);
            if (ownershipIt != variableOwnership.end() &&
                ownershipIt->second == Parser::OwnershipType::Owned) {
                // Return owned value with std::move
                write("std::move(");
                node.value->accept(*this);
                write(")");
            } else {
                node.value->accept(*this);
            }
        } else {
            node.value->accept(*this);
        }
    }

    write(";");
    output << "\n";
}

void CodeGenerator::visit(Parser::IfStmt& node) {
    indent();
    write("if (");
    node.condition->accept(*this);
    write(") {");
    output << "\n";

    indentLevel++;
    for (auto& stmt : node.thenBranch) {
        stmt->accept(*this);
    }
    indentLevel--;

    if (!node.elseBranch.empty()) {
        writeLine("} else {");
        indentLevel++;
        for (auto& stmt : node.elseBranch) {
            stmt->accept(*this);
        }
        indentLevel--;
    }

    writeLine("}");
}

void CodeGenerator::visit(Parser::WhileStmt& node) {
    indent();
    write("while (");
    node.condition->accept(*this);
    write(") {");
    output << "\n";

    indentLevel++;
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    indentLevel--;

    writeLine("}");
}

void CodeGenerator::visit(Parser::BreakStmt& node) {
    writeLine("break;");
}

void CodeGenerator::visit(Parser::ContinueStmt& node) {
    writeLine("continue;");
}

// Expression visitors
void CodeGenerator::visit(Parser::IntegerLiteralExpr& node) {
    // Just output the raw integer value - let the context determine if wrapping is needed
    write(std::to_string(node.value));
}

void CodeGenerator::visit(Parser::StringLiteralExpr& node) {
    // String literals create String objects
    // Escape special characters in the string for C++ output
    std::string escaped;
    for (char c : node.value) {
        switch (c) {
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            case '\\': escaped += "\\\\"; break;
            case '\"': escaped += "\\\""; break;
            default: escaped += c; break;
        }
    }
    // Output raw string literal - let the context wrap it if needed
    write("\"" + escaped + "\"");
}

void CodeGenerator::visit(Parser::BoolLiteralExpr& node) {
    // Just output the raw boolean value - let the context determine if wrapping is needed
    write(node.value ? "true" : "false");
}

void CodeGenerator::visit(Parser::ThisExpr& node) {
    // 'this' translates to 'this' in C++
    write("this");
}

void CodeGenerator::visit(Parser::IdentifierExpr& node) {
    // Track if this identifier refers to a smart pointer variable
    auto it = variableIsSmartPointer.find(node.name);
    if (it != variableIsSmartPointer.end()) {
        expressionIsSmartPointer[&node] = it->second;
    }

    write(sanitizeIdentifier(node.name));
}

void CodeGenerator::visit(Parser::ReferenceExpr& node) {
    // Generate address-of operator
    write("&");
    node.expr->accept(*this);
}

void CodeGenerator::visit(Parser::MemberAccessExpr& node) {
    node.object->accept(*this);

    // Check if this is a namespace/class member access (::)
    if (node.member.rfind("::", 0) == 0) {
        write(node.member); // Already has ::
    } else {
        // Determine if we need -> or . based on whether object is a smart pointer
        bool isSmartPtr = false;

        // If object is an IdentifierExpr, check if it's a user-defined type variable
        if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.object.get())) {
            isSmartPtr = expressionIsSmartPointer[node.object.get()];
        }
        // If object is a CallExpr, check if it's a constructor call that returns a smart pointer
        else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(node.object.get())) {
            // Check if this is a constructor call
            if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(callExpr->callee.get())) {
                if (memberExpr->member == "Constructor") {
                    // Constructor calls for user-defined types return smart pointers
                    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(memberExpr->object.get())) {
                        std::string className = identExpr->name;
                        std::string baseType = className;
                        size_t lastColon = className.rfind("::");
                        if (lastColon != std::string::npos) {
                            baseType = className.substr(lastColon + 2);
                        }
                        // Value types: String, Integer, Bool
                        // Smart pointer types: Float, Double, and all user-defined types
                        if (baseType != "String" && baseType != "Integer" && baseType != "Bool") {
                            isSmartPtr = true;
                        }
                    }
                }
            }
            // Also check for IdentifierExpr pattern
            else if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(callExpr->callee.get())) {
                std::string fullName = identExpr->name;
                if (fullName.find("::Constructor") != std::string::npos) {
                    size_t constructorPos = fullName.rfind("::Constructor");
                    if (constructorPos != std::string::npos) {
                        std::string className = fullName.substr(0, constructorPos);
                        std::string baseType = className;
                        size_t lastColon = className.rfind("::");
                        if (lastColon != std::string::npos) {
                            baseType = className.substr(lastColon + 2);
                        }
                        if (baseType != "String" && baseType != "Integer" && baseType != "Bool") {
                            isSmartPtr = true;
                        }
                    }
                }
            }
        }

        // Use -> for smart pointers, . for values
        if (isSmartPtr) {
            write("->" + sanitizeIdentifier(node.member));
        } else {
            write("." + sanitizeIdentifier(node.member));
        }
    }
}

void CodeGenerator::visit(Parser::CallExpr& node) {
    // Check if this is a constructor call
    bool isConstructorCall = false;
    std::string className;

    // Check for MemberAccessExpr pattern: Obj.Constructor() or Obj::Member::Constructor()
    if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(node.callee.get())) {
        if (memberExpr->member == "Constructor" || memberExpr->member == "::Constructor") {
            isConstructorCall = true;
            // Get the class name from the object part
            // Handle both simple identifiers and nested member access (for qualified names)
            if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(memberExpr->object.get())) {
                className = identExpr->name;
            } else if (auto* nestedMember = dynamic_cast<Parser::MemberAccessExpr*>(memberExpr->object.get())) {
                // Build the full qualified name from nested MemberAccessExprs
                // Recursively build: Test::Box<Integer> from nested structure
                std::function<std::string(Parser::Expression*)> buildQualifiedName;
                buildQualifiedName = [&](Parser::Expression* expr) -> std::string {
                    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
                        return ident->name;
                    } else if (auto* member = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
                        std::string base = buildQualifiedName(member->object.get());
                        // Remove leading :: from member if present
                        std::string memberName = member->member;
                        if (memberName.substr(0, 2) == "::") {
                            memberName = memberName.substr(2);
                        }
                        return base + "::" + memberName;
                    }
                    return "";
                };
                className = buildQualifiedName(memberExpr->object.get());
            }
        }
        // Check if this is a runtime extension function (Copy, Append, Length, etc.)
        else if (memberExpr->member == "Copy" || memberExpr->member == "Append" ||
                 memberExpr->member == "Length" || memberExpr->member == "CharAt" ||
                 memberExpr->member == "Substring" || memberExpr->member == "Equals") {
            // Transform obj.MethodName(args) to MethodName(obj, args)
            write(memberExpr->member + "(");
            memberExpr->object->accept(*this);

            if (!node.arguments.empty()) {
                write(", ");
                for (size_t i = 0; i < node.arguments.size(); ++i) {
                    if (i > 0) write(", ");
                    node.arguments[i]->accept(*this);
                }
            }

            write(")");
            return;  // Early return - we've handled this case
        }
    }
    // Also check for IdentifierExpr pattern: Full::Qualified::Name::Constructor()
    else if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.callee.get())) {
        std::string fullName = identExpr->name;
        // Check if it ends with ::Constructor
        if (fullName.find("::Constructor") != std::string::npos) {
            isConstructorCall = true;
            // Extract the class name (everything before ::Constructor)
            size_t constructorPos = fullName.rfind("::Constructor");
            if (constructorPos != std::string::npos) {
                className = fullName.substr(0, constructorPos);
            }
        }
    }

    if (isConstructorCall && !className.empty()) {
        // Handle constructor calls
        // Check if this is a built-in type (String, Integer, Bool) or user-defined
        // For built-ins, use direct construction; for user types, use make_unique

        // Convert className to handle template instantiations (mangling)
        std::string convertedClassName = convertType(className);

        // Extract the last component for built-in type checking
        std::string baseType = className;
        size_t lastColon = className.rfind("::");
        if (lastColon != std::string::npos) {
            baseType = className.substr(lastColon + 2);
        }

        // Check if baseType contains template args - remove them for built-in check
        std::string baseTypeNoTemplates = baseType;
        size_t anglePos = baseType.find('<');
        if (anglePos != std::string::npos) {
            baseTypeNoTemplates = baseType.substr(0, anglePos);
        }

        // With Owned<T> wrapper, all types use direct construction
        // Owned<T> wraps the actual object, not a unique_ptr
        write(convertedClassName + "(");

        // Write arguments
        for (size_t i = 0; i < node.arguments.size(); ++i) {
            if (i > 0) write(", ");

            // Check if argument is an Owned variable that needs std::move
            bool needsMove = false;
            if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.arguments[i].get())) {
                // Check if this variable is owned
                auto ownershipIt = variableOwnership.find(identExpr->name);
                if (ownershipIt != variableOwnership.end() &&
                    ownershipIt->second == Parser::OwnershipType::Owned) {
                    needsMove = true;
                }
            }

            if (needsMove) {
                write("std::move(");
                node.arguments[i]->accept(*this);
                write(")");
            } else {
                node.arguments[i]->accept(*this);
            }
        }

        write(")");
    } else {
        // Regular method call
        node.callee->accept(*this);

        write("(");

        for (size_t i = 0; i < node.arguments.size(); ++i) {
            if (i > 0) write(", ");

            // Check if argument is an Owned variable that needs std::move
            bool needsMove = false;
            if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(node.arguments[i].get())) {
                // Check if this variable is owned
                auto ownershipIt = variableOwnership.find(identExpr->name);
                if (ownershipIt != variableOwnership.end() &&
                    ownershipIt->second == Parser::OwnershipType::Owned) {
                    needsMove = true;
                }
            }

            if (needsMove) {
                write("std::move(");
                node.arguments[i]->accept(*this);
                write(")");
            } else {
                node.arguments[i]->accept(*this);
            }
        }

        write(")");
    }
}

void CodeGenerator::visit(Parser::BinaryExpr& node) {
    if (node.left) {
        // Binary operation
        // For arithmetic operators, use implicit C++ operator overloading
        // The Integer, String, Bool classes support standard operators via conversion
        node.left->accept(*this);
        write(" " + node.op + " ");
        node.right->accept(*this);
    } else {
        // Unary operation
        write(node.op);
        node.right->accept(*this);
    }
}

void CodeGenerator::visit(Parser::TypeRef& node) {
    // Type references are handled by their context
}

// ============================================================================
// Template Code Generation
// ============================================================================

std::string CodeGenerator::mangleTemplateName(const std::string& templateName, const std::vector<std::string>& args) {
    std::string result = templateName;

    // Handle qualified names: Collections::List -> Collections__List
    // Then append template args: Collections__List_Integer
    for (size_t i = 0; i < result.length(); ++i) {
        if (result[i] == ':' && i + 1 < result.length() && result[i + 1] == ':') {
            result.replace(i, 2, "__");
        }
    }

    // Append each template argument with underscore
    for (const auto& arg : args) {
        result += "_";
        std::string cleanArg = arg;
        // Replace :: with __ in arguments too
        for (size_t i = 0; i < cleanArg.length(); ++i) {
            if (cleanArg[i] == ':' && i + 1 < cleanArg.length() && cleanArg[i + 1] == ':') {
                cleanArg.replace(i, 2, "__");
            }
        }
        result += cleanArg;
    }

    return result;
}

std::unique_ptr<Parser::ClassDecl> CodeGenerator::cloneClassDecl(Parser::ClassDecl* original) {
    // Create new ClassDecl with copied template parameters
    auto cloned = std::make_unique<Parser::ClassDecl>(
        original->name,
        original->templateParams,  // Copy template params
        original->isFinal,
        original->baseClass,
        original->location
    );

    // Clone each access section
    for (auto& section : original->sections) {
        auto clonedSection = std::make_unique<Parser::AccessSection>(section->modifier, section->location);

        // Clone declarations in this section
        for (auto& decl : section->declarations) {
            // We need to clone each type of declaration
            // For now, we'll use a simplified approach - copy the pointer
            // In a production compiler, you'd deep-copy the entire AST
            // This is safe because we only read from the original
            clonedSection->declarations.push_back(std::unique_ptr<Parser::Declaration>(
                static_cast<Parser::Declaration*>(decl.get())
            ));
        }

        cloned->sections.push_back(std::move(clonedSection));
    }

    return cloned;
}

void CodeGenerator::substituteTypes(Parser::ClassDecl* classDecl, const std::unordered_map<std::string, std::string>& typeMap) {
    // Substitute types in each access section
    for (auto& section : classDecl->sections) {
        for (auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                substituteTypesInTypeRef(prop->type.get(), typeMap);
            }
            else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                // Substitute in return type
                substituteTypesInTypeRef(method->returnType.get(), typeMap);

                // Substitute in parameters
                for (auto& param : method->parameters) {
                    substituteTypesInTypeRef(param->type.get(), typeMap);
                }

                // Substitute in method body
                for (auto& stmt : method->body) {
                    substituteTypesInStatement(stmt.get(), typeMap);
                }
            }
            else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Substitute in parameters
                for (auto& param : ctor->parameters) {
                    substituteTypesInTypeRef(param->type.get(), typeMap);
                }

                // Substitute in constructor body
                for (auto& stmt : ctor->body) {
                    substituteTypesInStatement(stmt.get(), typeMap);
                }
            }
        }
    }
}

void CodeGenerator::substituteTypesInTypeRef(Parser::TypeRef* typeRef, const std::unordered_map<std::string, std::string>& typeMap) {
    // Check if this type name is a template parameter
    auto it = typeMap.find(typeRef->typeName);
    if (it != typeMap.end()) {
        // Replace template parameter with concrete type
        typeRef->typeName = it->second;
    }
}

void CodeGenerator::substituteTypesInStatement(Parser::Statement* stmt, const std::unordered_map<std::string, std::string>& typeMap) {
    if (auto* instantiate = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        substituteTypesInTypeRef(instantiate->type.get(), typeMap);
        if (instantiate->initializer) {
            substituteTypesInExpression(instantiate->initializer.get(), typeMap);
        }
    }
    else if (auto* run = dynamic_cast<Parser::RunStmt*>(stmt)) {
        substituteTypesInExpression(run->expression.get(), typeMap);
    }
    else if (auto* ret = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (ret->value) {
            substituteTypesInExpression(ret->value.get(), typeMap);
        }
    }
    else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        substituteTypesInExpression(ifStmt->condition.get(), typeMap);
        for (auto& s : ifStmt->thenBranch) {
            substituteTypesInStatement(s.get(), typeMap);
        }
        for (auto& s : ifStmt->elseBranch) {
            substituteTypesInStatement(s.get(), typeMap);
        }
    }
    else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        substituteTypesInExpression(whileStmt->condition.get(), typeMap);
        for (auto& s : whileStmt->body) {
            substituteTypesInStatement(s.get(), typeMap);
        }
    }
    // Add more statement types as needed
}

void CodeGenerator::substituteTypesInExpression(Parser::Expression* expr, const std::unordered_map<std::string, std::string>& typeMap) {
    if (auto* call = dynamic_cast<Parser::CallExpr*>(expr)) {
        substituteTypesInExpression(call->callee.get(), typeMap);
        for (auto& arg : call->arguments) {
            substituteTypesInExpression(arg.get(), typeMap);
        }
    }
    else if (auto* member = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        substituteTypesInExpression(member->object.get(), typeMap);
    }
    else if (auto* binary = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        substituteTypesInExpression(binary->left.get(), typeMap);
        substituteTypesInExpression(binary->right.get(), typeMap);
    }
    else if (auto* ref = dynamic_cast<Parser::ReferenceExpr*>(expr)) {
        substituteTypesInExpression(ref->expr.get(), typeMap);
    }
    // Literals don't need substitution
}

void CodeGenerator::generateTemplateInstantiations() {
    if (!semanticAnalyzer) {
        return;  // No semantic analyzer, skip template generation
    }

    const auto& instantiations = semanticAnalyzer->getTemplateInstantiations();
    const auto& templateClasses = semanticAnalyzer->getTemplateClasses();

    if (instantiations.empty()) {
        return;  // No templates to instantiate
    }

    writeLine("// ============================================================================");
    writeLine("// Template Instantiations");
    writeLine("// ============================================================================");
    writeLine("");

    for (const auto& inst : instantiations) {
        // Find the template class definition
        auto it = templateClasses.find(inst.templateName);
        if (it == templateClasses.end()) {
            // Template class not found - this should have been caught by semantic analysis
            continue;
        }

        Parser::ClassDecl* templateClass = it->second;

        // Clone the template class
        auto instantiatedClass = cloneClassDecl(templateClass);

        // Build type substitution map and extract type arguments as strings
        std::unordered_map<std::string, std::string> typeMap;
        std::vector<std::string> typeArgsAsStrings;
        size_t valueIndex = 0;

        for (size_t i = 0; i < templateClass->templateParams.size() && i < inst.arguments.size(); ++i) {
            const auto& param = templateClass->templateParams[i];
            const auto& arg = inst.arguments[i];

            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                typeMap[param.name] = arg.typeArg;
                typeArgsAsStrings.push_back(arg.typeArg);
            } else {
                // Non-type parameter - use evaluated value
                if (valueIndex < inst.evaluatedValues.size()) {
                    std::string valueStr = std::to_string(inst.evaluatedValues[valueIndex]);
                    typeMap[param.name] = valueStr;
                    typeArgsAsStrings.push_back(valueStr);
                    valueIndex++;
                }
            }
        }

        // Substitute types throughout the class
        substituteTypes(instantiatedClass.get(), typeMap);

        // Generate mangled name
        std::string mangledName = mangleTemplateName(inst.templateName, typeArgsAsStrings);
        instantiatedClass->name = mangledName;

        // Clear template parameters (it's no longer a template)
        instantiatedClass->templateParams.clear();

        // Generate code for this instantiated class
        writeLine("// Instantiation: " + inst.templateName + "<" +
                  [&typeArgsAsStrings]() {
                      std::string result;
                      for (size_t i = 0; i < typeArgsAsStrings.size(); ++i) {
                          if (i > 0) result += ", ";
                          result += typeArgsAsStrings[i];
                      }
                      return result;
                  }() + ">");
        instantiatedClass->accept(*this);
        writeLine("");
    }
}

} // namespace CodeGen
} // namespace XXML
