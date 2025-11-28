#include "xxml_annotation_runtime.h"
#include "xxml_llvm_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================
// Internal Storage for Annotation Registry
// ============================================

#define MAX_ANNOTATION_ENTRIES 1024
#define MAX_NAME_LENGTH 256

typedef struct {
    char typeName[MAX_NAME_LENGTH];
    char memberName[MAX_NAME_LENGTH];  // Empty for type-level annotations
    int32_t annotationCount;
    ReflectionAnnotationInfo* annotations;
    AnnotationTargetKind kind;
} AnnotationEntry;

static AnnotationEntry annotationRegistry[MAX_ANNOTATION_ENTRIES];
static int32_t annotationEntryCount = 0;

// ============================================
// Internal Helper Functions
// ============================================

static AnnotationEntry* findEntry(const char* typeName,
                                  const char* memberName,
                                  AnnotationTargetKind kind) {
    for (int32_t i = 0; i < annotationEntryCount; i++) {
        if (strcmp(annotationRegistry[i].typeName, typeName) == 0 &&
            annotationRegistry[i].kind == kind) {
            if (memberName == NULL || memberName[0] == '\0') {
                if (annotationRegistry[i].memberName[0] == '\0') {
                    return &annotationRegistry[i];
                }
            } else if (strcmp(annotationRegistry[i].memberName, memberName) == 0) {
                return &annotationRegistry[i];
            }
        }
    }
    return NULL;
}

static ReflectionAnnotationInfo* findAnnotationByName(ReflectionAnnotationInfo* annotations,
                                                       int32_t count,
                                                       const char* annotationName) {
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(annotations[i].annotationName, annotationName) == 0) {
            return &annotations[i];
        }
    }
    return NULL;
}

// ============================================
// Registration Functions
// ============================================

void Annotation_registerForType(const char* typeName,
                                int32_t annotationCount,
                                ReflectionAnnotationInfo* annotations) {
    if (annotationEntryCount >= MAX_ANNOTATION_ENTRIES) {
        fprintf(stderr, "Warning: Annotation registry full\n");
        return;
    }

    AnnotationEntry* entry = &annotationRegistry[annotationEntryCount++];
    strncpy(entry->typeName, typeName, MAX_NAME_LENGTH - 1);
    entry->typeName[MAX_NAME_LENGTH - 1] = '\0';
    entry->memberName[0] = '\0';
    entry->annotationCount = annotationCount;
    entry->annotations = annotations;
    entry->kind = ANNOTATION_TARGET_CLASS;
}

void Annotation_registerForMethod(const char* typeName,
                                  const char* methodName,
                                  int32_t annotationCount,
                                  ReflectionAnnotationInfo* annotations) {
    if (annotationEntryCount >= MAX_ANNOTATION_ENTRIES) {
        fprintf(stderr, "Warning: Annotation registry full\n");
        return;
    }

    AnnotationEntry* entry = &annotationRegistry[annotationEntryCount++];
    strncpy(entry->typeName, typeName, MAX_NAME_LENGTH - 1);
    entry->typeName[MAX_NAME_LENGTH - 1] = '\0';
    strncpy(entry->memberName, methodName, MAX_NAME_LENGTH - 1);
    entry->memberName[MAX_NAME_LENGTH - 1] = '\0';
    entry->annotationCount = annotationCount;
    entry->annotations = annotations;
    entry->kind = ANNOTATION_TARGET_METHOD;
}

void Annotation_registerForProperty(const char* typeName,
                                    const char* propertyName,
                                    int32_t annotationCount,
                                    ReflectionAnnotationInfo* annotations) {
    if (annotationEntryCount >= MAX_ANNOTATION_ENTRIES) {
        fprintf(stderr, "Warning: Annotation registry full\n");
        return;
    }

    AnnotationEntry* entry = &annotationRegistry[annotationEntryCount++];
    strncpy(entry->typeName, typeName, MAX_NAME_LENGTH - 1);
    entry->typeName[MAX_NAME_LENGTH - 1] = '\0';
    strncpy(entry->memberName, propertyName, MAX_NAME_LENGTH - 1);
    entry->memberName[MAX_NAME_LENGTH - 1] = '\0';
    entry->annotationCount = annotationCount;
    entry->annotations = annotations;
    entry->kind = ANNOTATION_TARGET_PROPERTY;
}

// ============================================
// Query Functions - Type Level
// ============================================

int32_t Annotation_getCountForType(const char* typeName) {
    AnnotationEntry* entry = findEntry(typeName, NULL, ANNOTATION_TARGET_CLASS);
    return entry ? entry->annotationCount : 0;
}

