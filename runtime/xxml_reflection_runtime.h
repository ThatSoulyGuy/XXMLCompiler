#ifndef XXML_REFLECTION_RUNTIME_H
#define XXML_REFLECTION_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// XXML Reflection Runtime Library
// Runtime type information for XXML classes
// ============================================

// Forward declarations
typedef struct ReflectionTypeInfo ReflectionTypeInfo;
typedef struct ReflectionMethodInfo ReflectionMethodInfo;
typedef struct ReflectionPropertyInfo ReflectionPropertyInfo;
typedef struct ReflectionParameterInfo ReflectionParameterInfo;
typedef struct ReflectionTemplateParamInfo ReflectionTemplateParamInfo;

// Property metadata
struct ReflectionPropertyInfo {
    const char* name;
    const char* typeName;
    int32_t ownership;  // 0=None, 1=Owned(^), 2=Reference(&), 3=Copy(%)
    size_t offset;      // Offset in bytes from object start
};

// Parameter metadata
struct ReflectionParameterInfo {
    const char* name;
    const char* typeName;
    int32_t ownership;
};

// Method metadata
struct ReflectionMethodInfo {
    const char* name;
    const char* returnType;
    int32_t returnOwnership;
    int32_t parameterCount;
    ReflectionParameterInfo* parameters;
    void* functionPointer;  // Pointer to actual method implementation
    bool isStatic;
    bool isConstructor;
};

// Template parameter metadata
struct ReflectionTemplateParamInfo {
    const char* name;
    bool isTypeParameter;  // true for type, false for value
    const char* valueType;  // For non-type parameters (e.g., "int64")
};

// Type metadata
struct ReflectionTypeInfo {
    const char* name;
    const char* namespaceName;
    const char* fullName;  // Namespace::ClassName
    bool isTemplate;
    int32_t templateParamCount;
    ReflectionTemplateParamInfo* templateParams;

    int32_t propertyCount;
    ReflectionPropertyInfo* properties;

    int32_t methodCount;
    ReflectionMethodInfo* methods;

    int32_t constructorCount;
    ReflectionMethodInfo* constructors;

    const char* baseClassName;  // NULL if no base class
    size_t instanceSize;        // Size in bytes
};

// ============================================
// Global Type Registry Functions
// ============================================

// Register a new type in the global registry
void* Reflection_registerType(ReflectionTypeInfo* typeInfo);

// Get type info by full name (e.g., "Math::Vector2")
ReflectionTypeInfo* Reflection_getTypeInfo(const char* typeName);

// Get total number of registered types
int32_t Reflection_getTypeCount();

// Get array of all registered type names
const char** Reflection_getAllTypeNames();

#ifdef __cplusplus
}
#endif

#endif // XXML_REFLECTION_RUNTIME_H
