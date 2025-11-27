#include "xxml_reflection_runtime.h"
#include "xxml_llvm_runtime.h"
#include <string.h>
#include <stdio.h>

// ============================================
// Global Type Registry Implementation
// ============================================

// Simple dynamic array for type registry
static ReflectionTypeInfo** typeRegistry = NULL;
static int32_t typeRegistryCount = 0;
static int32_t typeRegistryCapacity = 0;

void* Reflection_registerType(ReflectionTypeInfo* typeInfo) {
    if (!typeInfo) {
        return NULL;
    }

    // Check if already registered (avoid duplicates)
    for (int32_t i = 0; i < typeRegistryCount; i++) {
        if (strcmp(typeRegistry[i]->fullName, typeInfo->fullName) == 0) {
            return typeRegistry[i];  // Already registered
        }
    }

    // Expand registry if needed
    if (typeRegistryCount >= typeRegistryCapacity) {
        int32_t newCapacity = (typeRegistryCapacity == 0) ? 16 : typeRegistryCapacity * 2;
        ReflectionTypeInfo** newRegistry = (ReflectionTypeInfo**)xxml_malloc(
            sizeof(ReflectionTypeInfo*) * newCapacity);

        if (typeRegistry) {
            xxml_memcpy(newRegistry, typeRegistry,
                       sizeof(ReflectionTypeInfo*) * typeRegistryCount);
            xxml_free(typeRegistry);
        }

        typeRegistry = newRegistry;
        typeRegistryCapacity = newCapacity;
    }

    // Add to registry
    typeRegistry[typeRegistryCount++] = typeInfo;
    return typeInfo;
}

ReflectionTypeInfo* Reflection_getTypeInfo(const char* typeName) {
    if (!typeName) {
        return NULL;
    }

    // First try exact match on fullName
    for (int32_t i = 0; i < typeRegistryCount; i++) {
        if (strcmp(typeRegistry[i]->fullName, typeName) == 0) {
            return typeRegistry[i];
        }
    }

    // If no exact match, try matching just the simple name
    // This allows GetType<String> to find Language::Core::String
    for (int32_t i = 0; i < typeRegistryCount; i++) {
        if (strcmp(typeRegistry[i]->name, typeName) == 0) {
            return typeRegistry[i];
        }
    }

    return NULL;
}

int32_t Reflection_getTypeCount() {
    return typeRegistryCount;
}

const char** Reflection_getAllTypeNames() {
    const char** names = (const char**)xxml_malloc(
        sizeof(const char*) * typeRegistryCount);

    for (int32_t i = 0; i < typeRegistryCount; i++) {
        names[i] = typeRegistry[i]->fullName;
    }

    return names;
}

// ============================================
// XXML Syscall Interface Functions
// These are the functions called from XXML code via Syscall::
// ============================================

// Type lookup
void* xxml_reflection_getTypeByName(const char* typeName) {
    return (void*)Reflection_getTypeInfo(typeName);
}

// Type info accessors
const char* xxml_reflection_type_getName(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->name : "";
}

const char* xxml_reflection_type_getFullName(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->fullName : "";
}

const char* xxml_reflection_type_getNamespace(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (info->namespaceName ? info->namespaceName : "") : "";
}

int64_t xxml_reflection_type_isTemplate(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (info->isTemplate ? 1 : 0) : 0;
}

int64_t xxml_reflection_type_getTemplateParamCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->templateParamCount : 0;
}

int64_t xxml_reflection_type_getPropertyCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->propertyCount : 0;
}

void* xxml_reflection_type_getProperty(void* typeInfo, int64_t index) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || index < 0 || index >= info->propertyCount) {
        return NULL;
    }
    return (void*)&info->properties[index];
}

void* xxml_reflection_type_getPropertyByName(void* typeInfo, const char* name) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || !name) {
        return NULL;
    }
    for (int32_t i = 0; i < info->propertyCount; i++) {
        if (strcmp(info->properties[i].name, name) == 0) {
            return (void*)&info->properties[i];
        }
    }
    return NULL;
}

int64_t xxml_reflection_type_getMethodCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->methodCount : 0;
}

void* xxml_reflection_type_getMethod(void* typeInfo, int64_t index) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || index < 0 || index >= info->methodCount) {
        return NULL;
    }
    return (void*)&info->methods[index];
}

void* xxml_reflection_type_getMethodByName(void* typeInfo, const char* name) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || !name) {
        return NULL;
    }
    for (int32_t i = 0; i < info->methodCount; i++) {
        if (strcmp(info->methods[i].name, name) == 0) {
            return (void*)&info->methods[i];
        }
    }
    return NULL;
}

int64_t xxml_reflection_type_getInstanceSize(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (int64_t)info->instanceSize : 0;
}

// Property info accessors
const char* xxml_reflection_property_getName(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->name : "";
}

const char* xxml_reflection_property_getTypeName(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->typeName : "";
}

int64_t xxml_reflection_property_getOwnership(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->ownership : 0;
}

int64_t xxml_reflection_property_getOffset(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? (int64_t)info->offset : 0;
}

// Method info accessors
const char* xxml_reflection_method_getName(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->name : "";
}

const char* xxml_reflection_method_getReturnType(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->returnType : "";
}

int64_t xxml_reflection_method_getReturnOwnership(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->returnOwnership : 0;
}

int64_t xxml_reflection_method_getParameterCount(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->parameterCount : 0;
}

void* xxml_reflection_method_getParameter(void* methodInfo, int64_t index) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    if (!info || index < 0 || index >= info->parameterCount) {
        return NULL;
    }
    return (void*)&info->parameters[index];
}

int64_t xxml_reflection_method_isStatic(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? (info->isStatic ? 1 : 0) : 0;
}

int64_t xxml_reflection_method_isConstructor(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? (info->isConstructor ? 1 : 0) : 0;
}

// Parameter info accessors
const char* xxml_reflection_parameter_getName(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->name : "";
}

const char* xxml_reflection_parameter_getTypeName(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->typeName : "";
}

int64_t xxml_reflection_parameter_getOwnership(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->ownership : 0;
}
