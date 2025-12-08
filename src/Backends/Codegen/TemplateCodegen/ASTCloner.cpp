#include "Backends/Codegen/TemplateCodegen/ASTCloner.h"
#include <iostream>

namespace XXML::Backends::Codegen {

std::unique_ptr<Parser::ClassDecl> ASTCloner::cloneClass(
    Parser::ClassDecl* original,
    const std::string& newName,
    const TypeMap& typeMap) {

    // Create new class with mangled name and no template parameters
    auto instantiated = std::make_unique<Parser::ClassDecl>(
        newName,
        std::vector<Parser::TemplateParameter>{},  // No template params in instantiation
        original->isFinal,
        original->baseClass,
        original->location
    );

    // Deep clone all sections
    for (const auto& section : original->sections) {
        auto newSection = std::make_unique<Parser::AccessSection>(
            section->modifier,
            section->location
        );

        // Clone all declarations in the section
        for (const auto& decl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                auto clonedProp = cloneProperty(prop, typeMap);
                if (clonedProp) newSection->declarations.push_back(std::move(clonedProp));
            }
            else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                // Clone constructor
                std::vector<std::unique_ptr<Parser::ParameterDecl>> clonedParams;
                for (const auto& param : ctor->parameters) {
                    auto clonedType = cloneType(param->type.get(), typeMap);
                    auto clonedParam = std::make_unique<Parser::ParameterDecl>(
                        param->name,
                        std::move(clonedType),
                        param->location
                    );
                    clonedParams.push_back(std::move(clonedParam));
                }

                std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
                for (const auto& stmt : ctor->body) {
                    auto clonedStmt = cloneStmt(stmt.get(), typeMap);
                    if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
                }

                auto clonedCtor = std::make_unique<Parser::ConstructorDecl>(
                    ctor->isDefault,
                    std::move(clonedParams),
                    std::move(clonedBody),
                    ctor->location
                );
                newSection->declarations.push_back(std::move(clonedCtor));
            }
            else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                auto clonedMethod = cloneMethod(method, typeMap);
                if (clonedMethod) newSection->declarations.push_back(std::move(clonedMethod));
            }
            else if (auto* dtor = dynamic_cast<Parser::DestructorDecl*>(decl.get())) {
                // Clone destructor
                std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
                for (const auto& stmt : dtor->body) {
                    auto clonedStmt = cloneStmt(stmt.get(), typeMap);
                    if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
                }

                auto clonedDtor = std::make_unique<Parser::DestructorDecl>(
                    std::move(clonedBody),
                    dtor->location
                );
                newSection->declarations.push_back(std::move(clonedDtor));
            }
        }

        instantiated->sections.push_back(std::move(newSection));
    }

    return instantiated;
}

std::unique_ptr<Parser::TypeRef> ASTCloner::cloneType(
    const Parser::TypeRef* original,
    const TypeMap& typeMap) {

    if (!original) return nullptr;

    // Substitute type name if it's a template parameter
    std::string typeName = original->typeName;
    auto it = typeMap.find(typeName);
    if (it != typeMap.end()) {
        typeName = it->second;
    }

    // Clone template arguments
    std::vector<Parser::TemplateArgument> clonedArgs;
    for (const auto& arg : original->templateArgs) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Type argument - substitute if it's a template parameter
            std::string argType = arg.typeArg;
            auto argIt = typeMap.find(argType);
            if (argIt != typeMap.end()) {
                argType = argIt->second;
            }
            clonedArgs.emplace_back(argType, arg.location);
        } else {
            // Value argument - clone the expression
            auto clonedExpr = cloneExpr(arg.valueArg.get(), typeMap);
            clonedArgs.emplace_back(std::move(clonedExpr), arg.location);
        }
    }

    // Record nested template instantiation if this type has template arguments
    // This ensures types like ListIterator<Integer> are also instantiated
    if (!clonedArgs.empty()) {
        recordNestedInstantiation(typeName, clonedArgs);
    }

    return std::make_unique<Parser::TypeRef>(
        typeName,
        std::move(clonedArgs),
        original->ownership,
        original->location
    );
}

