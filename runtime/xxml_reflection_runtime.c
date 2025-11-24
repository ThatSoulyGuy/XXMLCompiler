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

    // Linear search through registry
    for (int32_t i = 0; i < typeRegistryCount; i++) {
        if (strcmp(typeRegistry[i]->fullName, typeName) == 0) {
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
