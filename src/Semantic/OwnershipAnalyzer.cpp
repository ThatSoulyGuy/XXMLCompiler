#include "../../include/Semantic/OwnershipAnalyzer.h"
#include <iostream>

namespace XXML {
namespace Semantic {

OwnershipAnalyzer::OwnershipAnalyzer(Common::ErrorReporter& errorReporter)
    : errorReporter_(errorReporter),
      currentScope_(0) {
    // Initialize with global scope
    scopeVariables_.push({});
}

OwnershipAnalysisResult OwnershipAnalyzer::run(Parser::Program& program,
                                                const SemanticValidationResult& semanticResult) {
    result_ = OwnershipAnalysisResult{};
    semanticResult_ = &semanticResult;  // Store for use in analyzeCallArguments

    // Analyze each declaration
    for (auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            // Iterate through sections (public, private, etc.)
            for (auto& section : classDecl->sections) {
                for (auto& member : section->declarations) {
                    if (auto* method = dynamic_cast<Parser::MethodDecl*>(member.get())) {
                        if (!method->isNative) {
                            analyzeMethod(method);
                        }
                    } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(member.get())) {
                        // Analyze constructor
                        pushScope();
                        for (auto& param : ctor->parameters) {
                            if (param && param->type) {
                                declareVariable(param->name, param->type->typeName,
                                              param->type->ownership, param->location);
                            }
                        }
                        analyzeBlock(ctor->body);
                        popScope();
                    }
                }
            }
        }
    }

    // Populate result
    for (const auto& [name, info] : variables_) {
        result_.variableStates[name] = info.state;
    }

    result_.success = result_.violations.empty() && !errorReporter_.hasErrors();
    return result_;
}

void OwnershipAnalyzer::analyzeMethod(Parser::MethodDecl* method) {
    if (!method) return;

    pushScope();

    // Register parameters
    for (auto& param : method->parameters) {
        if (param && param->type) {
            declareVariable(param->name, param->type->typeName,
                          param->type->ownership, param->location);
        }
    }

    // Analyze body (body is a vector of statements)
    analyzeBlock(method->body);

    popScope();
}

void OwnershipAnalyzer::analyzeLambda(Parser::LambdaExpr* lambda) {
    if (!lambda) return;

    pushScope();

    // Analyze captures
    analyzeLambdaCaptures(lambda);

    // Register parameters
    for (auto& param : lambda->parameters) {
        if (param && param->type) {
            declareVariable(param->name, param->type->typeName,
                          param->type->ownership, param->location);
        }
    }

    // Analyze body
    analyzeBlock(lambda->body);

    popScope();
}

//==============================================================================
// QUERY METHODS
//==============================================================================

OwnershipState OwnershipAnalyzer::getVariableState(const std::string& varName) const {
    auto it = variables_.find(varName);
    if (it != variables_.end()) {
        return it->second.state;
    }
    return OwnershipState::Invalid;
}

bool OwnershipAnalyzer::isVariableMoved(const std::string& varName) const {
    return getVariableState(varName) == OwnershipState::Moved;
}

bool OwnershipAnalyzer::isVariableBorrowed(const std::string& varName) const {
    return getVariableState(varName) == OwnershipState::Borrowed;
}

//==============================================================================
// SCOPE MANAGEMENT
//==============================================================================

void OwnershipAnalyzer::pushScope() {
    currentScope_++;
    scopeVariables_.push({});
}

void OwnershipAnalyzer::popScope() {
    // Check for dangling references before leaving scope
    checkBorrowsValid(currentScope_);

    // Clean up variables in this scope
    if (!scopeVariables_.empty()) {
        for (const auto& varName : scopeVariables_.top()) {
            variables_.erase(varName);
        }
        scopeVariables_.pop();
    }

    currentScope_--;
}

//==============================================================================
// OWNERSHIP OPERATIONS
//==============================================================================

void OwnershipAnalyzer::declareVariable(const std::string& name, const std::string& typeName,
                                        Parser::OwnershipType ownership,
                                        const Common::SourceLocation& loc) {
    VariableInfo info;
    info.name = name;
    info.typeName = typeName;
    info.ownership = ownership;
    info.state = OwnershipState::Owned;
    info.scopeLevel = currentScope_;
    info.declLocation = loc;

    variables_[name] = info;

    if (!scopeVariables_.empty()) {
        scopeVariables_.top().insert(name);
    }
}

void OwnershipAnalyzer::markMoved(const std::string& varName, const Common::SourceLocation& loc) {
    auto it = variables_.find(varName);
    if (it == variables_.end()) return;

    if (it->second.state == OwnershipState::Moved) {
        // Double move!
        reportDoubleMove(varName, loc, it->second.moveLocation);
        return;
    }

    it->second.state = OwnershipState::Moved;
    it->second.moveLocation = loc;
}

