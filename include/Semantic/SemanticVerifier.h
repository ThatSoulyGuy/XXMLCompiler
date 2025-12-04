#pragma once
#include <string>
#include <vector>
#include <functional>
#include "SemanticAnalyzer.h"
#include "PassResults.h"
#include "SemanticError.h"
#include "../Parser/AST.h"

namespace XXML {
namespace Semantic {

// Forward declarations
class TypeCanonicalizer;
class TemplateExpander;
class OwnershipAnalyzer;
class LayoutComputer;
class ABILowering;

//==============================================================================
// SEMANTIC VERIFIER
//
// Comprehensive verification pass to ensure all semantic invariants are
// satisfied BEFORE IR generation. This is the final gate that prevents
// ambiguous or incomplete semantic information from reaching codegen.
//
// INVARIANTS ENFORCED:
// 1. Type Resolution: No Unknown types, all forward refs resolved
// 2. Template Expansion: All templates fully instantiated with concrete types
// 3. Ownership: No use-after-move, no double-move, no dangling refs
// 4. Layout: All class layouts computed with valid offsets/alignment
// 5. ABI: All FFI signatures complete with valid calling conventions
// 6. Control Flow: All blocks terminated, no unreachable code
//==============================================================================

class SemanticVerifier {
public:
    //==========================================================================
    // VERIFICATION RESULT
    //==========================================================================

    struct VerificationResult {
        bool success = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        // Counts by category
        int typeErrors = 0;
        int templateErrors = 0;
        int ownershipErrors = 0;
        int layoutErrors = 0;
        int abiErrors = 0;
        int controlFlowErrors = 0;

        void addError(const std::string& error) {
            success = false;
            errors.push_back(error);
        }

        void addError(const std::string& category, const std::string& error) {
            success = false;
            errors.push_back("[" + category + "] " + error);

            if (category == "TYPE") typeErrors++;
            else if (category == "TEMPLATE") templateErrors++;
            else if (category == "OWNERSHIP") ownershipErrors++;
            else if (category == "LAYOUT") layoutErrors++;
            else if (category == "ABI") abiErrors++;
            else if (category == "CONTROL_FLOW") controlFlowErrors++;
        }

        void addWarning(const std::string& warning) {
            warnings.push_back(warning);
        }

        std::string getSummary() const {
            if (success) return "Verification passed";
            return "Verification failed: " + std::to_string(errors.size()) + " errors, " +
                   std::to_string(warnings.size()) + " warnings";
        }

        std::string getDetailedSummary() const {
            std::string summary = getSummary() + "\n";
            if (typeErrors > 0) summary += "  Type errors: " + std::to_string(typeErrors) + "\n";
            if (templateErrors > 0) summary += "  Template errors: " + std::to_string(templateErrors) + "\n";
            if (ownershipErrors > 0) summary += "  Ownership errors: " + std::to_string(ownershipErrors) + "\n";
            if (layoutErrors > 0) summary += "  Layout errors: " + std::to_string(layoutErrors) + "\n";
            if (abiErrors > 0) summary += "  ABI errors: " + std::to_string(abiErrors) + "\n";
            if (controlFlowErrors > 0) summary += "  Control flow errors: " + std::to_string(controlFlowErrors) + "\n";
            return summary;
        }
    };

    //==========================================================================
    // VERIFICATION MODE
    //==========================================================================

    enum class Mode {
        Strict,     // All errors are fatal (default for codegen)
        Permissive, // Errors become warnings where possible
        Report      // Just collect and report, don't fail
    };

    //==========================================================================
    // MAIN ENTRY POINTS
    //==========================================================================

    /**
     * Verify all semantic invariants before IR generation.
     * This is the primary entry point - call this before codegen.
     * Default mode is Permissive to allow cross-module Unknown types.
     */
    static VerificationResult verify(
        SemanticAnalyzer& analyzer,
        Parser::Program& program,
        Mode mode = Mode::Strict);

    /**
     * Verify using the new pass results (preferred for new pipeline)
     */
    static VerificationResult verifyWithPassResults(
        const TypeResolutionResult& typeResult,
        const TemplateExpansionResult& templateResult,
        const SemanticValidationResult& semanticResult,
        const OwnershipAnalysisResult& ownershipResult,
        const LayoutComputationResult& layoutResult,
        const ABILoweringResult& abiResult,
        Mode mode = Mode::Strict);

    /**
     * Quick pre-check before codegen - throws on first error
     */
    static void assertAllInvariantsOrThrow(
        SemanticAnalyzer& analyzer,
        Parser::Program& program);

    //==========================================================================
    // TYPE RESOLUTION INVARIANTS
    //==========================================================================

