#pragma once

#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

/**
 * @brief Derive handler for the Sendable marker trait
 *
 * Sendable is a marker trait that indicates a type can be safely moved
 * across thread boundaries. This derive handler:
 * - Does NOT generate any methods (marker trait)
 * - Validates that the class meets Sendable requirements:
 *   - No reference (&) fields (would become dangling)
 *   - All owned (^) fields must be of Sendable types
 *   - Copy (%) fields are implicitly sendable
 *
 * Example:
 *   @Derive(trait = "Sendable")
 *   [ Class <ThreadMessage> Final Extends None
 *       [ Public <>
 *           Property <data> Types String^;
 *           Property <count> Types Integer^;
 *       ]
 *   ]
 */
class SendableDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Sendable"; }

    /**
     * @brief Generate for Sendable - returns empty result (marker trait)
     */
    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    /**
     * @brief Check if class can derive Sendable
     *
     * Returns error if:
     * - Class has any reference (&) fields
     * - Class has owned fields of non-Sendable types
     */
    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
