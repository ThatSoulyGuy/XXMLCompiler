/**
 * XXML Annotation Processor API Implementation
 *
 * This file implements the C API functions that processors use to interact
 * with the compiler. These functions are linked into processor DLLs.
 */

#include "xxml_processor_api.h"
#include "xxml_llvm_runtime.h"  // For Integer_Constructor, String_Constructor, etc.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==========================================================================
 * Reflection Context Methods Implementation
 * ========================================================================== */

const char* Processor_getTargetKind(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->targetKind ? ctx->targetKind : "";
}

const char* Processor_getTargetName(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->targetName ? ctx->targetName : "";
}

const char* Processor_getTypeName(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->typeName ? ctx->typeName : "";
}

const char* Processor_getClassName(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->className ? ctx->className : "";
}

const char* Processor_getNamespaceName(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->namespaceName ? ctx->namespaceName : "";
}

const char* Processor_getSourceFile(ProcessorReflectionContext* ctx) {
    if (!ctx) return "";
    return ctx->sourceFile ? ctx->sourceFile : "";
}

int Processor_getLineNumber(ProcessorReflectionContext* ctx) {
    if (!ctx) return 0;
    return ctx->lineNumber;
}

int Processor_getColumnNumber(ProcessorReflectionContext* ctx) {
    if (!ctx) return 0;
    return ctx->columnNumber;
}

/* ==========================================================================
 * Annotation Argument Access Implementation
 * ========================================================================== */

ProcessorAnnotationArg* Processor_getArg(ProcessorAnnotationArgs* args, const char* name) {
    if (!args || !name || !args->args) return NULL;

    for (int i = 0; i < args->count; i++) {
        if (args->args[i].name && strcmp(args->args[i].name, name) == 0) {
            return &args->args[i];
        }
    }
    return NULL;
}

ProcessorAnnotationArg* Processor_getArgAt(ProcessorAnnotationArgs* args, int index) {
    if (!args || !args->args || index < 0 || index >= args->count) {
        return NULL;
    }
    return &args->args[index];
}

int Processor_getArgCount(ProcessorAnnotationArgs* args) {
    if (!args) return 0;
    return args->count;
}

const char* Processor_argGetName(ProcessorAnnotationArg* arg) {
    if (!arg || !arg->name) return "";
    return arg->name;
}

int64_t Processor_argAsInt(ProcessorAnnotationArg* arg) {
    if (!arg || arg->type != PROCESSOR_ARG_INT) return 0;
    return arg->value.intValue;
}

const char* Processor_argAsString(ProcessorAnnotationArg* arg) {
    if (!arg || arg->type != PROCESSOR_ARG_STRING) return "";
    return arg->value.stringValue ? arg->value.stringValue : "";
}

int Processor_argAsBool(ProcessorAnnotationArg* arg) {
    if (!arg || arg->type != PROCESSOR_ARG_BOOL) return 0;
    return arg->value.boolValue;
}

double Processor_argAsDouble(ProcessorAnnotationArg* arg) {
    if (!arg || arg->type != PROCESSOR_ARG_DOUBLE) return 0.0;
    return arg->value.doubleValue;
}

/* ==========================================================================
 * Compilation Context Methods Implementation
 *
 * These functions are implemented by the compiler and linked at load time.
 * The implementations here are stubs that will be overridden when the
 * processor DLL is loaded by the compiler.
 *
 * In the processor DLL, these will call back into the compiler through
 * function pointers stored in the _internal field.
 * ========================================================================== */

/* Internal structure passed by compiler - contains error flag */
typedef struct {
    int* errorFlag;       /* Pointer to error flag - set to 1 on error */
    const char* currentFile;
    int currentLine;
    int currentCol;
} ProcessorCompilerState;

void Processor_message(ProcessorCompilationContext* ctx, void* msgObj) {
    if (!ctx || !msgObj) return;

    /* Convert XXML String object to C string */
    const char* msg = String_toCString(msgObj);
    if (!msg) return;

    /* Currently we just print directly - callback system can be enhanced later */
    printf("[Annotation Processor] %s\n", msg);
}

