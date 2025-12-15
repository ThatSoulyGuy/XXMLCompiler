/**
 * XXML Derive API Implementation
 *
 * This file implements the C API functions that derives use to interact
 * with the compiler. These functions are linked into derive DLLs.
 */

#include "xxml_derive_api.h"
#include "xxml_llvm_runtime.h"  /* For Integer_Constructor, String_Constructor, etc. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==========================================================================
 * Extended Internal Structures
 *
 * These structures are populated by the compiler before calling the derive.
 * They contain flattened data from the AST for safe cross-DLL access.
 * ========================================================================== */

/**
 * Extended class information - populated by compiler
 */
typedef struct {
    /* Property information */
    int propertyCount;
    const char** propertyNames;
    const char** propertyTypes;
    const char** propertyOwnerships;

    /* Method information */
    int methodCount;
    const char** methodNames;
    const char** methodReturnTypes;

    /* Class information */
    const char* baseClassName;
    int isFinal;
} DeriveClassInfo;

/**
 * Type system query interface - populated by compiler
 * Function pointers for type system queries
 */
typedef struct {
    /* Check if a type has a method */
    int (*hasMethod)(void* compilerState, const char* typeName, const char* methodName);
    /* Check if a type has a property */
    int (*hasProperty)(void* compilerState, const char* typeName, const char* propertyName);
    /* Check if a type implements a trait */
    int (*implementsTrait)(void* compilerState, const char* typeName, const char* traitName);
    /* Check if type is built-in */
    int (*isBuiltin)(void* compilerState, const char* typeName);
    /* Opaque pointer to compiler state */
    void* compilerState;
} DeriveTypeSystem;

/**
 * Code generation state - collects generated methods/properties
 */
typedef struct {
    /* Generated methods */
    int methodCapacity;
    int methodCount;
    DeriveGeneratedMethod* methods;

    /* Generated properties */
    int propertyCapacity;
    int propertyCount;
    DeriveGeneratedProperty* properties;

    /* Error state */
    int hasErrors;
} DeriveCodeGen;

/**
 * Diagnostics state - for error/warning reporting
 */
typedef struct {
    int* errorFlag;           /* Set to 1 when error is emitted */
    const char* deriveName;   /* Name of the derive being processed */
    const char* targetClass;  /* Name of the target class */
    const char* sourceFile;
    int lineNumber;
    int columnNumber;
} DeriveDiagnostics;

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Duplicate a string - caller must free
 */
static char* derive_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

/**
 * Add a method to the code generation state
 */
static void derive_addMethodInternal(DeriveCodeGen* gen, const char* name,
                                     const char* returnType, const char* parameters,
                                     const char* body, int isStatic) {
    if (!gen) return;

    /* Grow array if needed */
    if (gen->methodCount >= gen->methodCapacity) {
        int newCapacity = gen->methodCapacity == 0 ? 4 : gen->methodCapacity * 2;
        DeriveGeneratedMethod* newMethods = (DeriveGeneratedMethod*)realloc(
            gen->methods, newCapacity * sizeof(DeriveGeneratedMethod));
        if (!newMethods) return;
        gen->methods = newMethods;
        gen->methodCapacity = newCapacity;
    }

    /* Add method */
    DeriveGeneratedMethod* m = &gen->methods[gen->methodCount++];
    m->name = derive_strdup(name);
    m->returnType = derive_strdup(returnType);
    m->parameters = derive_strdup(parameters);
    m->body = derive_strdup(body);
    m->isStatic = isStatic;
}

/**
 * Add a property to the code generation state
 */
static void derive_addPropertyInternal(DeriveCodeGen* gen, const char* name,
                                       const char* type, const char* ownership,
                                       const char* defaultValue) {
    if (!gen) return;

    /* Grow array if needed */
    if (gen->propertyCount >= gen->propertyCapacity) {
        int newCapacity = gen->propertyCapacity == 0 ? 4 : gen->propertyCapacity * 2;
        DeriveGeneratedProperty* newProps = (DeriveGeneratedProperty*)realloc(
            gen->properties, newCapacity * sizeof(DeriveGeneratedProperty));
        if (!newProps) return;
        gen->properties = newProps;
        gen->propertyCapacity = newCapacity;
    }

    /* Add property */
    DeriveGeneratedProperty* p = &gen->properties[gen->propertyCount++];
    p->name = derive_strdup(name);
    p->type = derive_strdup(type);
    p->ownership = derive_strdup(ownership);
    p->defaultValue = derive_strdup(defaultValue);
}

/* ==========================================================================
 * Target Class Introspection Implementation
 * ========================================================================== */