void OwnershipAnalyzer::markBorrowed(const std::string& varName, const std::string& /* refName */,
                                     int borrowScope) {
    auto it = variables_.find(varName);
    if (it != variables_.end()) {
        it->second.state = OwnershipState::Borrowed;
        it->second.borrowScopes.push_back(borrowScope);
    }
}

void OwnershipAnalyzer::checkNotMoved(const std::string& varName, const Common::SourceLocation& loc) {
    auto it = variables_.find(varName);
    if (it != variables_.end() && it->second.state == OwnershipState::Moved) {
        reportUseAfterMove(varName, loc, it->second.moveLocation);
    }
}

void OwnershipAnalyzer::checkBorrowsValid(int scopeLevel) {
    for (auto& [name, info] : variables_) {
        if (info.scopeLevel == scopeLevel) {
            // This variable is going out of scope
            // Check if any references to it exist in outer scopes
            for (const auto& borrowScope : info.borrowScopes) {
                if (borrowScope < scopeLevel) {
                    // Reference exists in outer scope - dangling!
                    reportDanglingReference("reference", name, info.declLocation);
                }
            }
        }
    }
}

//==============================================================================
// ANALYSIS HELPERS
//==============================================================================

void OwnershipAnalyzer::analyzeBlock(const std::vector<std::unique_ptr<Parser::Statement>>& statements) {
    for (auto& stmt : statements) {
        if (stmt) {
            analyzeStatement(stmt.get());
        }
    }
}

void OwnershipAnalyzer::analyzeStatement(Parser::Statement* stmt) {
    if (!stmt) return;

    // InstantiateStmt
    if (auto* inst = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        if (inst->initializer) {
            analyzeExpression(inst->initializer.get());
        }
        if (inst->type) {
            declareVariable(inst->variableName, inst->type->typeName,
                          inst->type->ownership, inst->location);
        }
    }
    // AssignmentStmt
    else if (auto* assign = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        if (assign->value) {
            analyzeExpression(assign->value.get());
        }
        if (assign->target) {
            analyzeAssignmentTarget(assign->target.get());
        }
    }
    // ReturnStmt
    else if (auto* ret = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        if (ret->value) {
            analyzeExpression(ret->value.get());

            // If returning an owned variable, it's moved
            std::string varName = extractVariableName(ret->value.get());
            if (!varName.empty()) {
                auto it = variables_.find(varName);
                if (it != variables_.end() && isOwnedType(it->second.ownership)) {
                    markMoved(varName, ret->location);
                }
            }
        }
    }
    // IfStmt
    else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        if (ifStmt->condition) {
            analyzeExpression(ifStmt->condition.get());
        }

        // Analyze then branch
        pushScope();
        analyzeBlock(ifStmt->thenBranch);
        popScope();

        // Analyze else branch
        if (!ifStmt->elseBranch.empty()) {
            pushScope();
            analyzeBlock(ifStmt->elseBranch);
            popScope();
        }
    }
    // WhileStmt
    else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        if (whileStmt->condition) {
            analyzeExpression(whileStmt->condition.get());
        }

        pushScope();
        analyzeBlock(whileStmt->body);
        popScope();
    }
    // ForStmt
    else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        pushScope();

        // Loop variable
        if (forStmt->iteratorType) {
            declareVariable(forStmt->iteratorName, forStmt->iteratorType->typeName,
                          forStmt->iteratorType->ownership, forStmt->location);
        }

        // Analyze range
        if (forStmt->rangeStart) analyzeExpression(forStmt->rangeStart.get());
        if (forStmt->rangeEnd) analyzeExpression(forStmt->rangeEnd.get());

        // Analyze body
        analyzeBlock(forStmt->body);

        popScope();
    }
    // RunStmt
    else if (auto* run = dynamic_cast<Parser::RunStmt*>(stmt)) {
        if (run->expression) {
            analyzeExpression(run->expression.get());
        }
    }
    // ExitStmt
    else if (auto* exit = dynamic_cast<Parser::ExitStmt*>(stmt)) {
        if (exit->exitCode) {
            analyzeExpression(exit->exitCode.get());
        }
    }
    // BreakStmt, ContinueStmt, RequireStmt - no ownership implications
}