void Processor_warning(ProcessorCompilationContext* ctx, void* msgObj) {
    if (!ctx || !msgObj) return;

    /* Convert XXML String object to C string */
    const char* msg = String_toCString(msgObj);
    if (!msg) return;

    /* Print warning to stderr */
    fprintf(stderr, "warning: %s\n", msg);
}

void Processor_warningAt(ProcessorCompilationContext* ctx, void* msgObj,
                         void* fileObj, int64_t line, int64_t col) {
    if (!ctx || !msgObj) return;

    /* Convert XXML String objects to C strings */
    const char* msg = String_toCString(msgObj);
    const char* file = fileObj ? String_toCString(fileObj) : "<unknown>";
    if (!msg) return;

    /* Print warning with location to stderr */
    fprintf(stderr, "%s:%lld:%lld: warning: %s\n", file ? file : "<unknown>", (long long)line, (long long)col, msg);
}

void Processor_error(ProcessorCompilationContext* ctx, void* msgObj) {
    if (!ctx || !msgObj) return;

    /* Convert XXML String object to C string */
    const char* msg = String_toCString(msgObj);
    if (!msg) return;

    /* Set error flag in compiler state to stop compilation */
    if (ctx->_internal) {
        ProcessorCompilerState* state = (ProcessorCompilerState*)ctx->_internal;
        if (state->errorFlag) {
            *(state->errorFlag) = 1;
        }
    }

    /* Print error to stderr */
    fprintf(stderr, "error: %s\n", msg);
}

void Processor_errorAt(ProcessorCompilationContext* ctx, void* msgObj,
                       void* fileObj, int64_t line, int64_t col) {
    if (!ctx || !msgObj) return;

    /* Convert XXML String objects to C strings */
    const char* msg = String_toCString(msgObj);
    const char* file = fileObj ? String_toCString(fileObj) : "<unknown>";
    if (!msg) return;

    /* Set error flag in compiler state to stop compilation */
    if (ctx->_internal) {
        ProcessorCompilerState* state = (ProcessorCompilerState*)ctx->_internal;
        if (state->errorFlag) {
            *(state->errorFlag) = 1;
        }
    }

    /* Print error with location to stderr */
    fprintf(stderr, "%s:%lld:%lld: error: %s\n", file ? file : "<unknown>", (long long)line, (long long)col, msg);
}

/* ==========================================================================
 * Extended Reflection Context - Internal Structure
 *
 * This structure is populated by the compiler before calling the processor.
 * It contains flattened data from the AST for safe cross-DLL access.
 * ========================================================================== */

typedef struct {
    /* For classes: property info */
    int propertyCount;
    const char** propertyNames;
    const char** propertyTypes;
    const char** propertyOwnerships;

    /* For classes: method info */
    int methodCount;
    const char** methodNames;
    const char** methodReturnTypes;

    /* For classes: class info */
    const char* baseClassName;
    int isFinal;

    /* For methods: parameter info */
    int parameterCount;
    const char** parameterNames;
    const char** parameterTypes;

    /* For methods: method info */
    const char* returnTypeName;
    int isStatic;

    /* For properties: property info */
    int hasDefault;
    const char* ownership;

    /* Target value (for variables with constant initializers) */
    int targetValueType;   /* 0=none, 1=int, 2=string, 3=bool, 4=double */
    void* targetValue;     /* Pointer to the actual value */

    /* Annotation arguments (for accessing @Annotation(arg1=val1, ...) values) */
    ProcessorAnnotationArgs* annotationArgs;
} ProcessorExtendedReflection;

/* ==========================================================================
 * Class Inspection Methods Implementation
 * ========================================================================== */

int Processor_getPropertyCount(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->propertyCount;
}

