#ifndef XXML_REFLECTION_RUNTIME_H
#define XXML_REFLECTION_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xxml_annotation_runtime.h"

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

    // Annotation support
    int32_t annotationCount;
    ReflectionAnnotationInfo* annotations;
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

    // Annotation support
    int32_t annotationCount;
    ReflectionAnnotationInfo* annotations;
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

    // Annotation support
    int32_t annotationCount;
    ReflectionAnnotationInfo* annotations;
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

// ============================================
// XXML Reflection Syscall Interface Functions
// ============================================

// Type lookup
void* xxml_reflection_getTypeByName(const char* typeName);

// Type info accessors
const char* xxml_reflection_type_getName(void* typeInfo);
const char* xxml_reflection_type_getFullName(void* typeInfo);
const char* xxml_reflection_type_getNamespace(void* typeInfo);
int64_t xxml_reflection_type_isTemplate(void* typeInfo);
int64_t xxml_reflection_type_getTemplateParamCount(void* typeInfo);
int64_t xxml_reflection_type_getPropertyCount(void* typeInfo);
void* xxml_reflection_type_getProperty(void* typeInfo, int64_t index);
void* xxml_reflection_type_getPropertyByName(void* typeInfo, const char* name);
int64_t xxml_reflection_type_getMethodCount(void* typeInfo);
void* xxml_reflection_type_getMethod(void* typeInfo, int64_t index);
void* xxml_reflection_type_getMethodByName(void* typeInfo, const char* name);
int64_t xxml_reflection_type_getInstanceSize(void* typeInfo);

// Property info accessors
const char* xxml_reflection_property_getName(void* propInfo);
const char* xxml_reflection_property_getTypeName(void* propInfo);
int64_t xxml_reflection_property_getOwnership(void* propInfo);
int64_t xxml_reflection_property_getOffset(void* propInfo);

// Method info accessors
const char* xxml_reflection_method_getName(void* methodInfo);
const char* xxml_reflection_method_getReturnType(void* methodInfo);
int64_t xxml_reflection_method_getReturnOwnership(void* methodInfo);
int64_t xxml_reflection_method_getParameterCount(void* methodInfo);
void* xxml_reflection_method_getParameter(void* methodInfo, int64_t index);
int64_t xxml_reflection_method_isStatic(void* methodInfo);
int64_t xxml_reflection_method_isConstructor(void* methodInfo);

// Parameter info accessors
const char* xxml_reflection_parameter_getName(void* paramInfo);
const char* xxml_reflection_parameter_getTypeName(void* paramInfo);
int64_t xxml_reflection_parameter_getOwnership(void* paramInfo);

// ============================================
// Language::Reflection Module Functions
// ============================================

// Type class methods
void* Language_Reflection_Type_Constructor(void* infoPtr);
void* Language_Reflection_Type_forName(void* nameStr);
void* Language_Reflection_Type_getName(void* self);
void* Language_Reflection_Type_getFullName(void* self);
void* Language_Reflection_Type_getNamespace(void* self);
void* Language_Reflection_Type_isTemplate(void* self);
void* Language_Reflection_Type_getPropertyCount(void* self);
void* Language_Reflection_Type_getMethodCount(void* self);
void* Language_Reflection_Type_getInstanceSize(void* self);
void* Language_Reflection_Type_getPropertyAt(void* self, void* index);
void* Language_Reflection_Type_getMethodAt(void* self, void* index);

// PropertyInfo class methods
void* Language_Reflection_PropertyInfo_Constructor(void* infoPtr);
void* Language_Reflection_PropertyInfo_getName(void* self);
void* Language_Reflection_PropertyInfo_getTypeName(void* self);

// MethodInfo class methods
void* Language_Reflection_MethodInfo_Constructor(void* infoPtr);
void* Language_Reflection_MethodInfo_getName(void* self);
void* Language_Reflection_MethodInfo_getReturnType(void* self);
void* Language_Reflection_MethodInfo_getParameterCount(void* self);

// NOTE: GetType<T> template implementations are generated by the compiler,
// not provided by the runtime library.

#ifdef __cplusplus
}
#endif

#endif // XXML_REFLECTION_RUNTIME_H
