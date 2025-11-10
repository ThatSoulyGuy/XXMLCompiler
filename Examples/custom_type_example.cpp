/**
 * @file custom_type_example.cpp
 * @brief Example: Registering custom types and operators in XXML compiler
 *
 * This example demonstrates how to extend the XXML compiler with:
 * - Custom types (Vector3)
 * - Custom operators (dot product)
 * - Multiple backends (C++20 and LLVM)
 *
 * Compile:
 *   g++ -std=c++20 -I../include custom_type_example.cpp -o custom_example
 */

#include <XXML.h>
#include <iostream>
#include <format>

int main() {
    std::cout << "=== XXML Compiler Extension Example ===\n\n";

    // 1. Create compilation context
    XXML::Core::CompilerConfig config;
    config.strictTypeChecking = true;
    config.enableOptimizations = true;

    XXML::Core::CompilationContext context(config);

    std::cout << "✅ Created compilation context\n";
    std::cout << std::format("   - Types registered: {}\n", context.types().size());
    std::cout << std::format("   - Operators registered: {}\n",
                            context.operators().getAllBinaryOperators().size() +
                            context.operators().getAllUnaryOperators().size());

    // 2. Register custom type: Vector3
    std::cout << "\n📝 Registering custom type 'Vector3'...\n";

    XXML::Core::TypeInfo vector3Type;
    vector3Type.xxmlName = "Vector3";
    vector3Type.cppType = "glm::vec3";           // C++ type
    vector3Type.llvmType = "<3 x float>";        // LLVM type
    vector3Type.category = XXML::Core::TypeCategory::Class;
    vector3Type.ownership = XXML::Core::OwnershipSemantics::Value;
    vector3Type.isBuiltin = false;

    // Add methods
    XXML::Core::TypeInfo::MethodInfo lengthMethod;
    lengthMethod.name = "length";
    lengthMethod.returnType = "Float";
    lengthMethod.isStatic = false;
    lengthMethod.isConst = true;
    vector3Type.methods.push_back(lengthMethod);

    // Add constructor
    XXML::Core::TypeInfo::ConstructorInfo ctor;
    ctor.parameterTypes = {"Float", "Float", "Float"};
    ctor.cppInitializer = "glm::vec3($1, $2, $3)";
    vector3Type.constructors.push_back(ctor);

    context.types().registerType(vector3Type);
    std::cout << "   ✅ Vector3 type registered\n";

    // 3. Register custom operator: dot product
    std::cout << "\n📝 Registering custom operator 'dot'...\n";

    XXML::Core::BinaryOperatorInfo dotOp;
    dotOp.symbol = "dot";
    dotOp.precedence = XXML::Core::OperatorPrecedence::Multiplicative;
    dotOp.associativity = XXML::Core::Associativity::Left;
    dotOp.returnType = "Float";
    dotOp.isArithmetic = true;

    // Custom C++ code generator
    dotOp.cppGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format("glm::dot({}, {})", lhs, rhs);
    };

    // Custom LLVM code generator
    dotOp.llvmGenerator = [](std::string_view lhs, std::string_view rhs) {
        return std::format(
            "%dp = call float @llvm.vector.reduce.fadd.v3f32(<3 x float> "
            "%mul_{}_{})",
            lhs, rhs
        );
    };

    context.operators().registerBinaryOperator(dotOp);
    std::cout << "   ✅ 'dot' operator registered\n";

    // 4. Register pipe operator |>
    std::cout << "\n📝 Registering pipe operator '|>'...\n";

    context.operators().registerBinaryOperatorWithGenerator(
        "|>",
        XXML::Core::OperatorPrecedence::Additive,
        XXML::Core::Associativity::Left,
        [](std::string_view lhs, std::string_view rhs) {
            return std::format("{}({})", rhs, lhs);  // Reverse application
        }
    );
    std::cout << "   ✅ Pipe operator '|>' registered\n";

    // 5. Query registered backends
    std::cout << "\n🔧 Available backends:\n";
    for (const auto& backendName : context.backends().getAllBackendNames()) {
        auto* backend = context.backends().getBackend(backendName);
        if (backend) {
            std::cout << std::format("   - {} (version {})\n",
                                    backend->targetName(),
                                    backend->version());

            // Show capabilities
            auto caps = backend->getCapabilities();
            if (!caps.empty()) {
                std::cout << "     Capabilities: ";
                for (size_t i = 0; i < caps.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << static_cast<int>(caps[i]);
                }
                std::cout << "\n";
            }
        }
    }

    // 6. Select backend and generate code
    std::cout << "\n🎯 Selecting C++20 backend...\n";
    context.setActiveBackend(XXML::Core::BackendTarget::Cpp20);

    auto* activeBackend = context.getActiveBackend();
    if (activeBackend) {
        std::cout << std::format("   ✅ Active backend: {}\n",
                                activeBackend->targetName());
    }

    // 7. Demonstrate type lookups
    std::cout << "\n🔍 Type system queries:\n";

    // Check if our custom type is registered
    if (context.types().isRegistered("Vector3")) {
        const auto* info = context.types().getTypeInfo("Vector3");
        std::cout << std::format("   - Vector3: C++ type = {}, LLVM type = {}\n",
                                info->cppType, info->llvmType);
    }

    // Check built-in types
    if (context.types().isRegistered("Integer")) {
        const auto* info = context.types().getTypeInfo("Integer");
        std::cout << std::format("   - Integer: builtin = {}, primitive = {}\n",
                                info->isBuiltin, info->isPrimitive());
    }

    // 8. Operator queries
    std::cout << "\n⚙️  Operator queries:\n";

    if (context.operators().isBinaryOperator("dot")) {
        auto dotInfo = context.operators().getBinaryOperator("dot");
        if (dotInfo) {
            std::cout << std::format("   - 'dot' operator: precedence = {}\n",
                                    dotInfo->precedence);
        }
    }

    // 9. Test operator code generation
    std::cout << "\n💻 Code generation test:\n";

    std::string dotCode = context.operators().generateBinaryCpp(
        "dot", "vec1", "vec2"
    );
    std::cout << std::format("   - C++: {}\n", dotCode);

    std::string pipeCode = context.operators().generateBinaryCpp(
        "|>", "value", "transform"
    );
    std::cout << std::format("   - Pipe: {}\n", pipeCode);

    // 10. Display statistics
    std::cout << "\n📊 Compilation Context Statistics:\n";
    auto stats = context.getStats();
    std::cout << std::format("   - Types registered: {}\n", stats.typesRegistered);
    std::cout << std::format("   - Operators registered: {}\n", stats.operatorsRegistered);
    std::cout << std::format("   - Backends available: {}\n", stats.backendsRegistered);
    std::cout << std::format("   - Errors: {}\n", stats.errorsReported);
    std::cout << std::format("   - Warnings: {}\n", stats.warningsReported);

    // 11. Demonstrate thread-safe compilation
    std::cout << "\n🧵 Thread safety demonstration:\n";
    std::cout << "   Each thread can have its own CompilationContext\n";
    std::cout << "   All registries use std::mutex for protection\n";
    std::cout << "   ✅ Safe for parallel compilation\n";

    std::cout << "\n=== Example Complete ===\n";
    std::cout << "\nNext steps:\n";
    std::cout << "1. Create XXML source files using Vector3 type\n";
    std::cout << "2. Compile with: context.generate(program)\n";
    std::cout << "3. Switch backends to generate LLVM IR\n";
    std::cout << "4. Extend with more custom types and operators\n";

    return 0;
}
