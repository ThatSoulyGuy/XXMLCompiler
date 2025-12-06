#include "Backends/Codegen/ReflectionCodegen/ReflectionCodegen.h"
#include <format>
#include <sstream>

namespace XXML {
namespace Backends {
namespace Codegen {

ReflectionCodegen::ReflectionCodegen(CodegenContext& ctx)
    : ctx_(ctx) {}

void ReflectionCodegen::emitLine(const std::string& line) {
    output_ << line << "\n";
}

std::string ReflectionCodegen::escapeString(const std::string& str) const {
    std::string result;
    for (char c : str) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\22";
        else if (c == '\n') result += "\\0A";
        else if (c == '\r') result += "\\0D";
        else if (c == '\t') result += "\\09";
        else result += c;
    }
    return result;
}

int32_t ReflectionCodegen::ownershipToInt(const std::string& ownership) const {
    if (ownership.empty()) return 0;
    char c = ownership[0];
    if (c == '^') return 1;  // Owned
    if (c == '&') return 2;  // Reference
    if (c == '%') return 3;  // Copy
    return 0;  // Unknown
}

std::string ReflectionCodegen::emitStringLiteral(const std::string& content, const std::string& prefix) {
    auto it = stringLabelMap_.find(content);
    if (it != stringLabelMap_.end()) {
        return it->second;
    }

    std::string label = std::format("{}{}", prefix, stringCounter_++);
    std::string escaped = escapeString(content);
    emitLine(std::format("@.str.{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                         label, content.length() + 1, escaped));
    stringLabelMap_[content] = label;
    return label;
}

std::string ReflectionCodegen::mangleName(const std::string& fullName) const {
    std::string mangledName = fullName;
    for (char& c : mangledName) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ') {
            c = '_';
        }
    }
    return mangledName;
}

void ReflectionCodegen::emitReflectionStructTypes() {
    emitLine("; Reflection structure types");
    emitLine("%ReflectionPropertyInfo = type { ptr, ptr, i32, i64 }");
    emitLine("%ReflectionParameterInfo = type { ptr, ptr, i32 }");
    emitLine("%ReflectionMethodInfo = type { ptr, ptr, i32, i32, ptr, ptr, i1, i1 }");
    emitLine("%ReflectionTemplateParamInfo = type { ptr }");
    emitLine("%ReflectionTypeInfo = type { ptr, ptr, ptr, i1, i32, ptr, i32, ptr, i32, ptr, i32, ptr, ptr, i64 }");
    emitLine("");
    emitLine("; Ownership types: 0=Unknown, 1=Owned(^), 2=Reference(&), 3=Copy(%)");
    emitLine("");
}

void ReflectionCodegen::emitStringLiterals(const ReflectionClassMetadata& metadata) {
    emitStringLiteral(metadata.name, "reflect_name_");
    emitStringLiteral(metadata.namespaceName, "reflect_ns_");
    emitStringLiteral(metadata.fullName, "reflect_full_");

    for (const auto& [propName, propType] : metadata.properties) {
        emitStringLiteral(propName, "reflect_prop_");
        emitStringLiteral(propType, "reflect_type_");
    }

    for (size_t i = 0; i < metadata.methods.size(); ++i) {
        emitStringLiteral(metadata.methods[i].first, "reflect_meth_");
        emitStringLiteral(metadata.methods[i].second, "reflect_ret_");

        for (const auto& [paramName, paramType, paramOwn] : metadata.methodParameters[i]) {
            emitStringLiteral(paramName, "reflect_param_");
            emitStringLiteral(paramType, "reflect_ptype_");
        }
    }

    for (const auto& tparam : metadata.templateParams) {
        emitStringLiteral(tparam, "reflect_tparam_");
    }
}

void ReflectionCodegen::emitPropertyArray(const std::string& mangledName, const ReflectionClassMetadata& metadata) {
    if (metadata.properties.empty()) return;

    emitLine(std::format("@reflection_props_{} = private constant [{} x %ReflectionPropertyInfo] [",
                         mangledName, metadata.properties.size()));

    for (size_t i = 0; i < metadata.properties.size(); ++i) {
        const auto& [propName, propType] = metadata.properties[i];
        std::string ownership = i < metadata.propertyOwnerships.size() ? metadata.propertyOwnerships[i] : "";
        int32_t ownershipVal = ownershipToInt(ownership);

        std::string nameLabel = stringLabelMap_[propName];
        std::string typeLabel = stringLabelMap_[propType];
        int64_t offset = static_cast<int64_t>(i * 8);

        emitLine(std::format("  %ReflectionPropertyInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {}, i64 {} }}{}",
                             nameLabel, typeLabel, ownershipVal, offset,
                             (i < metadata.properties.size() - 1) ? "," : ""));
    }
    emitLine("]");
}

void ReflectionCodegen::emitMethodParameterArrays(const std::string& mangledName, const ReflectionClassMetadata& metadata) {
    for (size_t m = 0; m < metadata.methods.size(); ++m) {
        const auto& params = metadata.methodParameters[m];
        if (params.empty()) continue;

        emitLine(std::format("@reflection_method_{}_params_{} = private constant [{} x %ReflectionParameterInfo] [",
                             mangledName, m, params.size()));

        for (size_t p = 0; p < params.size(); ++p) {
            const auto& [paramName, paramType, paramOwn] = params[p];
            int32_t ownershipVal = ownershipToInt(paramOwn);

            std::string nameLabel = stringLabelMap_[paramName];
            std::string typeLabel = stringLabelMap_[paramType];

            emitLine(std::format("  %ReflectionParameterInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {} }}{}",
                                 nameLabel, typeLabel, ownershipVal,
                                 (p < params.size() - 1) ? "," : ""));
        }
        emitLine("]");
    }
}

void ReflectionCodegen::emitMethodArray(const std::string& mangledName, const ReflectionClassMetadata& metadata) {
    if (metadata.methods.empty()) return;

    emitLine(std::format("@reflection_methods_{} = private constant [{} x %ReflectionMethodInfo] [",
                         mangledName, metadata.methods.size()));

    for (size_t m = 0; m < metadata.methods.size(); ++m) {
        const auto& [methodName, returnType] = metadata.methods[m];
        std::string returnOwn = m < metadata.methodReturnOwnerships.size() ? metadata.methodReturnOwnerships[m] : "";
        int32_t returnOwnVal = ownershipToInt(returnOwn);

        std::string nameLabel = stringLabelMap_[methodName];
        std::string retLabel = stringLabelMap_[returnType];

        int32_t paramCount = static_cast<int32_t>(metadata.methodParameters[m].size());
        std::string paramsArrayPtr = paramCount > 0 ?
            std::format("ptr @reflection_method_{}_params_{}", mangledName, m) :
            "ptr null";

        // Note: funcPtr and isStatic/isCtor are set to null/false for now
        emitLine(std::format("  %ReflectionMethodInfo {{ ptr @.str.{}, ptr @.str.{}, i32 {}, i32 {}, {}, ptr null, i1 false, i1 {} }}{}",
                             nameLabel, retLabel, returnOwnVal, paramCount, paramsArrayPtr,
                             (methodName == "Constructor" ? "true" : "false"),
                             (m < metadata.methods.size() - 1) ? "," : ""));
    }
    emitLine("]");
}

void ReflectionCodegen::emitTemplateParamArray(const std::string& mangledName, const ReflectionClassMetadata& metadata) {
    if (metadata.templateParams.empty()) return;

    emitLine(std::format("@reflection_tparams_{} = private constant [{} x %ReflectionTemplateParamInfo] [",
                         mangledName, metadata.templateParams.size()));

    for (size_t i = 0; i < metadata.templateParams.size(); ++i) {
        std::string label = stringLabelMap_[metadata.templateParams[i]];
        emitLine(std::format("  %ReflectionTemplateParamInfo {{ ptr @.str.{} }}{}",
                             label, (i < metadata.templateParams.size() - 1) ? "," : ""));
    }
    emitLine("]");
}

void ReflectionCodegen::emitTypeInfo(const std::string& mangledName, const ReflectionClassMetadata& metadata) {
    std::string nameLabel = stringLabelMap_[metadata.name];
    std::string nsLabel = stringLabelMap_[metadata.namespaceName];
    std::string fullLabel = stringLabelMap_[metadata.fullName];

    int32_t propCount = static_cast<int32_t>(metadata.properties.size());
    int32_t methodCount = static_cast<int32_t>(metadata.methods.size());
    int32_t tparamCount = static_cast<int32_t>(metadata.templateParams.size());

    std::string propsPtr = propCount > 0 ? std::format("ptr @reflection_props_{}", mangledName) : "ptr null";
    std::string methodsPtr = methodCount > 0 ? std::format("ptr @reflection_methods_{}", mangledName) : "ptr null";
    std::string tparamsPtr = tparamCount > 0 ? std::format("ptr @reflection_tparams_{}", mangledName) : "ptr null";

    emitLine(std::format("; TypeInfo for {}", metadata.fullName));
    emitLine(std::format("@reflection_typeinfo_{} = private constant %ReflectionTypeInfo {{",
                         mangledName));
    emitLine(std::format("  ptr @.str.{},", nameLabel));                    // name
    emitLine(std::format("  ptr @.str.{},", nsLabel));                      // namespace
    emitLine(std::format("  ptr @.str.{},", fullLabel));                    // fullName
    emitLine(std::format("  i1 {},", metadata.isTemplate ? "true" : "false")); // isTemplate
    emitLine(std::format("  i32 {},", tparamCount));                        // templateParamCount
    emitLine(std::format("  {},", tparamsPtr));                             // templateParams
    emitLine(std::format("  i32 {},", propCount));                          // propertyCount
    emitLine(std::format("  {},", propsPtr));                               // properties
    emitLine(std::format("  i32 {},", methodCount));                        // methodCount
    emitLine(std::format("  {},", methodsPtr));                             // methods
    emitLine("  i32 0,");                                                   // baseCount
    emitLine("  ptr null,");                                                // bases
    emitLine("  ptr null,");                                                // createInstance function ptr
    emitLine(std::format("  i64 {}", metadata.instanceSize));               // instanceSize
    emitLine("}");
}

void ReflectionCodegen::generate() {
    const auto& reflectionMetadata = ctx_.reflectionMetadata();
    if (reflectionMetadata.empty()) {
        return;
    }

    emitLine("; ============================================");
    emitLine("; Reflection Metadata");
    emitLine("; ============================================");
    emitLine("");

    // Emit struct type definitions
    emitReflectionStructTypes();

    // First pass: emit all string literals
    emitLine("; String literals for reflection");
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        emitStringLiterals(metadata);
    }
    emitLine("");

    // Second pass: emit metadata for each class
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        std::string mangledName = mangleName(fullName);

        emitLine(std::format("; Metadata for class: {}", fullName));

        // Emit arrays
        emitPropertyArray(mangledName, metadata);
        emitMethodParameterArrays(mangledName, metadata);
        emitMethodArray(mangledName, metadata);
        emitTemplateParamArray(mangledName, metadata);

        // Emit type info struct
        emitTypeInfo(mangledName, metadata);
        emitLine("");
    }
}

std::string ReflectionCodegen::getIR() const {
    return output_.str();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
