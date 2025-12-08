#include "Backends/Codegen/TypeDescriptor.h"
#include <algorithm>

namespace XXML::Backends::Codegen {

std::string TypeDescriptorManager::normalizeTypeName(const std::string& typeName) const {
    std::string normalized = typeName;

    // Strip ownership markers
    if (!normalized.empty()) {
        char last = normalized.back();
        if (last == '^' || last == '&' || last == '%') {
            normalized.pop_back();
        }
    }

    return normalized;
}

const TypeDescriptor& TypeDescriptorManager::getOrCreateDescriptor(
    const std::string& typeName,
    size_t size,
    bool isValueType) {

    std::string normalizedName = normalizeTypeName(typeName);

    auto it = descriptors_.find(normalizedName);
    if (it != descriptors_.end()) {
        return it->second;
    }

    // Create new descriptor
    TypeDescriptor desc;
    desc.typeName = normalizedName;
    desc.size = (size == 0) ? 8 : size;
    desc.alignment = desc.size;  // Simplify: alignment = size for now
    desc.isValueType = isValueType;
    desc.isTrivial = true;  // Most XXML types are trivially copyable

    // Try to infer function names based on type conventions
    std::string mangledName = normalizedName;
    for (char& c : mangledName) {
        if (c == ':') c = '_';
    }

    // Standard function names
    desc.equalsFn = mangledName + "_equals";
    desc.hashFn = mangledName + "_hashCode";
    desc.toStringFn = mangledName + "_toString";

    auto [insertIt, _] = descriptors_.emplace(normalizedName, std::move(desc));
    return insertIt->second;
}

bool TypeDescriptorManager::hasDescriptor(const std::string& typeName) const {
    std::string normalizedName = normalizeTypeName(typeName);
    return descriptors_.find(normalizedName) != descriptors_.end();
}

const TypeDescriptor* TypeDescriptorManager::getDescriptor(const std::string& typeName) const {
    std::string normalizedName = normalizeTypeName(typeName);
    auto it = descriptors_.find(normalizedName);
    if (it != descriptors_.end()) {
        return &it->second;
    }
    return nullptr;
}

void TypeDescriptorManager::registerFunction(
    const std::string& typeName,
    const std::string& functionKind,
    const std::string& functionName) {

    std::string normalizedName = normalizeTypeName(typeName);

    // Ensure descriptor exists
    if (!hasDescriptor(normalizedName)) {
        getOrCreateDescriptor(normalizedName);
    }

    auto it = descriptors_.find(normalizedName);
    if (it == descriptors_.end()) {
        return;
    }

    if (functionKind == "equals") {
        it->second.equalsFn = functionName;
    } else if (functionKind == "hash") {
        it->second.hashFn = functionName;
    } else if (functionKind == "toString") {
        it->second.toStringFn = functionName;
    }
}

} // namespace XXML::Backends::Codegen
