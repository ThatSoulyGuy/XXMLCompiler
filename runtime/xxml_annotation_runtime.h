#ifndef XXML_ANNOTATION_RUNTIME_H
#define XXML_ANNOTATION_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// XXML Annotation Runtime Library
// Runtime annotation information for XXML classes
// Used when annotations have the Retain keyword
// ============================================

// Forward declarations
typedef struct ReflectionAnnotationInfo ReflectionAnnotationInfo;
typedef struct ReflectionAnnotationArg ReflectionAnnotationArg;
typedef struct AnnotationTargetInfo AnnotationTargetInfo;

// ============================================
// Annotation Argument Value
// ============================================
typedef enum {
    ANNOTATION_ARG_INTEGER = 0,
    ANNOTATION_ARG_STRING = 1,
    ANNOTATION_ARG_BOOL = 2,
    ANNOTATION_ARG_FLOAT = 3,
    ANNOTATION_ARG_DOUBLE = 4
} AnnotationArgType;

struct ReflectionAnnotationArg {
    const char* name;           // Parameter name
    AnnotationArgType valueType;
    union {
        int64_t intValue;
        const char* stringValue;
        bool boolValue;
        float floatValue;
        double doubleValue;
    } value;
};

// ============================================
// Annotation Usage Info
// ============================================
struct ReflectionAnnotationInfo {
    const char* annotationName;     // Name of the annotation (e.g., "Deprecated")
    int32_t argumentCount;
    ReflectionAnnotationArg* arguments;
};

// ============================================
// Annotation Target Context (for runtime queries)
// ============================================
typedef enum {
    ANNOTATION_TARGET_CLASS = 0,
    ANNOTATION_TARGET_METHOD = 1,
    ANNOTATION_TARGET_PROPERTY = 2,
    ANNOTATION_TARGET_VARIABLE = 3
} AnnotationTargetKind;

struct AnnotationTargetInfo {
    AnnotationTargetKind kind;
    const char* name;               // Name of the target element
    const char* typeName;           // Type name (for properties/variables)
    const char* className;          // Containing class (for methods/properties)
    const char* namespaceName;      // Containing namespace
};

// ============================================
// Global Annotation Registry Functions
// ============================================

// Register annotations for a type (called during static initialization)
void Annotation_registerForType(const char* typeName,
                                int32_t annotationCount,
                                ReflectionAnnotationInfo* annotations);

// Register annotations for a method
void Annotation_registerForMethod(const char* typeName,
                                  const char* methodName,
                                  int32_t annotationCount,
                                  ReflectionAnnotationInfo* annotations);

// Register annotations for a property
void Annotation_registerForProperty(const char* typeName,
                                    const char* propertyName,
                                    int32_t annotationCount,
                                    ReflectionAnnotationInfo* annotations);

// ============================================
// Annotation Query Functions
// ============================================

// Get annotations for a type
int32_t Annotation_getCountForType(const char* typeName);
ReflectionAnnotationInfo* Annotation_getForType(const char* typeName, int32_t index);

// Get annotations for a method
int32_t Annotation_getCountForMethod(const char* typeName, const char* methodName);
ReflectionAnnotationInfo* Annotation_getForMethod(const char* typeName,
                                                   const char* methodName,
                                                   int32_t index);

// Get annotations for a property
int32_t Annotation_getCountForProperty(const char* typeName, const char* propertyName);
ReflectionAnnotationInfo* Annotation_getForProperty(const char* typeName,
                                                     const char* propertyName,
                                                     int32_t index);

// Check if a type has a specific annotation
bool Annotation_typeHas(const char* typeName, const char* annotationName);

// Check if a method has a specific annotation
bool Annotation_methodHas(const char* typeName,
                          const char* methodName,
                          const char* annotationName);

// Check if a property has a specific annotation
bool Annotation_propertyHas(const char* typeName,
                            const char* propertyName,
                            const char* annotationName);

// Get a specific annotation by name
ReflectionAnnotationInfo* Annotation_getByNameForType(const char* typeName,
                                                       const char* annotationName);
ReflectionAnnotationInfo* Annotation_getByNameForMethod(const char* typeName,
                                                         const char* methodName,
                                                         const char* annotationName);
ReflectionAnnotationInfo* Annotation_getByNameForProperty(const char* typeName,
                                                           const char* propertyName,
                                                           const char* annotationName);

// ============================================
// Annotation Argument Accessors
// ============================================

// Get argument by name from annotation
ReflectionAnnotationArg* Annotation_getArgument(ReflectionAnnotationInfo* annotation,
                                                 const char* argName);

// Get argument value helpers
int64_t Annotation_getIntArg(ReflectionAnnotationInfo* annotation,
                             const char* argName,
                             int64_t defaultValue);
const char* Annotation_getStringArg(ReflectionAnnotationInfo* annotation,
                                    const char* argName,
                                    const char* defaultValue);
bool Annotation_getBoolArg(ReflectionAnnotationInfo* annotation,
                           const char* argName,
                           bool defaultValue);
double Annotation_getDoubleArg(ReflectionAnnotationInfo* annotation,
                               const char* argName,
                               double defaultValue);

// ============================================
// XXML Language Bindings
// ============================================

// Language::Reflection::AnnotationInfo class methods
void* Language_Reflection_AnnotationInfo_Constructor(void* infoPtr);
void* Language_Reflection_AnnotationInfo_getName(void* self);
void* Language_Reflection_AnnotationInfo_getArgumentCount(void* self);
void* Language_Reflection_AnnotationInfo_getArgument(void* self, void* index);
void* Language_Reflection_AnnotationInfo_getArgumentByName(void* self, void* name);
void* Language_Reflection_AnnotationInfo_hasArgument(void* self, void* name);

// Language::Reflection::AnnotationArg class methods
void* Language_Reflection_AnnotationArg_Constructor(void* argPtr);
void* Language_Reflection_AnnotationArg_getName(void* self);
void* Language_Reflection_AnnotationArg_getType(void* self);
void* Language_Reflection_AnnotationArg_asInteger(void* self);
void* Language_Reflection_AnnotationArg_asString(void* self);
void* Language_Reflection_AnnotationArg_asBool(void* self);
void* Language_Reflection_AnnotationArg_asDouble(void* self);

#ifdef __cplusplus
}
#endif

#endif // XXML_ANNOTATION_RUNTIME_H
