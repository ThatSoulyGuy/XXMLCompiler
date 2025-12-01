#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Parser/AST.h"

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Base class for statement code generation
 *
 * Provides dispatch mechanism for generating IR for all statement types.
 */
class StmtCodegen {
public:
    StmtCodegen(CodegenContext& ctx, ExprCodegen& exprCodegen)
        : ctx_(ctx), exprCodegen_(exprCodegen) {}
    virtual ~StmtCodegen() = default;

    // Main dispatch
    void generate(Parser::Statement* stmt);

    // === Statement Visitors ===

    // AssignmentCodegen.cpp
    virtual void visitAssignment(Parser::AssignmentStmt* stmt);

    // InstantiateCodegen.cpp
    virtual void visitInstantiate(Parser::InstantiateStmt* stmt);

    // ControlFlowCodegen.cpp
    virtual void visitIf(Parser::IfStmt* stmt);
    virtual void visitWhile(Parser::WhileStmt* stmt);
    virtual void visitBreak(Parser::BreakStmt* stmt);
    virtual void visitContinue(Parser::ContinueStmt* stmt);

    // ForLoopCodegen.cpp
    virtual void visitFor(Parser::ForStmt* stmt);

    // ReturnCodegen.cpp
    virtual void visitReturn(Parser::ReturnStmt* stmt);
    virtual void visitExit(Parser::ExitStmt* stmt);

    // RunStmt (expression statement)
    virtual void visitRun(Parser::RunStmt* stmt);

protected:
    CodegenContext& ctx_;
    ExprCodegen& exprCodegen_;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
