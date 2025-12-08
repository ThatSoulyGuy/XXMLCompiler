#pragma once

#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

/**
 * @brief Derive handler for the Equatable trait
 *
 * Generates an equals() method that performs structural equality comparison
 * of all public properties in the class.
 *
 * Example:
 *   @Derive(trait = "Equatable")
 *   [ Class <Point> Final Extends None
 *       [ Public <>
 *           Property <x> Types Integer^;
 *           Property <y> Types Integer^;
 *       ]
 *   ]
 *
 * Generates:
 *   Method <equals> Returns Bool^ Parameters (Parameter <other> Types Point&) Do {
 *       If (x.equals(other.x).not()) -> { Return Bool::Constructor(false); }
 *       If (y.equals(other.y).not()) -> { Return Bool::Constructor(false); }
 *       Return Bool::Constructor(true);
 *   }
 */
class EqDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Equatable"; }

    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

private:
    /**
     * @brief Generate equality check for a single property
     */
    std::unique_ptr<Parser::Expression> generatePropertyEquals(
        Parser::PropertyDecl* prop,
        SemanticAnalyzer& analyzer);

    /**
     * @brief Check if a type has an equals method
     */
    bool hasEqualsMethod(const std::string& typeName, SemanticAnalyzer& analyzer);
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
