#include "../../include/Semantic/LayoutComputer.h"
#include <algorithm>
#include <queue>
#include <unordered_set>
#include <functional>

namespace XXML {
namespace Semantic {

LayoutComputer::LayoutComputer(Common::ErrorReporter& errorReporter,
                               const TypeResolutionResult& typeResolution,
                               const SemanticValidationResult& semanticResult)
    : errorReporter_(errorReporter),
      typeResolution_(typeResolution),
      semanticResult_(semanticResult) {
}

LayoutComputationResult LayoutComputer::run(
    const std::unordered_map<std::string, ClassInfo>& classRegistry) {

    result_ = LayoutComputationResult{};

    // Build dependency graph for layout order
    buildDependencyGraph(classRegistry);

    // Detect circular inheritance
    detectCircularInheritance();
    if (errorReporter_.hasErrors()) {
        return result_;
    }

    // Topological sort for layout order
    layoutOrder_ = topologicalSort();

    // Compute layouts in dependency order
    for (const auto& className : layoutOrder_) {
        auto it = classRegistry.find(className);
        if (it == classRegistry.end()) continue;

        ClassLayout layout = computeLayout(className, it->second);
        validateLayout(className, layout);

        ReflectionMetadataInfo metadata = generateMetadata(className, it->second, layout);
        validateReflectionMetadata(className, metadata, layout);

        result_.layouts[className] = layout;
        result_.metadata[className] = metadata;
    }

    result_.success = !errorReporter_.hasErrors();
    return result_;
}

const ClassLayout* LayoutComputer::getLayout(const std::string& className) const {
    auto it = result_.layouts.find(className);
    return it != result_.layouts.end() ? &it->second : nullptr;
}

const ReflectionMetadataInfo* LayoutComputer::getMetadata(const std::string& className) const {
    auto it = result_.metadata.find(className);
    return it != result_.metadata.end() ? &it->second : nullptr;
}

//==============================================================================
// SIZE AND ALIGNMENT
//==============================================================================

size_t LayoutComputer::getTypeSize(const std::string& typeName) const {
    // Check primitives
    size_t primitiveSize = getPrimitiveSize(typeName);
    if (primitiveSize > 0) return primitiveSize;

    // Check if it's a computed layout
    const ClassLayout* layout = getLayout(typeName);
    if (layout) return layout->totalSize;

    // Default to pointer size (objects are heap-allocated)
    return getPointerSize();
}

size_t LayoutComputer::getTypeAlignment(const std::string& typeName) const {
    // Check primitives
    size_t primitiveAlign = getPrimitiveAlignment(typeName);
    if (primitiveAlign > 0) return primitiveAlign;

    // Check if it's a computed layout
    const ClassLayout* layout = getLayout(typeName);
    if (layout) return layout->alignment;

    // Default to pointer alignment
    return getPointerAlignment();
}

size_t LayoutComputer::getPrimitiveSize(const std::string& typeName) const {
    if (typeName == "Integer" || typeName == "Language::Core::Integer") return 8;  // i64
    if (typeName == "Float" || typeName == "Language::Core::Float") return 4;      // f32
    if (typeName == "Double" || typeName == "Language::Core::Double") return 8;    // f64
    if (typeName == "Bool" || typeName == "Language::Core::Bool") return 1;        // i1
    if (typeName == "String" || typeName == "Language::Core::String") return 8;    // ptr
    return 0;  // Not a primitive
}

size_t LayoutComputer::getPrimitiveAlignment(const std::string& typeName) const {
    if (typeName == "Integer" || typeName == "Language::Core::Integer") return 8;
    if (typeName == "Float" || typeName == "Language::Core::Float") return 4;
    if (typeName == "Double" || typeName == "Language::Core::Double") return 8;
    if (typeName == "Bool" || typeName == "Language::Core::Bool") return 1;
    if (typeName == "String" || typeName == "Language::Core::String") return 8;
    return 0;
}

//==============================================================================
// LAYOUT COMPUTATION
//==============================================================================

ClassLayout LayoutComputer::computeLayout(const std::string& className,
                                          const ClassInfo& classInfo) {
    ClassLayout layout;
    layout.className = classInfo.qualifiedName;
    layout.qualifiedName = classInfo.qualifiedName;
    layout.isPacked = false;
    layout.hasBaseClass = !classInfo.baseClassName.empty();
    layout.baseClassName = classInfo.baseClassName;

    size_t currentOffset = 0;
    size_t maxAlignment = 1;

    // Include base class layout if present
    if (layout.hasBaseClass) {
        const ClassLayout* baseLayout = getLayout(classInfo.baseClassName);
        if (baseLayout) {
            layout.baseClassSize = baseLayout->totalSize;
            currentOffset = baseLayout->totalSize;
            maxAlignment = std::max(maxAlignment, baseLayout->alignment);

            // Copy base class fields (they come first)
            for (const auto& field : baseLayout->fields) {
                layout.fields.push_back(field);
            }
        }
    }

    // Add fields from this class
    for (const auto& [propName, propInfo] : classInfo.properties) {
        const std::string& propType = propInfo.first;
        Parser::OwnershipType ownership = propInfo.second;

        FieldLayout field = computeFieldLayout(propName, propType, ownership, currentOffset);
        maxAlignment = std::max(maxAlignment, field.alignment);
        layout.fields.push_back(field);
    }

    // Add trailing padding for alignment
    layout.totalSize = alignTo(currentOffset, maxAlignment);
    layout.alignment = maxAlignment;

    return layout;
}

FieldLayout LayoutComputer::computeFieldLayout(const std::string& propName,
                                                const std::string& propType,
                                                Parser::OwnershipType ownership,
                                                size_t& currentOffset) {
    FieldLayout field;
    field.name = propName;
    field.typeName = propType;
    field.ownership = ownership;

    // Determine size and alignment based on ownership
    if (ownership == Parser::OwnershipType::Owned) {
        // Owned types are pointers (Owned<T> is a smart pointer wrapper)
        field.size = getPointerSize();
        field.alignment = getPointerAlignment();
    } else if (ownership == Parser::OwnershipType::Reference) {
        // References are pointers
        field.size = getPointerSize();
        field.alignment = getPointerAlignment();
    } else {
        // Copy or value types - use actual type size
        field.size = getTypeSize(propType);
        field.alignment = getTypeAlignment(propType);
    }

    // If size/alignment is 0, default to pointer (objects)
    if (field.size == 0) {
        field.size = getPointerSize();
        field.alignment = getPointerAlignment();
    }

    // Align offset
    currentOffset = alignTo(currentOffset, field.alignment);
    field.offset = currentOffset;

    // Advance offset
    currentOffset += field.size;

    return field;
}

//==============================================================================
// REFLECTION METADATA
//==============================================================================

ReflectionMetadataInfo LayoutComputer::generateMetadata(const std::string& className,
                                                        const ClassInfo& classInfo,
                                                        const ClassLayout& layout) {
    ReflectionMetadataInfo metadata;
    metadata.fullName = classInfo.qualifiedName;

    // Extract namespace
    size_t lastColon = classInfo.qualifiedName.rfind("::");
    if (lastColon != std::string::npos) {
        metadata.namespaceName = classInfo.qualifiedName.substr(0, lastColon);
    }

    metadata.isTemplate = classInfo.isTemplate;
    for (const auto& param : classInfo.templateParams) {
        metadata.templateParams.push_back(param.name);
    }

    // Add properties
    for (const auto& [propName, propInfo] : classInfo.properties) {
        metadata.properties.push_back({propName, propInfo.first});
    }

    // Add methods
    for (const auto& [methodName, methodInfo] : classInfo.methods) {
        metadata.methods.push_back({methodName, methodInfo.returnType});
    }

    metadata.instanceSize = static_cast<int64_t>(layout.totalSize);

    return metadata;
}

//==============================================================================
// DEPENDENCY RESOLUTION
//==============================================================================

void LayoutComputer::buildDependencyGraph(
    const std::unordered_map<std::string, ClassInfo>& classRegistry) {

    dependencies_.clear();

    for (const auto& [name, info] : classRegistry) {
        std::vector<std::string> deps;

        // Base class is a dependency
        if (!info.baseClassName.empty()) {
            deps.push_back(info.baseClassName);
        }

        // Property types that are value types are dependencies
        for (const auto& [propName, propInfo] : info.properties) {
            if (propInfo.second == Parser::OwnershipType::Copy) {
                // Only value types need their layout computed first
                deps.push_back(propInfo.first);
            }
        }

        dependencies_[name] = deps;
    }
}

std::vector<std::string> LayoutComputer::topologicalSort() {
    std::vector<std::string> result;
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adjList;

    // Initialize in-degrees
    for (const auto& [name, deps] : dependencies_) {
        if (inDegree.find(name) == inDegree.end()) {
            inDegree[name] = 0;
        }
        for (const auto& dep : deps) {
            if (dependencies_.find(dep) != dependencies_.end()) {
                adjList[dep].push_back(name);
                inDegree[name]++;
            }
        }
    }

    // Find all nodes with no dependencies
    std::queue<std::string> queue;
    for (const auto& [name, degree] : inDegree) {
        if (degree == 0) {
            queue.push(name);
        }
    }

    // Process in topological order
    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();
        result.push_back(current);

        for (const auto& dependent : adjList[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                queue.push(dependent);
            }
        }
    }