void* Derive_getClassName(DeriveContext* ctx) {
    if (!ctx) return String_Constructor("");
    return String_Constructor(ctx->className ? ctx->className : "");
}

void* Derive_getNamespaceName(DeriveContext* ctx) {
    if (!ctx) return String_Constructor("");
    return String_Constructor(ctx->namespaceName ? ctx->namespaceName : "");
}

void* Derive_getSourceFile(DeriveContext* ctx) {
    if (!ctx) return String_Constructor("");
    return String_Constructor(ctx->sourceFile ? ctx->sourceFile : "");
}

int Derive_getLineNumber(DeriveContext* ctx) {
    if (!ctx) return 0;
    return ctx->lineNumber;
}

int Derive_getColumnNumber(DeriveContext* ctx) {
    if (!ctx) return 0;
    return ctx->columnNumber;
}

/* ==========================================================================
 * Property Inspection Implementation
 * ========================================================================== */

int Derive_getPropertyCount(DeriveContext* ctx) {
    if (!ctx || !ctx->_classInfo) return 0;
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    return info->propertyCount;
}

void* Derive_getPropertyNameAt(DeriveContext* ctx, int index) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    if (index < 0 || index >= info->propertyCount || !info->propertyNames) {
        return String_Constructor("");
    }
    return String_Constructor(info->propertyNames[index] ? info->propertyNames[index] : "");
}

void* Derive_getPropertyTypeAt(DeriveContext* ctx, int index) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    if (index < 0 || index >= info->propertyCount || !info->propertyTypes) {
        return String_Constructor("");
    }
    return String_Constructor(info->propertyTypes[index] ? info->propertyTypes[index] : "");
}

void* Derive_getPropertyOwnershipAt(DeriveContext* ctx, int index) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    if (index < 0 || index >= info->propertyCount || !info->propertyOwnerships) {
        return String_Constructor("");
    }
    return String_Constructor(info->propertyOwnerships[index] ? info->propertyOwnerships[index] : "");
}

void* Derive_hasProperty(DeriveContext* ctx, void* nameStr) {
    if (!ctx || !ctx->_classInfo || !nameStr) return Bool_Constructor(0);
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    const char* name = String_toCString(nameStr);
    if (!name) return Bool_Constructor(0);

    for (int i = 0; i < info->propertyCount; i++) {
        if (info->propertyNames && info->propertyNames[i] &&
            strcmp(info->propertyNames[i], name) == 0) {
            return Bool_Constructor(1);
        }
    }
    return Bool_Constructor(0);
}

/* ==========================================================================
 * Method Inspection Implementation
 * ========================================================================== */

int Derive_getMethodCount(DeriveContext* ctx) {
    if (!ctx || !ctx->_classInfo) return 0;
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    return info->methodCount;
}

void* Derive_getMethodNameAt(DeriveContext* ctx, int index) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    if (index < 0 || index >= info->methodCount || !info->methodNames) {
        return String_Constructor("");
    }
    return String_Constructor(info->methodNames[index] ? info->methodNames[index] : "");
}

void* Derive_getMethodReturnTypeAt(DeriveContext* ctx, int index) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    if (index < 0 || index >= info->methodCount || !info->methodReturnTypes) {
        return String_Constructor("");
    }
    return String_Constructor(info->methodReturnTypes[index] ? info->methodReturnTypes[index] : "");
}

void* Derive_hasMethod(DeriveContext* ctx, void* nameStr) {
    if (!ctx || !ctx->_classInfo || !nameStr) return Bool_Constructor(0);
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    const char* name = String_toCString(nameStr);
    if (!name) return Bool_Constructor(0);

    for (int i = 0; i < info->methodCount; i++) {
        if (info->methodNames && info->methodNames[i] &&
            strcmp(info->methodNames[i], name) == 0) {
            return Bool_Constructor(1);
        }
    }
    return Bool_Constructor(0);
}

void* Derive_getBaseClassName(DeriveContext* ctx) {
    if (!ctx || !ctx->_classInfo) return String_Constructor("");
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    return String_Constructor(info->baseClassName ? info->baseClassName : "");
}

void* Derive_isClassFinal(DeriveContext* ctx) {
    if (!ctx || !ctx->_classInfo) return Bool_Constructor(0);
    DeriveClassInfo* info = (DeriveClassInfo*)ctx->_classInfo;
    return Bool_Constructor(info->isFinal);
}

/* ==========================================================================
 * Type System Query Implementation
 * ========================================================================== */