void OwnershipAnalyzer::analyzeExpression(Parser::Expression* expr) {
    if (!expr) return;

    // IdentifierExpr
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        checkNotMoved(ident->name, ident->location);
    }
    // ReferenceExpr
    else if (auto* ref = dynamic_cast<Parser::ReferenceExpr*>(expr)) {
        if (ref->expr) {
            analyzeExpression(ref->expr.get());

            // Track the borrow
            std::string varName = extractVariableName(ref->expr.get());
            if (!varName.empty()) {
                markBorrowed(varName, "reference", currentScope_);
            }
        }
    }
    // MemberAccessExpr
    else if (auto* member = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        if (member->object) {
            analyzeExpression(member->object.get());
        }
    }
    // CallExpr
    else if (auto* call = dynamic_cast<Parser::CallExpr*>(expr)) {
        analyzeCallArguments(call);
        if (call->callee) {
            analyzeExpression(call->callee.get());
        }
    }
    // BinaryExpr
    else if (auto* binary = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        if (binary->left) analyzeExpression(binary->left.get());
        if (binary->right) analyzeExpression(binary->right.get());
    }
    // LambdaExpr
    else if (auto* lambda = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        analyzeLambda(lambda);
    }
    // Literals and other expressions - no ownership implications
}

void OwnershipAnalyzer::analyzeLambdaCaptures(Parser::LambdaExpr* lambda) {
    if (!lambda) return;

    for (const auto& capture : lambda->captures) {
        // Check if captured variable exists and is not moved
        checkNotMoved(capture.varName, lambda->location);

        auto it = variables_.find(capture.varName);
        if (it == variables_.end()) continue;

        // Owned captures move the variable
        if (capture.mode == Parser::LambdaExpr::CaptureMode::Owned) {
            markMoved(capture.varName, lambda->location);
        } else if (capture.mode == Parser::LambdaExpr::CaptureMode::Reference) {
            markBorrowed(capture.varName, "lambda", currentScope_);
        }
    }
}

void OwnershipAnalyzer::analyzeCallArguments(Parser::CallExpr* call) {
    if (!call) return;

    // Try to look up the function signature to determine parameter ownership
    const MethodInfo* methodInfo = nullptr;

    if (semanticResult_ && call->callee) {
        // Try to resolve the call target
        // Case 1: MemberAccessExpr (obj.method or Class.method for static calls)
        if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(call->callee.get())) {
            std::string methodName = memberAccess->member;

            // Try to get the object's type name
            // If object is an IdentifierExpr, check if it's a class name (static call) or variable
            if (auto* objIdent = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
                // First check if it's a class name (static call)
                auto classIt = semanticResult_->classRegistry.find(objIdent->name);
                if (classIt != semanticResult_->classRegistry.end() && classIt->second) {
                    auto methodIt = classIt->second->methods.find(methodName);
                    if (methodIt != classIt->second->methods.end()) {
                        methodInfo = &methodIt->second;
                    }
                }

                // If not found, check if it's a known variable
                if (!methodInfo) {
                    auto varIt = variables_.find(objIdent->name);
                    if (varIt != variables_.end()) {
                        // Look up the type's methods
                        auto classIt2 = semanticResult_->classRegistry.find(varIt->second.typeName);
                        if (classIt2 != semanticResult_->classRegistry.end() && classIt2->second) {
                            auto methodIt2 = classIt2->second->methods.find(methodName);
                            if (methodIt2 != classIt2->second->methods.end()) {
                                methodInfo = &methodIt2->second;
                            }
                        }
                    }
                }
            }
        }
    }

    // If we found method info, analyze arguments with ownership awareness
    if (methodInfo && !methodInfo->parameters.empty()) {
        for (size_t i = 0; i < call->arguments.size(); ++i) {
            auto& arg = call->arguments[i];
            if (!arg) continue;

            // Get parameter ownership if available
            Parser::OwnershipType paramOwnership = Parser::OwnershipType::None;
            if (i < methodInfo->parameters.size()) {
                paramOwnership = methodInfo->parameters[i].second;
            }

            // Check if this argument would be moved
            if (paramOwnership == Parser::OwnershipType::Owned) {
                std::string varName = extractVariableName(arg.get());
                if (!varName.empty()) {
                    auto varIt = variables_.find(varName);
                    if (varIt != variables_.end() && isOwnedType(varIt->second.ownership)) {
                        // This argument will be moved to the function
                        markMoved(varName, call->location);
                    }
                }
            } else if (paramOwnership == Parser::OwnershipType::Reference) {
                std::string varName = extractVariableName(arg.get());
                if (!varName.empty()) {
                    markBorrowed(varName, "call argument", currentScope_);
                }
            }

            analyzeExpression(arg.get());
        }
    } else {
        // Fallback: analyze all arguments without ownership awareness
        for (auto& arg : call->arguments) {
            if (arg) {
                analyzeExpression(arg.get());
            }
        }
    }
}

void OwnershipAnalyzer::analyzeAssignmentTarget(Parser::Expression* target) {
    // For property assignments, the target might consume the value
    if (auto* member = dynamic_cast<Parser::MemberAccessExpr*>(target)) {
        if (member->object) {
            analyzeExpression(member->object.get());
        }
    }
}

bool OwnershipAnalyzer::isOwnedType(Parser::OwnershipType ownership) const {
    return ownership == Parser::OwnershipType::Owned;
}