    /**
     * INVARIANT: No expressions have "Unknown" type
     * In strict mode, Unknown types are errors, not warnings.
     */
    static bool verifyNoUnknownTypes(
        SemanticAnalyzer& analyzer,
        VerificationResult& result,
        Mode mode = Mode::Strict);

    /**
     * INVARIANT: All forward references are resolved
     */
    static bool verifyNoForwardReferences(
        const TypeResolutionResult& typeResult,
        VerificationResult& result);

    /**
     * INVARIANT: All types have ownership annotations (except template params)
     */
    static bool verifyOwnershipAnnotations(
        SemanticAnalyzer& analyzer,
        Parser::Program& program,
        VerificationResult& result);

    /**
     * INVARIANT: No circular type dependencies
     */
    static bool verifyNoCircularTypes(
        const TypeResolutionResult& typeResult,
        VerificationResult& result);

    //==========================================================================
    // TEMPLATE INVARIANTS
    //==========================================================================

    /**
     * INVARIANT: All referenced classes exist in the registry
     */
    static bool verifyAllClassesResolved(
        SemanticAnalyzer& analyzer,
        VerificationResult& result);

    /**
     * INVARIANT: All template instantiations have concrete type arguments
     */
    static bool verifyAllTemplatesResolved(
        SemanticAnalyzer& analyzer,
        VerificationResult& result);

    /**
     * INVARIANT: All template constraints are satisfied
     */
    static bool verifyConstraintsSatisfied(
        const TemplateExpansionResult& templateResult,
        VerificationResult& result);

    /**
     * INVARIANT: No unexpanded generic types in codegen input
     */
    static bool verifyNoUnexpandedGenerics(
        SemanticAnalyzer& analyzer,
        VerificationResult& result);

    //==========================================================================
    // OWNERSHIP INVARIANTS
    //==========================================================================

    /**
     * INVARIANT: No use-after-move violations
     */
    static bool verifyNoUseAfterMove(
        const OwnershipAnalysisResult& ownershipResult,
        VerificationResult& result);

    /**
     * INVARIANT: No double-move violations
     */
    static bool verifyNoDoubleMove(
        const OwnershipAnalysisResult& ownershipResult,
        VerificationResult& result);

    /**
     * INVARIANT: All references have valid lifetimes
     */
    static bool verifyReferencesValid(
        const OwnershipAnalysisResult& ownershipResult,
        VerificationResult& result);

    /**
     * INVARIANT: Lambda captures honor ^, &, % rules
     */
    static bool verifyCapturesValid(
        const OwnershipAnalysisResult& ownershipResult,
        VerificationResult& result);

    //==========================================================================
    // LAYOUT INVARIANTS
    //==========================================================================

    /**
     * INVARIANT: All class layouts are computed
     */
    static bool verifyAllLayoutsComputed(
        const LayoutComputationResult& layoutResult,
        SemanticAnalyzer& analyzer,
        VerificationResult& result);

    /**
     * INVARIANT: Reflection metadata matches computed layouts
     */
    static bool verifyReflectionMatches(
        const LayoutComputationResult& layoutResult,
        VerificationResult& result);

    /**
     * INVARIANT: All alignments are valid (power of 2)
     */
    static bool verifyAlignmentValid(
        const LayoutComputationResult& layoutResult,
        VerificationResult& result);

    /**
     * INVARIANT: Field offsets are properly ordered
     */
    static bool verifyFieldOffsets(
        const LayoutComputationResult& layoutResult,
        VerificationResult& result);

    //==========================================================================
    // ABI/FFI INVARIANTS
    //==========================================================================

    /**
     * INVARIANT: All FFI signatures are complete
     */
    static bool verifyFFISignaturesComplete(
        const ABILoweringResult& abiResult,
        VerificationResult& result);

    /**
     * INVARIANT: All calling conventions are valid for platform
     */
    static bool verifyCallingConventionsValid(
        const ABILoweringResult& abiResult,
        VerificationResult& result);

    /**
     * INVARIANT: All marshaling strategies are defined
     */
    static bool verifyMarshalingStrategies(
        const ABILoweringResult& abiResult,
        VerificationResult& result);

    //==========================================================================
    // CONTROL FLOW INVARIANTS (checked during AST walk)
    //==========================================================================

    /**
     * INVARIANT: All methods have proper return statements
     */
    static bool verifyAllMethodsReturn(
        Parser::Program& program,
        SemanticAnalyzer& analyzer,
        VerificationResult& result);

    /**
     * INVARIANT: Break/continue only inside loops
     */
    static bool verifyBreakContinueValid(
        Parser::Program& program,
        VerificationResult& result);

private:
    // Helper for AST traversal
    class ControlFlowVerifier;
};

} // namespace Semantic
} // namespace XXML
