#pragma once

#include "Backends/LLVMIR/TypedModule.h"
#include "Backends/LLVMIR/TypedBuilder.h"
#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <map>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// MetadataBuilder - Type-Safe Reflection/Annotation Metadata Builder
// ============================================================================
//
// This builder generates reflection and annotation metadata using the typed
// IR system. It replaces all raw string emission (std::format) for metadata
// generation with type-safe builder calls.
//
// Reflection Struct Types:
// - %ReflectionPropertyInfo = type { ptr, ptr, i32, i64 }
//   (name, type, ownership, offset)
// - %ReflectionParameterInfo = type { ptr, ptr, i32 }
//   (name, type, ownership)
// - %ReflectionMethodInfo = type { ptr, ptr, i32, i32, ptr, ptr, i1, i1 }
//   (name, returnType, returnOwnership, paramCount, params, funcPtr, isStatic, isCtor)
// - %ReflectionTemplateParamInfo = type { ptr }
//   (name)
// - %ReflectionTypeInfo = type { ptr, ptr, ptr, i1, i32, ptr, i32, ptr, i32, ptr, i32, ptr, ptr, i64 }
//   (name, namespace, fullName, isTemplate, tparamCount, tparams, propCount, props, methodCount, methods, baseCount, bases, createInstance, instanceSize)

/// Ownership types for reflection
enum class OwnershipKind {
    Unknown = 0,
    Owned = 1,      // ^
    Reference = 2,  // &
    Copy = 3        // %
};

/// Convert ownership marker to enum
OwnershipKind parseOwnership(std::string_view marker);

/// Information about a property for reflection
struct ReflectionProperty {
    std::string name;
    std::string typeName;
    OwnershipKind ownership = OwnershipKind::Unknown;
    int64_t offset = 0;
};

/// Information about a method parameter for reflection
struct ReflectionParameter {
    std::string name;
    std::string typeName;
    OwnershipKind ownership = OwnershipKind::Unknown;
};

/// Information about a method for reflection
struct ReflectionMethod {
    std::string name;
    std::string returnTypeName;
    OwnershipKind returnOwnership = OwnershipKind::Unknown;
    std::vector<ReflectionParameter> parameters;
    bool isStatic = false;
    bool isConstructor = false;
    Function* funcPtr = nullptr;
};

/// Full reflection metadata for a class
struct ReflectionClassInfo {
    std::string name;
    std::string namespaceName;
    std::string fullName;
    bool isTemplate = false;
    int64_t instanceSize = 0;
    std::vector<std::string> templateParams;
    std::vector<ReflectionProperty> properties;
    std::vector<ReflectionMethod> methods;
    std::vector<std::string> baseClasses;
};

/// Result of creating reflection metadata
struct ReflectionMetadataResult {
    GlobalVariable* typeInfo;                // Main TypeInfo global
    GlobalVariable* propertiesArray;         // Properties array (may be null)
    GlobalVariable* methodsArray;            // Methods array (may be null)
    GlobalVariable* templateParamsArray;     // Template params array (may be null)
    std::vector<GlobalVariable*> methodParamArrays;  // Per-method parameter arrays
};

class MetadataBuilder {
public:
    MetadataBuilder(Module& module, GlobalBuilder& globalBuilder);

    // ========================================================================
    // Struct Type Initialization
    // ========================================================================

    /// Initialize all reflection struct types. Call this once before generating metadata.
    void initializeReflectionTypes();

    /// Check if reflection types are initialized
    bool areReflectionTypesInitialized() const;

    // ========================================================================
    // Get Reflection Struct Types
    // ========================================================================

    StructType* getPropertyInfoType();
    StructType* getParameterInfoType();
    StructType* getMethodInfoType();
    StructType* getTemplateParamInfoType();
    StructType* getTypeInfoType();

    // ========================================================================
    // Metadata Generation
    // ========================================================================

    /// Generate complete reflection metadata for a class.
    ReflectionMetadataResult generateClassMetadata(const ReflectionClassInfo& info);

    /// Generate metadata for multiple classes.
    std::vector<ReflectionMetadataResult> generateAllMetadata(
        std::span<const ReflectionClassInfo> classes);

    // ========================================================================
    // Individual Component Builders
    // ========================================================================

    /// Create a property info array.
    GlobalVariable* createPropertyArray(std::string_view mangledClassName,
                                        std::span<const ReflectionProperty> properties);

    /// Create a parameter info array for a method.
    GlobalVariable* createParameterArray(std::string_view mangledClassName,
                                         size_t methodIndex,
                                         std::span<const ReflectionParameter> parameters);

    /// Create a method info array.
    GlobalVariable* createMethodArray(std::string_view mangledClassName,
                                      std::span<const ReflectionMethod> methods,
                                      const std::vector<GlobalVariable*>& paramArrays);

    /// Create a template parameter info array.
    GlobalVariable* createTemplateParamArray(std::string_view mangledClassName,
                                             std::span<const std::string> templateParams);

    /// Create the main type info structure.
    GlobalVariable* createTypeInfo(std::string_view mangledClassName,
                                   const ReflectionClassInfo& info,
                                   GlobalVariable* propsArray,
                                   GlobalVariable* methodsArray,
                                   GlobalVariable* tparamsArray);

    // ========================================================================
    // String Helpers
    // ========================================================================

    /// Get or create a string constant and return its global variable.
    GlobalVariable* getOrCreateString(std::string_view content);

    /// Mangle a name for use in global variable names.
    static std::string mangleName(std::string_view fullName);

    // ========================================================================
    // Module & Context Accessors
    // ========================================================================

    Module& getModule() { return module_; }
    const Module& getModule() const { return module_; }

    TypeContext& getContext() { return module_.getContext(); }
    const TypeContext& getContext() const { return module_.getContext(); }

    GlobalBuilder& getGlobalBuilder() { return globalBuilder_; }

private:
    Module& module_;
    GlobalBuilder& globalBuilder_;

    // Cached struct types
    StructType* propertyInfoType_ = nullptr;
    StructType* parameterInfoType_ = nullptr;
    StructType* methodInfoType_ = nullptr;
    StructType* templateParamInfoType_ = nullptr;
    StructType* typeInfoType_ = nullptr;

    // String literal cache
    std::map<std::string, GlobalVariable*> stringCache_;
    size_t stringCounter_ = 0;

    // Helper to create anonymous struct constant (for array initializers)
    // Returns global variables that need to be emitted
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