bool OwnershipAnalyzer::wouldMove(Parser::Expression* expr, Parser::OwnershipType paramOwnership) {
    if (paramOwnership != Parser::OwnershipType::Owned) return false;

    std::string varName = extractVariableName(expr);
    if (varName.empty()) return false;

    auto it = variables_.find(varName);
    return it != variables_.end() && isOwnedType(it->second.ownership);
}

std::string OwnershipAnalyzer::extractVariableName(Parser::Expression* expr) {
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        return ident->name;
    }
    return "";
}

//==============================================================================
// ERROR REPORTING
//==============================================================================

void OwnershipAnalyzer::reportUseAfterMove(const std::string& varName,
                                           const Common::SourceLocation& useLoc,
                                           const Common::SourceLocation& moveLoc) {
    OwnershipViolation violation;
    violation.kind = OwnershipViolation::Kind::UseAfterMove;
    violation.variableName = varName;
    violation.useLocation = useLoc;
    violation.moveLocation = moveLoc;
    violation.message = "Use of moved variable '" + varName + "'";

    result_.violations.push_back(violation);

    errorReporter_.reportError(
        Common::ErrorCode::InvalidReference,
        violation.message,
        useLoc
    );

    // Add note showing where variable was moved
    errorReporter_.reportNote(
        "Variable '" + varName + "' was moved here",
        moveLoc
    );

    // Add suggested fix
    errorReporter_.reportNote(
        "Suggestion: Use a reference (&) instead of owned (^) to borrow without moving, "
        "or use copy (%) to create a duplicate",
        useLoc
    );
}

void OwnershipAnalyzer::reportDoubleMove(const std::string& varName,
                                         const Common::SourceLocation& secondMove,
                                         const Common::SourceLocation& firstMove) {
    OwnershipViolation violation;
    violation.kind = OwnershipViolation::Kind::DoubleMoveViolation;
    violation.variableName = varName;
    violation.useLocation = secondMove;
    violation.moveLocation = firstMove;
    violation.message = "Variable '" + varName + "' moved twice";

    result_.violations.push_back(violation);

    errorReporter_.reportError(
        Common::ErrorCode::InvalidReference,
        violation.message,
        secondMove
    );

    // Add note showing first move location
    errorReporter_.reportNote(
        "Variable '" + varName + "' was first moved here",
        firstMove
    );

    // Add suggested fix
    errorReporter_.reportNote(
        "Suggestion: Clone the value before the first move if you need to use it twice, "
        "or restructure code to avoid multiple moves",
        secondMove
    );
}

void OwnershipAnalyzer::reportDanglingReference(const std::string& refName,
                                                const std::string& targetName,
                                                const Common::SourceLocation& loc) {
    OwnershipViolation violation;
    violation.kind = OwnershipViolation::Kind::DanglingReference;
    violation.variableName = targetName;
    violation.useLocation = loc;
    violation.message = "Reference '" + refName + "' to '" + targetName + "' outlives target";

    result_.violations.push_back(violation);

    errorReporter_.reportError(
        Common::ErrorCode::InvalidReference,
        violation.message,
        loc
    );

    // Add explanation
    errorReporter_.reportNote(
        "The referenced variable '" + targetName + "' goes out of scope while '" + refName + "' is still alive",
        loc
    );

    // Add suggested fix
    errorReporter_.reportNote(
        "Suggestion: Use owned (^) to take ownership instead of borrowing, "
        "or ensure the reference doesn't escape its scope",
        loc
    );
}

void OwnershipAnalyzer::reportInvalidCapture(const std::string& varName,
                                             const std::string& reason,
                                             const Common::SourceLocation& loc) {
    OwnershipViolation violation;
    violation.kind = OwnershipViolation::Kind::InvalidCapture;
    violation.variableName = varName;
    violation.useLocation = loc;
    violation.message = "Invalid capture of '" + varName + "': " + reason;

    result_.violations.push_back(violation);

    errorReporter_.reportError(
        Common::ErrorCode::InvalidReference,
        violation.message,
        loc
    );

    // Add suggested fix based on the reason
    if (reason.find("moved") != std::string::npos) {
        errorReporter_.reportNote(
            "Suggestion: Capture by reference (&) instead of by value, "
            "or clone the value before the lambda",
            loc
        );
    } else if (reason.find("reference") != std::string::npos || reason.find("borrow") != std::string::npos) {
        errorReporter_.reportNote(
            "Suggestion: Capture by value (^) instead of by reference to extend the lifetime, "
            "or ensure the lambda doesn't outlive the referenced value",
            loc
        );
    } else {
        errorReporter_.reportNote(
            "Suggestion: Review the lambda capture list and ensure ownership rules are honored",
            loc
        );
    }
}

} // namespace Semantic
} // namespace XXML