    return result;
}

void LayoutComputer::detectCircularInheritance() {
    // Use DFS to detect cycles
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> inStack;

    std::function<bool(const std::string&)> hasCycle;
    hasCycle = [&](const std::string& name) -> bool {
        if (inStack.count(name)) return true;
        if (visited.count(name)) return false;

        visited.insert(name);
        inStack.insert(name);

        auto it = dependencies_.find(name);
        if (it != dependencies_.end()) {
            for (const auto& dep : it->second) {
                if (hasCycle(dep)) return true;
            }
        }

        inStack.erase(name);
        return false;
    };

    for (const auto& [name, _] : dependencies_) {
        if (hasCycle(name)) {
            errorReporter_.reportError(
                Common::ErrorCode::TypeMismatch,
                "Circular inheritance detected involving '" + name + "'",
                Common::SourceLocation{}
            );
        }
    }
}

//==============================================================================
// VALIDATION
//==============================================================================

void LayoutComputer::validateLayout(const std::string& className, const ClassLayout& layout) {
    // Zero-size structs are allowed in XXML:
    // - Classes with only static methods (e.g., Console, Math)
    // - Marker types (e.g., None)
    // - Empty base classes
    // So we don't error on zero size, just ensure minimum alignment of 1

    // Check for proper alignment (0 is invalid, must be power of 2)
    if (layout.alignment == 0) {
        // Default to 1 for empty structs
        return;
    }
    if ((layout.alignment & (layout.alignment - 1)) != 0) {
        errorReporter_.reportError(
            Common::ErrorCode::TypeMismatch,
            "Class '" + className + "' has invalid alignment: " + std::to_string(layout.alignment),
            Common::SourceLocation{}
        );
    }

    // Check field offsets are increasing
    size_t lastOffset = 0;
    for (const auto& field : layout.fields) {
        if (field.offset < lastOffset) {
            errorReporter_.reportError(
                Common::ErrorCode::TypeMismatch,
                "Field '" + field.name + "' in class '" + className +
                "' has invalid offset: " + std::to_string(field.offset),
                Common::SourceLocation{}
            );
        }
        lastOffset = field.offset + field.size;
    }
}

void LayoutComputer::validateReflectionMetadata(const std::string& className,
                                                 const ReflectionMetadataInfo& metadata,
                                                 const ClassLayout& layout) {
    // Check that instanceSize matches computed layout
    if (metadata.instanceSize != static_cast<int64_t>(layout.totalSize)) {
        errorReporter_.reportError(
            Common::ErrorCode::TypeMismatch,
            "Reflection metadata size (" + std::to_string(metadata.instanceSize) +
            ") doesn't match computed layout (" + std::to_string(layout.totalSize) +
            ") for class '" + className + "'",
            Common::SourceLocation{}
        );
    }
}

} // namespace Semantic
} // namespace XXML
