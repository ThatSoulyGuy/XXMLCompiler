#include "Backends/Codegen/MetadataGen/MetadataGen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

MetadataGen::MetadataGen(CodegenContext& ctx)
    : ctx_(ctx) {}

int32_t MetadataGen::ownershipToInt(const std::string& ownership) {
    if (ownership.empty()) return 0;
    char c = ownership[0];
    if (c == '^') return 1;  // Owned
    if (c == '&') return 2;  // Reference
    if (c == '%') return 3;  // Copy
    return 0;  // Unknown
}

std::string MetadataGen::mangleName(const std::string& name) {
    std::string mangled = name;
    for (char& c : mangled) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ') {
            c = '_';
        }
    }
    return mangled;
}

void MetadataGen::registerClass(Parser::ClassDecl* classDecl, const std::string& namespaceName) {
    if (!classDecl) return;

    ReflectionMetadata meta;
    meta.name = classDecl->name;
    meta.namespaceName = namespaceName;
    meta.fullName = namespaceName.empty() ? classDecl->name : namespaceName + "::" + classDecl->name;
    meta.isTemplate = !classDecl->templateParams.empty();

    // Collect template parameters
    for (const auto& tp : classDecl->templateParams) {
        meta.templateParams.push_back(tp.name);
    }

    // Collect properties and methods from class sections
    for (const auto& section : classDecl->sections) {
        if (!section) continue;

        for (const auto& decl : section->declarations) {
            if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                meta.properties.emplace_back(propDecl->name, propDecl->type->typeName);
                std::string ownership;
                if (!propDecl->type->typeName.empty()) {
                    char lastChar = propDecl->type->typeName.back();
                    if (lastChar == '^' || lastChar == '&' || lastChar == '%') {
                        ownership = std::string(1, lastChar);
                    }
                }
                meta.propertyOwnerships.push_back(ownership);
            }
            else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                std::string returnType = methodDecl->returnType ? methodDecl->returnType->typeName : "None";
                meta.methods.emplace_back(methodDecl->name, returnType);

                std::string returnOwn;
                if (!returnType.empty()) {
                    char lastChar = returnType.back();
                    if (lastChar == '^' || lastChar == '&' || lastChar == '%') {
                        returnOwn = std::string(1, lastChar);
                    }
                }
                meta.methodReturnOwnerships.push_back(returnOwn);

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : methodDecl->parameters) {
                    std::string paramType = param->type->typeName;
                    std::string paramOwn;
                    if (!paramType.empty()) {
                        char lastChar = paramType.back();
                        if (lastChar == '^' || lastChar == '&' || lastChar == '%') {
                            paramOwn = std::string(1, lastChar);
                        }
                    }
                    params.emplace_back(param->name, paramType, paramOwn);
                }
                meta.methodParameters.push_back(std::move(params));
            }
            else if (auto* ctorDecl = dynamic_cast<Parser::ConstructorDecl*>(decl.get())) {
                meta.methods.emplace_back("Constructor", meta.name);
                meta.methodReturnOwnerships.push_back("^");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : ctorDecl->parameters) {
                    std::string paramType = param->type->typeName;
                    std::string paramOwn;
                    if (!paramType.empty()) {
                        char lastChar = paramType.back();
                        if (lastChar == '^' || lastChar == '&' || lastChar == '%') {
                            paramOwn = std::string(1, lastChar);
                        }
                    }
                    params.emplace_back(param->name, paramType, paramOwn);
                }
                meta.methodParameters.push_back(std::move(params));
            }
        }
    }

    meta.instanceSize = static_cast<int64_t>(meta.properties.size() * 8);
    metadata_[meta.fullName] = std::move(meta);
}

const ReflectionMetadata* MetadataGen::getMetadata(const std::string& fullName) const {
    auto it = metadata_.find(fullName);
    return it != metadata_.end() ? &it->second : nullptr;
}

LLVMIR::PtrValue MetadataGen::getOrCreateString(const std::string& content, const std::string& prefix) {
    auto it = stringLabelMap_.find(content);
    if (it != stringLabelMap_.end()) {
        // Return a new global string - the label is tracked but we create new each time
        // A more complete implementation would cache the actual global
    }

    std::string label = prefix + std::to_string(stringCounter_++);
    stringLabelMap_[content] = label;
    return ctx_.builder().createGlobalString(content, "str." + label);
}

void MetadataGen::generateStructTypes() {
    // Reflection struct types are created via the legacy string emitter
    // The IR module creation is a future enhancement
}

void MetadataGen::generate() {
    if (metadata_.empty()) return;

    generateStructTypes();

    for (const auto& [fullName, meta] : metadata_) {
        std::string mangledName = mangleName(fullName);
        generateParameterArrays(mangledName, meta);
        generatePropertyArray(mangledName, meta);
        generateMethodArray(mangledName, meta);
        generateTypeInfo(mangledName, meta);
    }

    generateInitFunction();
}

void MetadataGen::generatePropertyArray(const std::string& /*mangledName*/, const ReflectionMetadata& /*meta*/) {
    // Placeholder - full implementation would create global arrays
}

void MetadataGen::generateMethodArray(const std::string& /*mangledName*/, const ReflectionMetadata& /*meta*/) {
    // Placeholder - full implementation would create global arrays
}

void MetadataGen::generateParameterArrays(const std::string& /*mangledName*/, const ReflectionMetadata& /*meta*/) {
    // Placeholder - full implementation would create global arrays
}

void MetadataGen::generateTypeInfo(const std::string& /*mangledName*/, const ReflectionMetadata& /*meta*/) {
    // Placeholder - full implementation would create ReflectionTypeInfo global
}

void MetadataGen::generateInitFunction() {
    if (metadata_.empty()) return;

    auto& mod = ctx_.module();
    auto& llvmCtx = mod.getContext();

    // Create __reflection_init function
    auto* funcType = llvmCtx.getFunctionTy(llvmCtx.getVoidTy(), {}, false);
    auto* initFunc = mod.createFunction(funcType, "__reflection_init", LLVMIR::Function::Linkage::Internal);
    if (!initFunc) return;

    auto* entryBlock = initFunc->createBasicBlock("entry");
    ctx_.builder().setInsertPoint(entryBlock);

    // Get or declare Reflection_registerType
    auto* regFunc = mod.getFunction("Reflection_registerType");
    if (!regFunc) {
        auto* regFuncType = llvmCtx.getFunctionTy(llvmCtx.getVoidTy(), {llvmCtx.getPtrTy()}, false);
        regFunc = mod.createFunction(regFuncType, "Reflection_registerType", LLVMIR::Function::Linkage::External);
    }

    // For now, just create an empty init function
    // Full implementation would call Reflection_registerType for each type

    ctx_.builder().createRetVoid();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