const char* Processor_getPropertyNameAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->propertyCount || !ext->propertyNames) return "";
    return ext->propertyNames[index] ? ext->propertyNames[index] : "";
}

const char* Processor_getPropertyTypeAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->propertyCount || !ext->propertyTypes) return "";
    return ext->propertyTypes[index] ? ext->propertyTypes[index] : "";
}

const char* Processor_getPropertyOwnershipAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->propertyCount || !ext->propertyOwnerships) return "";
    return ext->propertyOwnerships[index] ? ext->propertyOwnerships[index] : "";
}

int Processor_getMethodCount(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->methodCount;
}

const char* Processor_getMethodNameAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->methodCount || !ext->methodNames) return "";
    return ext->methodNames[index] ? ext->methodNames[index] : "";
}

const char* Processor_getMethodReturnTypeAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->methodCount || !ext->methodReturnTypes) return "";
    return ext->methodReturnTypes[index] ? ext->methodReturnTypes[index] : "";
}

int Processor_hasMethod(ProcessorReflectionContext* ctx, const char* name) {
    if (!ctx || !ctx->_internal || !name) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    for (int i = 0; i < ext->methodCount; i++) {
        if (ext->methodNames && ext->methodNames[i] && strcmp(ext->methodNames[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int Processor_hasProperty(ProcessorReflectionContext* ctx, const char* name) {
    if (!ctx || !ctx->_internal || !name) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    for (int i = 0; i < ext->propertyCount; i++) {
        if (ext->propertyNames && ext->propertyNames[i] && strcmp(ext->propertyNames[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char* Processor_getBaseClassName(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->baseClassName ? ext->baseClassName : "";
}

int Processor_isClassFinal(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->isFinal;
}

/* ==========================================================================
 * Method Inspection Methods Implementation
 * ========================================================================== */

int Processor_getParameterCount(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->parameterCount;
}

const char* Processor_getParameterNameAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->parameterCount || !ext->parameterNames) return "";
    return ext->parameterNames[index] ? ext->parameterNames[index] : "";
}

const char* Processor_getParameterTypeAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (index < 0 || index >= ext->parameterCount || !ext->parameterTypes) return "";
    return ext->parameterTypes[index] ? ext->parameterTypes[index] : "";
}

const char* Processor_getReturnTypeName(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->returnTypeName ? ext->returnTypeName : "";
}

int Processor_isMethodStatic(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->isStatic;
}

/* ==========================================================================
 * Property Inspection Methods Implementation
 * ========================================================================== */

int Processor_hasDefaultValue(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->hasDefault;
}

const char* Processor_getOwnership(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->ownership ? ext->ownership : "";
}

/* ==========================================================================
 * Target Value Access Implementation
 * ========================================================================== */

void* Processor_getTargetValue(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) {
        return NULL;
    }
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;

    // The compiler passes primitive value pointers; we need to wrap them in XXML objects
    // so the processor code can call methods like .toString() on them
    if (!ext->targetValue) {
        return NULL;
    }

    switch (ext->targetValueType) {
        case 1:  // int
            return Integer_Constructor(*(int64_t*)ext->targetValue);
        case 2:  // string
            return String_Constructor((const char*)ext->targetValue);
        case 3:  // bool
            return Bool_Constructor(*(int*)ext->targetValue != 0);
        case 4:  // double
            return Double_Constructor(*(double*)ext->targetValue);
        default:
            return NULL;
    }
}

int Processor_hasTargetValue(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->targetValueType != 0 && ext->targetValue != NULL;
}

int Processor_getTargetValueType(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    return ext->targetValueType;
}

/* ==========================================================================
 * Annotation Argument Access (through ReflectionContext)
 *
 * These functions allow processors to access the arguments passed to the
 * annotation at the usage site, e.g., @MyAnnotation(name = "value", count = 42)
 * ========================================================================== */

int Processor_getAnnotationArgCount(ProcessorReflectionContext* ctx) {
    if (!ctx || !ctx->_internal) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return 0;
    return ext->annotationArgs->count;
}

void* Processor_getAnnotationArg(ProcessorReflectionContext* ctx, void* nameStr) {
    if (!ctx || !ctx->_internal || !nameStr) {
        return NULL;
    }

    // Convert XXML String to C string
    const char* name = String_toCString(nameStr);
    if (!name) return NULL;

    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return NULL;

    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            // Create and return XXML object based on type
            ProcessorAnnotationArg* arg = &ext->annotationArgs->args[i];
            switch (arg->type) {
                case PROCESSOR_ARG_INT:
                    return Integer_Constructor(arg->value.intValue);
                case PROCESSOR_ARG_STRING:
                    return String_Constructor(arg->value.stringValue);
                case PROCESSOR_ARG_BOOL:
                    return Bool_Constructor(arg->value.boolValue != 0);
                case PROCESSOR_ARG_DOUBLE:
                    return Double_Constructor(arg->value.doubleValue);
                default:
                    return NULL;
            }
        }
    }
    return NULL;
}

const char* Processor_getAnnotationArgNameAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs || index < 0 || index >= ext->annotationArgs->count) return "";
    return ext->annotationArgs->args[index].name ? ext->annotationArgs->args[index].name : "";
}

int Processor_getAnnotationArgTypeAt(ProcessorReflectionContext* ctx, int index) {
    if (!ctx || !ctx->_internal) return -1;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs || index < 0 || index >= ext->annotationArgs->count) return -1;
    return ext->annotationArgs->args[index].type;
}

int Processor_hasAnnotationArg(ProcessorReflectionContext* ctx, const char* name) {
    if (!ctx || !ctx->_internal || !name) return 0;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return 0;
    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int64_t Processor_getAnnotationIntArg(ProcessorReflectionContext* ctx, const char* name, int64_t defaultValue) {
    if (!ctx || !ctx->_internal || !name) return defaultValue;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return defaultValue;
    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            if (ext->annotationArgs->args[i].type == PROCESSOR_ARG_INT) {
                return ext->annotationArgs->args[i].value.intValue;
            }
            return defaultValue;
        }
    }
    return defaultValue;
}

