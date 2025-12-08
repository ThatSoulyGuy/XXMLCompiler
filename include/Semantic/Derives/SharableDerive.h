#pragma once

#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

/**
 * @brief Derive handler for the Sharable marker trait
 *
 * Sharable is a marker trait that indicates a type can be safely shared
 * (referenced) across threads. This derive handler:
 * - Does NOT generate any methods (marker trait)
 * - Validates that the class meets Sharable requirements:
 *   - All fields must be of Sharable types
 *   - Immutable types are Sharable
 *   - Atomic<T>, Mutex, sync primitives are Sharable
 *
 * Example:
 *   @Derive(trait = "Sharable")
 *   [ Class <SharedConfig> Final Extends None
 *       [ Public <>
 *           Property <maxThreads> Types Integer^;
 *           Property <appName> Types String^;
 *       ]
 *   ]
 */
class SharableDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Sharable"; }

    /**
     * @brief Generate for Sharable - returns empty result (marker trait)
     */
    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    /**
     * @brief Check if class can derive Sharable
     *
     * Returns error if:
     * - Class has fields of non-Sharable types
     */
    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
