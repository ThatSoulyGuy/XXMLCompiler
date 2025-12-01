#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

// Specialized DeclCodegen for class declarations
class ClassCodegenImpl : public DeclCodegen {
public:
    using DeclCodegen::DeclCodegen;

    void visitClass(Parser::ClassDecl* decl) override {
        if (!decl) return;

        // Skip template declarations (only generate code for instantiations)
        if (!decl->templateParams.empty() && decl->name.find('<') == std::string::npos) {
            return;
        }

        // Check for duplicate class generation
        std::string className = std::string(ctx_.currentNamespace());
        if (!className.empty()) {
            className += "::";
        }
        className += decl->name;

        if (ctx_.isClassGenerated(className)) {
            return;
        }
        ctx_.markClassGenerated(className);

        // Set current class context
        ctx_.setCurrentClassName(className);

        // Collect properties and create ClassInfo
        ClassInfo classInfo;
        classInfo.name = className;
        classInfo.mangledName = ctx_.mangleTypeName(className);

        // First pass: collect properties
        size_t propIndex = 0;
        for (auto& section : decl->sections) {
            if (!section) continue;
            for (auto& memberDecl : section->declarations) {
                if (!memberDecl) continue;
                if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(memberDecl.get())) {
                    PropertyInfo propInfo;
                    propInfo.name = prop->name;
                    propInfo.xxmlType = prop->type ? prop->type->typeName : "ptr";
                    propInfo.llvmType = ctx_.getLLVMTypeString(propInfo.xxmlType);
                    propInfo.index = propIndex++;
                    classInfo.properties.push_back(propInfo);
                }
            }
        }

        // Create struct type for the class
        std::vector<LLVMIR::Type*> fieldTypes;
        for (const auto& prop : classInfo.properties) {
            fieldTypes.push_back(ctx_.mapType(prop.xxmlType));
        }
        if (fieldTypes.empty()) {
            fieldTypes.push_back(ctx_.module().getContext().getInt8Ty());
        }

        auto* structType = ctx_.module().createStruct("class." + classInfo.mangledName);
        classInfo.structType = structType;
        classInfo.instanceSize = fieldTypes.size() * 8; // Approximate

        // Register class info
        ctx_.registerClass(className, classInfo);

        // Second pass: generate methods and constructors
        for (auto& section : decl->sections) {
            if (!section) continue;
            for (auto& memberDecl : section->declarations) {
                if (!memberDecl) continue;

                if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(memberDecl.get())) {
                    visitConstructor(ctor);
                } else if (auto* dtor = dynamic_cast<Parser::DestructorDecl*>(memberDecl.get())) {
                    visitDestructor(dtor);
                } else if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                    visitMethod(method);
                }
            }
        }

        // Clear class context
        ctx_.setCurrentClassName("");
    }

    void visitNativeStruct(Parser::NativeStructureDecl* decl) override {
        if (!decl) return;

        // Native structs are typically opaque types
        std::string typeName = ctx_.mangleTypeName(decl->name);
        ctx_.module().createStruct("native." + typeName);
    }
};

// Factory function
std::unique_ptr<DeclCodegen> createClassCodegen(CodegenContext& ctx,
                                                 ExprCodegen& exprCodegen,
                                                 StmtCodegen& stmtCodegen) {
    return std::make_unique<ClassCodegenImpl>(ctx, exprCodegen, stmtCodegen);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
