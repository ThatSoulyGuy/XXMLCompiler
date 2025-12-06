#include "Backends/Codegen/ReflectionCodegen/ReflectionCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

ReflectionCodegen::ReflectionCodegen(CodegenContext& ctx)
    : ctx_(ctx) {
    // Initialize the type-safe builders
    globalBuilder_ = std::make_unique<LLVMIR::GlobalBuilder>(ctx_.module());
    metadataBuilder_ = std::make_unique<LLVMIR::MetadataBuilder>(ctx_.module(), *globalBuilder_);
}

LLVMIR::OwnershipKind ReflectionCodegen::parseOwnership(const std::string& ownership) {
    if (ownership.empty()) return LLVMIR::OwnershipKind::Unknown;
    char c = ownership[0];
    if (c == '^') return LLVMIR::OwnershipKind::Owned;
    if (c == '&') return LLVMIR::OwnershipKind::Reference;
    if (c == '%') return LLVMIR::OwnershipKind::Copy;
    return LLVMIR::OwnershipKind::Unknown;
}

LLVMIR::ReflectionClassInfo ReflectionCodegen::convertMetadata(const ReflectionClassMetadata& metadata) const {
    LLVMIR::ReflectionClassInfo info;

    // Basic info
    info.name = metadata.name;
    info.namespaceName = metadata.namespaceName;
    info.fullName = metadata.fullName;
    info.isTemplate = metadata.isTemplate;
    info.instanceSize = static_cast<int64_t>(metadata.instanceSize);
    info.templateParams = metadata.templateParams;

    // Convert properties
    for (size_t i = 0; i < metadata.properties.size(); ++i) {
        LLVMIR::ReflectionProperty prop;
        prop.name = metadata.properties[i].first;
        prop.typeName = metadata.properties[i].second;
        prop.ownership = i < metadata.propertyOwnerships.size()
            ? parseOwnership(metadata.propertyOwnerships[i])
            : LLVMIR::OwnershipKind::Unknown;
        prop.offset = static_cast<int64_t>(i * 8);  // Simple offset calculation
        info.properties.push_back(std::move(prop));
    }

    // Convert methods
    for (size_t m = 0; m < metadata.methods.size(); ++m) {
        LLVMIR::ReflectionMethod method;
        method.name = metadata.methods[m].first;
        method.returnTypeName = metadata.methods[m].second;
        method.returnOwnership = m < metadata.methodReturnOwnerships.size()
            ? parseOwnership(metadata.methodReturnOwnerships[m])
            : LLVMIR::OwnershipKind::Unknown;
        method.isStatic = false;  // TODO: Get from AST if needed
        method.isConstructor = (method.name == "Constructor");
        method.funcPtr = nullptr;  // TODO: Set actual function pointer if needed

        // Convert parameters
        if (m < metadata.methodParameters.size()) {
            for (const auto& [paramName, paramType, paramOwn] : metadata.methodParameters[m]) {
                LLVMIR::ReflectionParameter param;
                param.name = paramName;
                param.typeName = paramType;
                param.ownership = parseOwnership(paramOwn);
                method.parameters.push_back(std::move(param));
            }
        }

        info.methods.push_back(std::move(method));
    }

    return info;
}

void ReflectionCodegen::generate() {
    const auto& reflectionMetadata = ctx_.reflectionMetadata();
    if (reflectionMetadata.empty()) {
        return;
    }

    // Initialize reflection struct types in the module
    metadataBuilder_->initializeReflectionTypes();

    // Generate metadata for each class using the type-safe MetadataBuilder
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        // Convert from CodegenContext's format to MetadataBuilder's format
        LLVMIR::ReflectionClassInfo classInfo = convertMetadata(metadata);

        // Generate the metadata using MetadataBuilder
        // The result contains GlobalVariable* pointers that are already added to the Module
        metadataBuilder_->generateClassMetadata(classInfo);
    }

    // All metadata is now in the Module and will be emitted via LLVMEmitter
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
