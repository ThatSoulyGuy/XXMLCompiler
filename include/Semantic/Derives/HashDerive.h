#pragma once

#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

/**
 * @brief Derive handler for the Hashable trait
 *
 * Generates a hash() method that computes a hash code from all public properties.
 * Uses a simple combining algorithm: hash = hash * 31 + prop.hash()
 *
 * Example:
 *   @Derive(trait = "Hashable")
 *   [ Class <Point> Final Extends None
 *       [ Public <>
 *           Property <x> Types Integer^;
 *           Property <y> Types Integer^;
 *       ]
 *   ]
 *
 * Generates:
 *   Method <hash> Returns NativeType<"int64">^ Parameters () Do {
 *       Instantiate NativeType<"int64">^ As <result> = 17;
 *       Set result = result * 31 + x.hash();
 *       Set result = result * 31 + y.hash();
 *       Return result;
 *   }
 */
class HashDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Hashable"; }

    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

private:
    /**
     * @brief Check if a type has a hash method
     */
    bool hasHashMethod(const std::string& typeName, SemanticAnalyzer& analyzer);
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
