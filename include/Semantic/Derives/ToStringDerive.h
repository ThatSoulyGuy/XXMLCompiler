#pragma once

#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

/**
 * @brief Derive handler for the Stringable trait
 *
 * Generates a toString() method that produces a string representation
 * of all public properties in the class.
 *
 * Example:
 *   @Derive(trait = "Stringable")
 *   [ Class <Person> Extends None
 *       [ Public <>
 *           Property <name> Types String^;
 *           Property <age> Types Integer^;
 *       ]
 *   ]
 *
 * Generates:
 *   Method <toString> Returns String^ Parameters () Do {
 *       Return String::Constructor("Person{name=").concat(name.toString())
 *           .concat(String::Constructor(", age=")).concat(age.toString())
 *           .concat(String::Constructor("}"));
 *   }
 */
class ToStringDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Stringable"; }

    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

private:
    /**
     * @brief Generate the expression for converting a property to string
     *
     * For String types: returns the property directly
     * For other types: calls property.toString()
     */
    std::unique_ptr<Parser::Expression> generatePropertyToString(
        Parser::PropertyDecl* prop,
        SemanticAnalyzer& analyzer);

    /**
     * @brief Check if a type has a toString method
     */
    bool hasToStringMethod(const std::string& typeName, SemanticAnalyzer& analyzer);
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