ReflectionAnnotationInfo* Annotation_getForType(const char* typeName, int32_t index) {
    AnnotationEntry* entry = findEntry(typeName, NULL, ANNOTATION_TARGET_CLASS);
    if (entry && index >= 0 && index < entry->annotationCount) {
        return &entry->annotations[index];
    }
    return NULL;
}

bool Annotation_typeHas(const char* typeName, const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, NULL, ANNOTATION_TARGET_CLASS);
    if (!entry) return false;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName) != NULL;
}

ReflectionAnnotationInfo* Annotation_getByNameForType(const char* typeName,
                                                       const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, NULL, ANNOTATION_TARGET_CLASS);
    if (!entry) return NULL;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName);
}

// ============================================
// Query Functions - Method Level
// ============================================

int32_t Annotation_getCountForMethod(const char* typeName, const char* methodName) {
    AnnotationEntry* entry = findEntry(typeName, methodName, ANNOTATION_TARGET_METHOD);
    return entry ? entry->annotationCount : 0;
}

ReflectionAnnotationInfo* Annotation_getForMethod(const char* typeName,
                                                   const char* methodName,
                                                   int32_t index) {
    AnnotationEntry* entry = findEntry(typeName, methodName, ANNOTATION_TARGET_METHOD);
    if (entry && index >= 0 && index < entry->annotationCount) {
        return &entry->annotations[index];
    }
    return NULL;
}

bool Annotation_methodHas(const char* typeName,
                          const char* methodName,
                          const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, methodName, ANNOTATION_TARGET_METHOD);
    if (!entry) return false;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName) != NULL;
}

ReflectionAnnotationInfo* Annotation_getByNameForMethod(const char* typeName,
                                                         const char* methodName,
                                                         const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, methodName, ANNOTATION_TARGET_METHOD);
    if (!entry) return NULL;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName);
}

// ============================================
// Query Functions - Property Level
// ============================================

int32_t Annotation_getCountForProperty(const char* typeName, const char* propertyName) {
    AnnotationEntry* entry = findEntry(typeName, propertyName, ANNOTATION_TARGET_PROPERTY);
    return entry ? entry->annotationCount : 0;
}

ReflectionAnnotationInfo* Annotation_getForProperty(const char* typeName,
                                                     const char* propertyName,
                                                     int32_t index) {
    AnnotationEntry* entry = findEntry(typeName, propertyName, ANNOTATION_TARGET_PROPERTY);
    if (entry && index >= 0 && index < entry->annotationCount) {
        return &entry->annotations[index];
    }
    return NULL;
}

bool Annotation_propertyHas(const char* typeName,
                            const char* propertyName,
                            const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, propertyName, ANNOTATION_TARGET_PROPERTY);
    if (!entry) return false;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName) != NULL;
}

ReflectionAnnotationInfo* Annotation_getByNameForProperty(const char* typeName,
                                                           const char* propertyName,
                                                           const char* annotationName) {
    AnnotationEntry* entry = findEntry(typeName, propertyName, ANNOTATION_TARGET_PROPERTY);
    if (!entry) return NULL;
    return findAnnotationByName(entry->annotations, entry->annotationCount, annotationName);
}

// ============================================
// Argument Accessor Functions
// ============================================

ReflectionAnnotationArg* Annotation_getArgument(ReflectionAnnotationInfo* annotation,
                                                 const char* argName) {
    if (!annotation) return NULL;
    for (int32_t i = 0; i < annotation->argumentCount; i++) {
        if (strcmp(annotation->arguments[i].name, argName) == 0) {
            return &annotation->arguments[i];
        }
    }
    return NULL;
}

int64_t Annotation_getIntArg(ReflectionAnnotationInfo* annotation,
                             const char* argName,
                             int64_t defaultValue) {
    ReflectionAnnotationArg* arg = Annotation_getArgument(annotation, argName);
    if (arg && arg->valueType == ANNOTATION_ARG_INTEGER) {
        return arg->value.intValue;
    }
    return defaultValue;
}

const char* Annotation_getStringArg(ReflectionAnnotationInfo* annotation,
                                    const char* argName,
                                    const char* defaultValue) {
    ReflectionAnnotationArg* arg = Annotation_getArgument(annotation, argName);
    if (arg && arg->valueType == ANNOTATION_ARG_STRING) {
        return arg->value.stringValue;
    }
    return defaultValue;
}

bool Annotation_getBoolArg(ReflectionAnnotationInfo* annotation,
                           const char* argName,
                           bool defaultValue) {
    ReflectionAnnotationArg* arg = Annotation_getArgument(annotation, argName);
    if (arg && arg->valueType == ANNOTATION_ARG_BOOL) {
        return arg->value.boolValue;
    }
    return defaultValue;
}