void* Derive_typeHasMethod(DeriveContext* ctx, void* typeNameStr, void* methodNameStr) {
    if (!ctx || !ctx->_typeSystem || !typeNameStr || !methodNameStr) {
        return Bool_Constructor(0);
    }
    DeriveTypeSystem* ts = (DeriveTypeSystem*)ctx->_typeSystem;
    if (!ts->hasMethod) return Bool_Constructor(0);

    const char* typeName = String_toCString(typeNameStr);
    const char* methodName = String_toCString(methodNameStr);
    if (!typeName || !methodName) return Bool_Constructor(0);

    int result = ts->hasMethod(ts->compilerState, typeName, methodName);
    return Bool_Constructor(result);
}

void* Derive_typeHasProperty(DeriveContext* ctx, void* typeNameStr, void* propertyNameStr) {
    if (!ctx || !ctx->_typeSystem || !typeNameStr || !propertyNameStr) {
        return Bool_Constructor(0);
    }
    DeriveTypeSystem* ts = (DeriveTypeSystem*)ctx->_typeSystem;
    if (!ts->hasProperty) return Bool_Constructor(0);

    const char* typeName = String_toCString(typeNameStr);
    const char* propertyName = String_toCString(propertyNameStr);
    if (!typeName || !propertyName) return Bool_Constructor(0);

    int result = ts->hasProperty(ts->compilerState, typeName, propertyName);
    return Bool_Constructor(result);
}

void* Derive_typeImplementsTrait(DeriveContext* ctx, void* typeNameStr, void* traitNameStr) {
    if (!ctx || !ctx->_typeSystem || !typeNameStr || !traitNameStr) {
        return Bool_Constructor(0);
    }
    DeriveTypeSystem* ts = (DeriveTypeSystem*)ctx->_typeSystem;
    if (!ts->implementsTrait) return Bool_Constructor(0);

    const char* typeName = String_toCString(typeNameStr);
    const char* traitName = String_toCString(traitNameStr);
    if (!typeName || !traitName) return Bool_Constructor(0);

    int result = ts->implementsTrait(ts->compilerState, typeName, traitName);
    return Bool_Constructor(result);
}

void* Derive_isBuiltinType(DeriveContext* ctx, void* typeNameStr) {
    if (!ctx || !ctx->_typeSystem || !typeNameStr) {
        return Bool_Constructor(0);
    }
    DeriveTypeSystem* ts = (DeriveTypeSystem*)ctx->_typeSystem;
    if (!ts->isBuiltin) return Bool_Constructor(0);

    const char* typeName = String_toCString(typeNameStr);
    if (!typeName) return Bool_Constructor(0);

    int result = ts->isBuiltin(ts->compilerState, typeName);
    return Bool_Constructor(result);
}

/* ==========================================================================
 * Code Generation Implementation
 * ========================================================================== */

void Derive_addMethod(DeriveContext* ctx, void* nameStr, void* returnTypeStr,
                      void* parametersStr, void* bodyStr) {
    if (!ctx || !ctx->_codeGen || !nameStr || !returnTypeStr || !bodyStr) return;
    DeriveCodeGen* gen = (DeriveCodeGen*)ctx->_codeGen;

    const char* name = String_toCString(nameStr);
    const char* returnType = String_toCString(returnTypeStr);
    const char* parameters = parametersStr ? String_toCString(parametersStr) : "";
    const char* body = String_toCString(bodyStr);

    if (!name || !returnType || !body) return;

    derive_addMethodInternal(gen, name, returnType, parameters, body, 0);
}

void Derive_addStaticMethod(DeriveContext* ctx, void* nameStr, void* returnTypeStr,
                            void* parametersStr, void* bodyStr) {
    if (!ctx || !ctx->_codeGen || !nameStr || !returnTypeStr || !bodyStr) return;
    DeriveCodeGen* gen = (DeriveCodeGen*)ctx->_codeGen;

    const char* name = String_toCString(nameStr);
    const char* returnType = String_toCString(returnTypeStr);
    const char* parameters = parametersStr ? String_toCString(parametersStr) : "";
    const char* body = String_toCString(bodyStr);

    if (!name || !returnType || !body) return;

    derive_addMethodInternal(gen, name, returnType, parameters, body, 1);
}

void Derive_addProperty(DeriveContext* ctx, void* nameStr, void* typeStr,
                        void* ownershipStr, void* defaultValueStr) {
    if (!ctx || !ctx->_codeGen || !nameStr || !typeStr) return;
    DeriveCodeGen* gen = (DeriveCodeGen*)ctx->_codeGen;

    const char* name = String_toCString(nameStr);
    const char* type = String_toCString(typeStr);
    const char* ownership = ownershipStr ? String_toCString(ownershipStr) : "";
    const char* defaultValue = defaultValueStr ? String_toCString(defaultValueStr) : "";

    if (!name || !type) return;

    derive_addPropertyInternal(gen, name, type, ownership, defaultValue);
}