std::unique_ptr<Parser::MethodDecl> ASTCloner::cloneMethod(
    const Parser::MethodDecl* original,
    const TypeMap& typeMap) {

    if (!original) return nullptr;

    auto clonedRetType = cloneType(original->returnType.get(), typeMap);

    std::vector<std::unique_ptr<Parser::ParameterDecl>> clonedParams;
    for (const auto& param : original->parameters) {
        auto clonedType = cloneType(param->type.get(), typeMap);
        auto clonedParam = std::make_unique<Parser::ParameterDecl>(
            param->name,
            std::move(clonedType),
            param->location
        );
        clonedParams.push_back(std::move(clonedParam));
    }

    std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
    for (const auto& stmt : original->body) {
        auto clonedStmt = cloneStmt(stmt.get(), typeMap);
        if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
    }

    return std::make_unique<Parser::MethodDecl>(
        original->name,
        std::move(clonedRetType),
        std::move(clonedParams),
        std::move(clonedBody),
        original->location
    );
}

std::unique_ptr<Parser::MethodDecl> ASTCloner::cloneMethodWithName(
    Parser::MethodDecl* original,
    const std::string& newName,
    const TypeMap& typeMap) {

    if (!original) return nullptr;

    auto clonedRetType = cloneType(original->returnType.get(), typeMap);

    std::vector<std::unique_ptr<Parser::ParameterDecl>> clonedParams;
    for (const auto& param : original->parameters) {
        auto clonedType = cloneType(param->type.get(), typeMap);
        auto clonedParam = std::make_unique<Parser::ParameterDecl>(
            param->name,
            std::move(clonedType),
            param->location
        );
        clonedParams.push_back(std::move(clonedParam));
    }

    std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
    for (const auto& stmt : original->body) {
        auto clonedStmt = cloneStmt(stmt.get(), typeMap);
        if (clonedStmt) clonedBody.push_back(std::move(clonedStmt));
    }

    // Create method with the new (mangled) name
    auto clonedMethod = std::make_unique<Parser::MethodDecl>(
        newName,  // Use the provided mangled name
        std::move(clonedRetType),
        std::move(clonedParams),
        std::move(clonedBody),
        original->location
    );

    // Clear template params since this is an instantiated (concrete) method
    clonedMethod->templateParams.clear();

    return clonedMethod;
}

std::unique_ptr<Parser::PropertyDecl> ASTCloner::cloneProperty(
    const Parser::PropertyDecl* original,
    const TypeMap& typeMap) {

    if (!original) return nullptr;

    auto clonedType = cloneType(original->type.get(), typeMap);
    return std::make_unique<Parser::PropertyDecl>(
        original->name,
        std::move(clonedType),
        original->location
    );
}