double Annotation_getDoubleArg(ReflectionAnnotationInfo* annotation,
                               const char* argName,
                               double defaultValue) {
    ReflectionAnnotationArg* arg = Annotation_getArgument(annotation, argName);
    if (arg && (arg->valueType == ANNOTATION_ARG_DOUBLE || arg->valueType == ANNOTATION_ARG_FLOAT)) {
        return arg->valueType == ANNOTATION_ARG_DOUBLE ? arg->value.doubleValue : (double)arg->value.floatValue;
    }
    return defaultValue;
}

// ============================================
// XXML Language Bindings Implementation
// ============================================

// AnnotationInfo wrapper structure
typedef struct {
    ReflectionAnnotationInfo* info;
} AnnotationInfoWrapper;

// AnnotationArg wrapper structure
typedef struct {
    ReflectionAnnotationArg* arg;
} AnnotationArgWrapper;

void* Language_Reflection_AnnotationInfo_Constructor(void* infoPtr) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)xxml_alloc(sizeof(AnnotationInfoWrapper));
    wrapper->info = (ReflectionAnnotationInfo*)infoPtr;
    return wrapper;
}

void* Language_Reflection_AnnotationInfo_getName(void* self) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)self;
    if (!wrapper || !wrapper->info) return xxml_string_create("");
    return xxml_string_create(wrapper->info->annotationName);
}

void* Language_Reflection_AnnotationInfo_getArgumentCount(void* self) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)self;
    if (!wrapper || !wrapper->info) return (void*)0;
    return (void*)(int64_t)wrapper->info->argumentCount;
}

void* Language_Reflection_AnnotationInfo_getArgument(void* self, void* index) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)self;
    int64_t idx = (int64_t)index;
    if (!wrapper || !wrapper->info || idx < 0 || idx >= wrapper->info->argumentCount) {
        return NULL;
    }
    return Language_Reflection_AnnotationArg_Constructor(&wrapper->info->arguments[idx]);
}

void* Language_Reflection_AnnotationInfo_getArgumentByName(void* self, void* name) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)self;
    const char* nameStr = xxml_string_data(name);
    if (!wrapper || !wrapper->info || !nameStr) return NULL;

    ReflectionAnnotationArg* arg = Annotation_getArgument(wrapper->info, nameStr);
    if (!arg) return NULL;
    return Language_Reflection_AnnotationArg_Constructor(arg);
}

void* Language_Reflection_AnnotationInfo_hasArgument(void* self, void* name) {
    AnnotationInfoWrapper* wrapper = (AnnotationInfoWrapper*)self;
    const char* nameStr = xxml_string_data(name);
    if (!wrapper || !wrapper->info || !nameStr) return (void*)0;

    return (void*)(int64_t)(Annotation_getArgument(wrapper->info, nameStr) != NULL);
}

void* Language_Reflection_AnnotationArg_Constructor(void* argPtr) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)xxml_alloc(sizeof(AnnotationArgWrapper));
    wrapper->arg = (ReflectionAnnotationArg*)argPtr;
    return wrapper;
}

void* Language_Reflection_AnnotationArg_getName(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg) return xxml_string_create("");
    return xxml_string_create(wrapper->arg->name);
}

void* Language_Reflection_AnnotationArg_getType(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg) return (void*)0;
    return (void*)(int64_t)wrapper->arg->valueType;
}

void* Language_Reflection_AnnotationArg_asInteger(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg || wrapper->arg->valueType != ANNOTATION_ARG_INTEGER) {
        return (void*)0;
    }
    return (void*)wrapper->arg->value.intValue;
}

void* Language_Reflection_AnnotationArg_asString(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg || wrapper->arg->valueType != ANNOTATION_ARG_STRING) {
        return xxml_string_create("");
    }
    return xxml_string_create(wrapper->arg->value.stringValue);
}

void* Language_Reflection_AnnotationArg_asBool(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg || wrapper->arg->valueType != ANNOTATION_ARG_BOOL) {
        return (void*)0;
    }
    return (void*)(int64_t)wrapper->arg->value.boolValue;
}

void* Language_Reflection_AnnotationArg_asDouble(void* self) {
    AnnotationArgWrapper* wrapper = (AnnotationArgWrapper*)self;
    if (!wrapper || !wrapper->arg) {
        // Return 0.0 as double bits
        double zero = 0.0;
        return *(void**)&zero;
    }
    double value = 0.0;
    if (wrapper->arg->valueType == ANNOTATION_ARG_DOUBLE) {
        value = wrapper->arg->value.doubleValue;
    } else if (wrapper->arg->valueType == ANNOTATION_ARG_FLOAT) {
        value = (double)wrapper->arg->value.floatValue;
    }
    return *(void**)&value;
}