/* ==========================================================================
 * Diagnostics Implementation
 * ========================================================================== */

void Derive_error(DeriveContext* ctx, void* messageStr) {
    if (!ctx || !messageStr) return;

    const char* message = String_toCString(messageStr);
    if (!message) return;

    /* Set error flag */
    if (ctx->_codeGen) {
        DeriveCodeGen* gen = (DeriveCodeGen*)ctx->_codeGen;
        gen->hasErrors = 1;
    }
    if (ctx->_diagnostics) {
        DeriveDiagnostics* diag = (DeriveDiagnostics*)ctx->_diagnostics;
        if (diag->errorFlag) {
            *(diag->errorFlag) = 1;
        }
        /* Print error with context */
        fprintf(stderr, "%s:%d:%d: error: in derive '%s' for class '%s': %s\n",
                diag->sourceFile ? diag->sourceFile : "<unknown>",
                diag->lineNumber, diag->columnNumber,
                diag->deriveName ? diag->deriveName : "<unknown>",
                diag->targetClass ? diag->targetClass : "<unknown>",
                message);
    } else {
        fprintf(stderr, "error: %s\n", message);
    }
}

void Derive_warning(DeriveContext* ctx, void* messageStr) {
    if (!ctx || !messageStr) return;

    const char* message = String_toCString(messageStr);
    if (!message) return;

    if (ctx->_diagnostics) {
        DeriveDiagnostics* diag = (DeriveDiagnostics*)ctx->_diagnostics;
        fprintf(stderr, "%s:%d:%d: warning: in derive '%s' for class '%s': %s\n",
                diag->sourceFile ? diag->sourceFile : "<unknown>",
                diag->lineNumber, diag->columnNumber,
                diag->deriveName ? diag->deriveName : "<unknown>",
                diag->targetClass ? diag->targetClass : "<unknown>",
                message);
    } else {
        fprintf(stderr, "warning: %s\n", message);
    }
}

void Derive_message(DeriveContext* ctx, void* messageStr) {
    if (!ctx || !messageStr) return;

    const char* message = String_toCString(messageStr);
    if (!message) return;

    if (ctx->_diagnostics) {
        DeriveDiagnostics* diag = (DeriveDiagnostics*)ctx->_diagnostics;
        printf("[Derive %s] %s\n",
               diag->deriveName ? diag->deriveName : "<unknown>",
               message);
    } else {
        printf("[Derive] %s\n", message);
    }
}

void* Derive_hasErrors(DeriveContext* ctx) {
    if (!ctx || !ctx->_codeGen) return Bool_Constructor(0);
    DeriveCodeGen* gen = (DeriveCodeGen*)ctx->_codeGen;
    return Bool_Constructor(gen->hasErrors);
}

/* ==========================================================================
 * xxml_ Prefixed Wrapper Functions
 *
 * These wrapper functions provide the xxml_ prefixed names that the LLVM IR
 * expects (due to Syscall name mangling). They simply forward to the
 * implementation functions above.
 * ========================================================================== */

/* Target Class Introspection Wrappers */
void* xxml_Derive_getClassName(DeriveContext* ctx) {
    return Derive_getClassName(ctx);
}

void* xxml_Derive_getNamespaceName(DeriveContext* ctx) {
    return Derive_getNamespaceName(ctx);
}

void* xxml_Derive_getSourceFile(DeriveContext* ctx) {
    return Derive_getSourceFile(ctx);
}

int xxml_Derive_getLineNumber(DeriveContext* ctx) {
    return Derive_getLineNumber(ctx);
}

int xxml_Derive_getColumnNumber(DeriveContext* ctx) {
    return Derive_getColumnNumber(ctx);
}

/* Property Inspection Wrappers */
int xxml_Derive_getPropertyCount(DeriveContext* ctx) {
    return Derive_getPropertyCount(ctx);
}

void* xxml_Derive_getPropertyNameAt(DeriveContext* ctx, int index) {
    return Derive_getPropertyNameAt(ctx, index);
}

void* xxml_Derive_getPropertyTypeAt(DeriveContext* ctx, int index) {
    return Derive_getPropertyTypeAt(ctx, index);
}

void* xxml_Derive_getPropertyOwnershipAt(DeriveContext* ctx, int index) {
    return Derive_getPropertyOwnershipAt(ctx, index);
}