std::unique_ptr<Parser::Statement> ASTCloner::cloneStmt(
    const Parser::Statement* stmt,
    const TypeMap& typeMap) {

    if (!stmt) return nullptr;

    // Handle different statement types
    if (auto* inst = dynamic_cast<const Parser::InstantiateStmt*>(stmt)) {
        auto clonedType = cloneType(inst->type.get(), typeMap);
        auto clonedInit = cloneExpr(inst->initializer.get(), typeMap);
        return std::make_unique<Parser::InstantiateStmt>(
            std::move(clonedType),
            inst->variableName,
            std::move(clonedInit),
            inst->location
        );
    }
    if (auto* run = dynamic_cast<const Parser::RunStmt*>(stmt)) {
        auto clonedExpr = cloneExpr(run->expression.get(), typeMap);
        return std::make_unique<Parser::RunStmt>(std::move(clonedExpr), run->location);
    }
    if (auto* ret = dynamic_cast<const Parser::ReturnStmt*>(stmt)) {
        auto clonedExpr = ret->value ? cloneExpr(ret->value.get(), typeMap) : nullptr;
        return std::make_unique<Parser::ReturnStmt>(std::move(clonedExpr), ret->location);
    }
    if (auto* assign = dynamic_cast<const Parser::AssignmentStmt*>(stmt)) {
        auto clonedTarget = cloneExpr(assign->target.get(), typeMap);
        auto clonedValue = cloneExpr(assign->value.get(), typeMap);
        return std::make_unique<Parser::AssignmentStmt>(
            std::move(clonedTarget),
            std::move(clonedValue),
            assign->location
        );
    }
    if (auto* ifStmt = dynamic_cast<const Parser::IfStmt*>(stmt)) {
        auto clonedCondition = cloneExpr(ifStmt->condition.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedThenBranch;
        for (const auto& s : ifStmt->thenBranch) {
            auto cloned = cloneStmt(s.get(), typeMap);
            if (cloned) clonedThenBranch.push_back(std::move(cloned));
        }

        std::vector<std::unique_ptr<Parser::Statement>> clonedElseBranch;
        for (const auto& s : ifStmt->elseBranch) {
            auto cloned = cloneStmt(s.get(), typeMap);
            if (cloned) clonedElseBranch.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::IfStmt>(
            std::move(clonedCondition),
            std::move(clonedThenBranch),
            std::move(clonedElseBranch),
            ifStmt->location
        );
    }
    if (auto* whileStmt = dynamic_cast<const Parser::WhileStmt*>(stmt)) {
        auto clonedCondition = cloneExpr(whileStmt->condition.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
        for (const auto& s : whileStmt->body) {
            auto cloned = cloneStmt(s.get(), typeMap);
            if (cloned) clonedBody.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::WhileStmt>(
            std::move(clonedCondition),
            std::move(clonedBody),
            whileStmt->location
        );
    }
    if (auto* forStmt = dynamic_cast<const Parser::ForStmt*>(stmt)) {
        auto clonedIteratorType = cloneType(forStmt->iteratorType.get(), typeMap);
        auto clonedRangeStart = cloneExpr(forStmt->rangeStart.get(), typeMap);
        auto clonedRangeEnd = cloneExpr(forStmt->rangeEnd.get(), typeMap);

        std::vector<std::unique_ptr<Parser::Statement>> clonedBody;
        for (const auto& s : forStmt->body) {
            auto cloned = cloneStmt(s.get(), typeMap);
            if (cloned) clonedBody.push_back(std::move(cloned));
        }

        return std::make_unique<Parser::ForStmt>(
            std::move(clonedIteratorType),
            forStmt->iteratorName,
            std::move(clonedRangeStart),
            std::move(clonedRangeEnd),
            std::move(clonedBody),
            forStmt->location
        );
    }
    if (auto* breakStmt = dynamic_cast<const Parser::BreakStmt*>(stmt)) {
        return std::make_unique<Parser::BreakStmt>(breakStmt->location);
    }
    if (auto* continueStmt = dynamic_cast<const Parser::ContinueStmt*>(stmt)) {
        return std::make_unique<Parser::ContinueStmt>(continueStmt->location);
    }
    if (auto* exitStmt = dynamic_cast<const Parser::ExitStmt*>(stmt)) {
        auto clonedCode = cloneExpr(exitStmt->exitCode.get(), typeMap);
        return std::make_unique<Parser::ExitStmt>(std::move(clonedCode), exitStmt->location);
    }

    // For other statement types, return nullptr (placeholder)
    return nullptr;
}

std::unique_ptr<Parser::Expression> ASTCloner::cloneExpr(
    const Parser::Expression* expr,
    const TypeMap& typeMap) {

    if (!expr) return nullptr;

    // Clone various expression types
    if (auto* intLit = dynamic_cast<const Parser::IntegerLiteralExpr*>(expr)) {
        return std::make_unique<Parser::IntegerLiteralExpr>(intLit->value, intLit->location);
    }
    if (auto* strLit = dynamic_cast<const Parser::StringLiteralExpr*>(expr)) {
        return std::make_unique<Parser::StringLiteralExpr>(strLit->value, strLit->location);
    }
    if (auto* boolLit = dynamic_cast<const Parser::BoolLiteralExpr*>(expr)) {
        return std::make_unique<Parser::BoolLiteralExpr>(boolLit->value, boolLit->location);
    }
    if (auto* ident = dynamic_cast<const Parser::IdentifierExpr*>(expr)) {
        // Check if this identifier is a template parameter that needs substitution
        auto it = typeMap.find(ident->name);
        if (it != typeMap.end()) {
            // This is a template parameter - substitute with the actual type
            // The type might be qualified (e.g., "MyNamespace::MyClass")
            // We need to convert it to a MemberAccessExpr chain
            std::string fullType = it->second;

            // Split by :: to build nested MemberAccessExprs
            size_t pos = fullType.find("::");
            if (pos != std::string::npos) {
                // Build chain: MyNamespace::MyClass -> MemberAccessExpr(IdentifierExpr("MyNamespace"), "::MyClass")
                std::unique_ptr<Parser::Expression> current = std::make_unique<Parser::IdentifierExpr>(
                    fullType.substr(0, pos), ident->location);

                size_t start = pos;
                while ((pos = fullType.find("::", start + 2)) != std::string::npos) {
                    std::string member = "::" + fullType.substr(start + 2, pos - start - 2);
                    current = std::make_unique<Parser::MemberAccessExpr>(
                        std::move(current), member, ident->location);
                    start = pos;
                }

                // Add the final member
                std::string lastMember = "::" + fullType.substr(start + 2);
                return std::make_unique<Parser::MemberAccessExpr>(
                    std::move(current), lastMember, ident->location);
            } else {
                // Simple type without namespace - just replace the identifier name
                return std::make_unique<Parser::IdentifierExpr>(fullType, ident->location);
            }
        }

        // Check if the identifier contains template arguments that need substitution
        // e.g., "ListIterator<T>" -> "ListIterator<Integer>" when T maps to Integer
        std::string identName = ident->name;
        size_t ltPos = identName.find('<');
        if (ltPos != std::string::npos) {
            size_t gtPos = identName.rfind('>');
            if (gtPos != std::string::npos && gtPos > ltPos) {
                // Extract template arguments and substitute
                std::string baseName = identName.substr(0, ltPos);
                std::string argsStr = identName.substr(ltPos + 1, gtPos - ltPos - 1);

                // Parse and substitute template arguments (handles comma-separated args)
                std::string newArgs;
                size_t argStart = 0;
                int depth = 0;
                for (size_t i = 0; i <= argsStr.length(); ++i) {
                    bool atEnd = (i == argsStr.length());
                    char c = atEnd ? ',' : argsStr[i];

                    if (c == '<') depth++;
                    else if (c == '>') depth--;
                    else if ((c == ',' && depth == 0) || atEnd) {
                        std::string arg = argsStr.substr(argStart, i - argStart);
                        // Trim whitespace
                        while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t')) arg.erase(0, 1);
                        while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t')) arg.pop_back();

                        // Substitute if this arg is a template parameter
                        auto argIt = typeMap.find(arg);
                        if (argIt != typeMap.end()) {
                            arg = argIt->second;
                        }

                        if (!newArgs.empty()) newArgs += ", ";
                        newArgs += arg;
                        argStart = i + 1;
                    }
                }

                identName = baseName + "<" + newArgs + ">";
            }
        }

        // Return identifier with potentially substituted template args
        return std::make_unique<Parser::IdentifierExpr>(identName, ident->location);
    }
    if (auto* call = dynamic_cast<const Parser::CallExpr*>(expr)) {
        // Check for __typename intrinsic - replaces __typename(T) with the actual type name string
        bool isTypenameIntrinsic = false;
        if (auto* calleeIdent = dynamic_cast<const Parser::IdentifierExpr*>(call->callee.get())) {
            isTypenameIntrinsic = (calleeIdent->name == "__typename");
        } else if (auto* calleeMember = dynamic_cast<const Parser::MemberAccessExpr*>(call->callee.get())) {
            // Also support Syscall::typename syntax
            if (auto* baseIdent = dynamic_cast<const Parser::IdentifierExpr*>(calleeMember->object.get())) {
                isTypenameIntrinsic = (baseIdent->name == "Syscall" && calleeMember->member == "::typename");
            }
        }

        if (isTypenameIntrinsic && call->arguments.size() == 1) {
            // Check if the argument is a template parameter
            if (auto* argIdent = dynamic_cast<const Parser::IdentifierExpr*>(call->arguments[0].get())) {
                auto it = typeMap.find(argIdent->name);
                if (it != typeMap.end()) {
                    // Found! Replace the entire call with a string literal of the type name
                    return std::make_unique<Parser::StringLiteralExpr>(it->second, call->location);
                }
            }
        }

        // Not a __typename intrinsic, clone normally
        auto clonedCallee = cloneExpr(call->callee.get(), typeMap);
        std::vector<std::unique_ptr<Parser::Expression>> clonedArgs;
        for (const auto& arg : call->arguments) {
            auto clonedArg = cloneExpr(arg.get(), typeMap);
            if (clonedArg) clonedArgs.push_back(std::move(clonedArg));
        }
        return std::make_unique<Parser::CallExpr>(
            std::move(clonedCallee),
            std::move(clonedArgs),
            call->location
        );
    }
    if (auto* member = dynamic_cast<const Parser::MemberAccessExpr*>(expr)) {
        auto clonedObject = cloneExpr(member->object.get(), typeMap);
        return std::make_unique<Parser::MemberAccessExpr>(
            std::move(clonedObject),
            member->member,
            member->location
        );
    }
    if (auto* binary = dynamic_cast<const Parser::BinaryExpr*>(expr)) {
        auto clonedLeft = cloneExpr(binary->left.get(), typeMap);
        auto clonedRight = cloneExpr(binary->right.get(), typeMap);
        return std::make_unique<Parser::BinaryExpr>(
            std::move(clonedLeft),
            binary->op,
            std::move(clonedRight),
            binary->location
        );
    }
    if (auto* ref = dynamic_cast<const Parser::ReferenceExpr*>(expr)) {
        auto clonedInner = cloneExpr(ref->expr.get(), typeMap);
        return std::make_unique<Parser::ReferenceExpr>(
            std::move(clonedInner),
            ref->location
        );
    }
    if (auto* thisExpr = dynamic_cast<const Parser::ThisExpr*>(expr)) {
        return std::make_unique<Parser::ThisExpr>(thisExpr->location);
    }

    // For unknown expression types, return nullptr
    return nullptr;
}

std::unique_ptr<Parser::LambdaExpr> ASTCloner::cloneLambda(
    const Parser::LambdaExpr* lambda,
    const TypeMap& typeMap) {
    if (!lambda) return nullptr;

    // Clone captures (they don't have types that need substitution)
    std::vector<Parser::LambdaExpr::CaptureSpec> captures = lambda->captures;

    // Clone parameters with type substitution
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;
    for (const auto& param : lambda->parameters) {
        auto clonedType = cloneType(param->type.get(), typeMap);
        params.push_back(std::make_unique<Parser::ParameterDecl>(
            param->name, std::move(clonedType), param->location));
    }

    // Clone return type with substitution
    auto clonedReturnType = cloneType(lambda->returnType.get(), typeMap);

    // Clone body statements with type substitution
    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;
    for (const auto& stmt : lambda->body) {
        if (auto clonedStmt = cloneStmt(stmt.get(), typeMap)) {
            bodyStmts.push_back(std::move(clonedStmt));
        }
    }

    auto cloned = std::make_unique<Parser::LambdaExpr>(
        std::move(captures),
        std::move(params),
        std::move(clonedReturnType),
        std::move(bodyStmts),
        lambda->location);

    cloned->isCompiletime = lambda->isCompiletime;
    // Don't copy templateParams - the cloned lambda is a concrete instantiation

    return cloned;
}

void ASTCloner::recordNestedInstantiation(const std::string& typeName,
                                           const std::vector<Parser::TemplateArgument>& args) {
    // Skip built-in types that don't need instantiation
    if (typeName == "Integer" || typeName == "Float" || typeName == "Bool" ||
        typeName == "String" || typeName == "None" || typeName == "NativeType") {
        return;
    }

    // Check if any argument is still a template parameter (contains only alphanumeric and _)
    // These are unsubstituted template parameters like T, K, V
    for (const auto& arg : args) {
        if (arg.kind == Parser::TemplateArgument::Kind::Type) {
            // Simple heuristic: single uppercase letter or common param names are template params
            const std::string& typeArg = arg.typeArg;
            if (typeArg.length() == 1 && std::isupper(typeArg[0])) {
                return; // Still has unsubstituted template parameter
            }
            if (typeArg == "K" || typeArg == "V" || typeArg == "T" || typeArg == "U" ||
                typeArg == "Key" || typeArg == "Value" || typeArg == "Element") {
                return; // Common template parameter names
            }
        }
    }

    // Record this instantiation
    NestedTemplateInstantiation inst;
    inst.templateName = typeName;
    inst.arguments = args;
    nestedInstantiations_.insert(inst);
}

} // namespace XXML::Backends::Codegen