const char* Processor_getAnnotationStringArg(ProcessorReflectionContext* ctx, const char* name, const char* defaultValue) {
    if (!ctx || !ctx->_internal || !name) return defaultValue ? defaultValue : "";
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return defaultValue ? defaultValue : "";
    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            if (ext->annotationArgs->args[i].type == PROCESSOR_ARG_STRING) {
                return ext->annotationArgs->args[i].value.stringValue ? ext->annotationArgs->args[i].value.stringValue : "";
            }
            return defaultValue ? defaultValue : "";
        }
    }
    return defaultValue ? defaultValue : "";
}

int Processor_getAnnotationBoolArg(ProcessorReflectionContext* ctx, const char* name, int defaultValue) {
    if (!ctx || !ctx->_internal || !name) return defaultValue;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return defaultValue;
    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            if (ext->annotationArgs->args[i].type == PROCESSOR_ARG_BOOL) {
                return ext->annotationArgs->args[i].value.boolValue;
            }
            return defaultValue;
        }
    }
    return defaultValue;
}

double Processor_getAnnotationDoubleArg(ProcessorReflectionContext* ctx, const char* name, double defaultValue) {
    if (!ctx || !ctx->_internal || !name) return defaultValue;
    ProcessorExtendedReflection* ext = (ProcessorExtendedReflection*)ctx->_internal;
    if (!ext->annotationArgs) return defaultValue;
    for (int i = 0; i < ext->annotationArgs->count; i++) {
        if (ext->annotationArgs->args[i].name && strcmp(ext->annotationArgs->args[i].name, name) == 0) {
            if (ext->annotationArgs->args[i].type == PROCESSOR_ARG_DOUBLE) {
                return ext->annotationArgs->args[i].value.doubleValue;
            }
            return defaultValue;
        }
    }
    return defaultValue;
}
