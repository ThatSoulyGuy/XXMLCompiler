#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

void StmtCodegen::generate(Parser::Statement* stmt) {
    if (!stmt) return;

    // Dispatch based on statement type
    if (auto* assign = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        visitAssignment(assign);
    } else if (auto* inst = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        visitInstantiate(inst);
    } else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        visitIf(ifStmt);
    } else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        visitWhile(whileStmt);
    } else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        visitFor(forStmt);
    } else if (auto* breakStmt = dynamic_cast<Parser::BreakStmt*>(stmt)) {
        visitBreak(breakStmt);
    } else if (auto* contStmt = dynamic_cast<Parser::ContinueStmt*>(stmt)) {
        visitContinue(contStmt);
    } else if (auto* retStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        visitReturn(retStmt);
    } else if (auto* exitStmt = dynamic_cast<Parser::ExitStmt*>(stmt)) {
        visitExit(exitStmt);
    } else if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        visitRun(runStmt);
    }
    // Other statement types can be added here
}

// Default implementations

void StmtCodegen::visitAssignment(Parser::AssignmentStmt*) {}
void StmtCodegen::visitInstantiate(Parser::InstantiateStmt*) {}
void StmtCodegen::visitIf(Parser::IfStmt*) {}
void StmtCodegen::visitWhile(Parser::WhileStmt*) {}
void StmtCodegen::visitFor(Parser::ForStmt*) {}
void StmtCodegen::visitBreak(Parser::BreakStmt*) {}
void StmtCodegen::visitContinue(Parser::ContinueStmt*) {}
void StmtCodegen::visitReturn(Parser::ReturnStmt*) {}
void StmtCodegen::visitExit(Parser::ExitStmt*) {}
void StmtCodegen::visitRun(Parser::RunStmt*) {}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