void* xxml_Derive_hasProperty(DeriveContext* ctx, void* nameStr) {
    return Derive_hasProperty(ctx, nameStr);
}

/* Method Inspection Wrappers */
int xxml_Derive_getMethodCount(DeriveContext* ctx) {
    return Derive_getMethodCount(ctx);
}

void* xxml_Derive_getMethodNameAt(DeriveContext* ctx, int index) {
    return Derive_getMethodNameAt(ctx, index);
}

void* xxml_Derive_getMethodReturnTypeAt(DeriveContext* ctx, int index) {
    return Derive_getMethodReturnTypeAt(ctx, index);
}

void* xxml_Derive_hasMethod(DeriveContext* ctx, void* nameStr) {
    return Derive_hasMethod(ctx, nameStr);
}

void* xxml_Derive_getBaseClassName(DeriveContext* ctx) {
    return Derive_getBaseClassName(ctx);
}

void* xxml_Derive_isClassFinal(DeriveContext* ctx) {
    return Derive_isClassFinal(ctx);
}

/* Type System Query Wrappers */
void* xxml_Derive_typeHasMethod(DeriveContext* ctx, void* typeName, void* methodName) {
    return Derive_typeHasMethod(ctx, typeName, methodName);
}

void* xxml_Derive_typeHasProperty(DeriveContext* ctx, void* typeName, void* propertyName) {
    return Derive_typeHasProperty(ctx, typeName, propertyName);
}

void* xxml_Derive_typeImplementsTrait(DeriveContext* ctx, void* typeName, void* traitName) {
    return Derive_typeImplementsTrait(ctx, typeName, traitName);
}

void* xxml_Derive_isBuiltinType(DeriveContext* ctx, void* typeName) {
    return Derive_isBuiltinType(ctx, typeName);
}

/* Code Generation Wrappers */
void xxml_Derive_addMethod(DeriveContext* ctx, void* name, void* returnType,
                           void* parameters, void* body) {
    Derive_addMethod(ctx, name, returnType, parameters, body);
}

void xxml_Derive_addStaticMethod(DeriveContext* ctx, void* name, void* returnType,
                                 void* parameters, void* body) {
    Derive_addStaticMethod(ctx, name, returnType, parameters, body);
}

void xxml_Derive_addProperty(DeriveContext* ctx, void* name, void* type,
                             void* ownership, void* defaultValue) {
    Derive_addProperty(ctx, name, type, ownership, defaultValue);
}

/* Diagnostics Wrappers */
void xxml_Derive_error(DeriveContext* ctx, void* message) {
    Derive_error(ctx, message);
}

void xxml_Derive_warning(DeriveContext* ctx, void* message) {
    Derive_warning(ctx, message);
}

void xxml_Derive_message(DeriveContext* ctx, void* message) {
    Derive_message(ctx, message);
}

void* xxml_Derive_hasErrors(DeriveContext* ctx) {
    return Derive_hasErrors(ctx);
}

/* ==========================================================================
 * Compiler-Side Helper Functions
 *
 * These functions are called by the compiler to create/destroy contexts
 * and retrieve generated code. They are not exported to derive DLLs.
 * ========================================================================== */

/**
 * Create a code generation state - called by compiler before invoking derive
 */
DeriveCodeGen* DeriveCodeGen_create(void) {
    DeriveCodeGen* gen = (DeriveCodeGen*)calloc(1, sizeof(DeriveCodeGen));
    return gen;
}

/**
 * Free code generation state and all generated code
 */
void DeriveCodeGen_destroy(DeriveCodeGen* gen) {
    if (!gen) return;

    /* Free methods */
    for (int i = 0; i < gen->methodCount; i++) {
        free((void*)gen->methods[i].name);
        free((void*)gen->methods[i].returnType);
        free((void*)gen->methods[i].parameters);
        free((void*)gen->methods[i].body);
    }
    free(gen->methods);

    /* Free properties */
    for (int i = 0; i < gen->propertyCount; i++) {
        free((void*)gen->properties[i].name);
        free((void*)gen->properties[i].type);
        free((void*)gen->properties[i].ownership);
        free((void*)gen->properties[i].defaultValue);
    }
    free(gen->properties);

    free(gen);
}

/**
 * Get the result of code generation
 */
DeriveResult DeriveCodeGen_getResult(DeriveCodeGen* gen) {
    DeriveResult result = {0};
    if (!gen) return result;

    result.methodCount = gen->methodCount;
    result.methods = gen->methods;
    result.propertyCount = gen->propertyCount;
    result.properties = gen->properties;
    result.hasErrors = gen->hasErrors;

    return result;
}
