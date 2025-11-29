/**
 * XXML Annotation Processor API
 *
 * This header defines the C ABI interface between the XXML compiler and
 * user-defined annotation processors compiled to DLLs.
 *
 * Processors are compiled separately and loaded dynamically during main compilation.
 */

#ifndef XXML_PROCESSOR_API_H
#define XXML_PROCESSOR_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Processor Context Structures
 * ========================================================================== */

/**
 * ReflectionContext - Provides information about the annotated element
 *
 * This struct is passed to processors to give them introspection capabilities
 * about the code element that was annotated.
 */
typedef struct {
    const char* targetKind;      /* "class", "method", "property", "variable" */
    const char* targetName;      /* Name of the annotated element */
    const char* typeName;        /* Type of the element (for properties/variables) */
    const char* className;       /* Containing class (for methods/properties) */
    const char* namespaceName;   /* Containing namespace */
    const char* sourceFile;      /* Source file path */
    int lineNumber;              /* Line number in source */
    int columnNumber;            /* Column number in source */
    void* _internal;             /* Opaque pointer to AST node for deep inspection */
} ProcessorReflectionContext;

/**
 * CompilationContext - Provides compiler interaction capabilities
 *
 * This struct allows processors to emit messages, warnings, and errors
 * during compilation.
 */
typedef struct {
    void* _internal;             /* Opaque pointer to compiler state */
} ProcessorCompilationContext;

/**
 * Annotation argument value types
 */
typedef enum {
    PROCESSOR_ARG_INT = 0,
    PROCESSOR_ARG_STRING = 1,
    PROCESSOR_ARG_BOOL = 2,
    PROCESSOR_ARG_DOUBLE = 3
} ProcessorArgType;

/**
 * AnnotationArg - A single annotation argument
 */
typedef struct {
    const char* name;            /* Argument name */
    ProcessorArgType type;       /* Value type */
    union {
        int64_t intValue;
        const char* stringValue;
        int boolValue;
        double doubleValue;
    } value;
} ProcessorAnnotationArg;

/**
 * AnnotationArgs - Collection of annotation arguments
 */
typedef struct {
    int count;                   /* Number of arguments */
    ProcessorAnnotationArg* args; /* Array of arguments */
} ProcessorAnnotationArgs;

/* ==========================================================================
 * Compilation Context Methods
 * These are implemented by the compiler and called by processors
 * Note: msg parameters are XXML String objects (void*), not const char*
 * ========================================================================== */

/**
 * Emit an informational message during compilation
 * @param msg XXML String object
 */
void Processor_message(ProcessorCompilationContext* ctx, void* msg);

/**
 * Emit a warning at the annotation location
 * @param msg XXML String object
 */
void Processor_warning(ProcessorCompilationContext* ctx, void* msg);

/**
 * Emit a warning at a specific location
 * @param msg XXML String object
 * @param file XXML String object
 */
void Processor_warningAt(ProcessorCompilationContext* ctx, void* msg,
                         void* file, int64_t line, int64_t col);

/**
 * Emit an error at the annotation location (will fail compilation)
 * @param msg XXML String object
 */
void Processor_error(ProcessorCompilationContext* ctx, void* msg);

/**
 * Emit an error at a specific location
 * @param msg XXML String object
 * @param file XXML String object
 */
void Processor_errorAt(ProcessorCompilationContext* ctx, void* msg,
                       void* file, int64_t line, int64_t col);

/* ==========================================================================
 * Reflection Context Methods
 * Note: String-returning methods return XXML String objects (void*), not const char*
 * ========================================================================== */

/**
 * Get the kind of the annotated target
 * Returns: XXML String object with "class", "method", "property", or "variable"
 */
void* Processor_getTargetKind(ProcessorReflectionContext* ctx);

/**
 * Get the name of the annotated target
 * Returns: XXML String object
 */
void* Processor_getTargetName(ProcessorReflectionContext* ctx);

/**
 * Get the type name of the annotated target (for properties/variables)
 * Returns: XXML String object
 */
void* Processor_getTypeName(ProcessorReflectionContext* ctx);

/**
 * Get the containing class name (for methods/properties)
 * Returns: XXML String object
 */
void* Processor_getClassName(ProcessorReflectionContext* ctx);

/**
 * Get the containing namespace
 * Returns: XXML String object
 */
void* Processor_getNamespaceName(ProcessorReflectionContext* ctx);

/**
 * Get the source file path
 * Returns: XXML String object
 */
void* Processor_getSourceFile(ProcessorReflectionContext* ctx);

/**
 * Get the line number
 */
int Processor_getLineNumber(ProcessorReflectionContext* ctx);

/**
 * Get the column number
 */
int Processor_getColumnNumber(ProcessorReflectionContext* ctx);

/* ==========================================================================
 * Class Inspection Methods (only valid when targetKind == "class")
 * ========================================================================== */

/**
 * Get the number of properties in the class
 */
int Processor_getPropertyCount(ProcessorReflectionContext* ctx);

/**
 * Get property name by index (0-based)
 */
const char* Processor_getPropertyNameAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get property type by index (0-based)
 */
const char* Processor_getPropertyTypeAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get property ownership by index ("^", "&", "%", or "")
 */
const char* Processor_getPropertyOwnershipAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get the number of methods in the class
 */
int Processor_getMethodCount(ProcessorReflectionContext* ctx);

/**
 * Get method name by index (0-based)
 */
const char* Processor_getMethodNameAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get method return type by index (0-based)
 */
const char* Processor_getMethodReturnTypeAt(ProcessorReflectionContext* ctx, int index);

/**
 * Check if class has a method with the given name
 */
int Processor_hasMethod(ProcessorReflectionContext* ctx, const char* name);

/**
 * Check if class has a property with the given name
 */
int Processor_hasProperty(ProcessorReflectionContext* ctx, const char* name);

/**
 * Get base class name (empty string if no base class)
 */
const char* Processor_getBaseClassName(ProcessorReflectionContext* ctx);

/**
 * Check if class is marked as Final
 */
int Processor_isClassFinal(ProcessorReflectionContext* ctx);

/* ==========================================================================
 * Method Inspection Methods (only valid when targetKind == "method")
 * ========================================================================== */

/**
 * Get the number of parameters in the method
 */
int Processor_getParameterCount(ProcessorReflectionContext* ctx);

/**
 * Get parameter name by index (0-based)
 */
const char* Processor_getParameterNameAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get parameter type by index (0-based)
 */
const char* Processor_getParameterTypeAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get the method's return type name
 */
const char* Processor_getReturnTypeName(ProcessorReflectionContext* ctx);

/**
 * Check if method is static
 */
int Processor_isMethodStatic(ProcessorReflectionContext* ctx);

/* ==========================================================================
 * Property Inspection Methods (only valid when targetKind == "property")
 * ========================================================================== */

/**
 * Check if property has a default value/initializer
 */
int Processor_hasDefaultValue(ProcessorReflectionContext* ctx);

/**
 * Get the ownership marker for the property ("^", "&", "%", or "")
 */
const char* Processor_getOwnership(ProcessorReflectionContext* ctx);

/* ==========================================================================
 * Target Value Access
 *
 * Get a pointer to the actual value being annotated.
 * For variables with constant initializers, this returns a pointer to the value.
 * Cast the returned pointer based on getTypeName():
 *   - "Integer" -> int64_t*
 *   - "String"  -> const char**
 *   - "Bool"    -> int* (0 or 1)
 *   - "Double"  -> double*
 * Returns NULL if no value is available (e.g., for class/method annotations).
 * ========================================================================== */

/**
 * Get a pointer to the target value (cast based on type)
 * Returns NULL if no value is available
 */
void* Processor_getTargetValue(ProcessorReflectionContext* ctx);

/**
 * Check if a target value is available
 */
int Processor_hasTargetValue(ProcessorReflectionContext* ctx);

/**
 * Get target value type: 0=none, 1=int, 2=string, 3=bool, 4=double
 */
int Processor_getTargetValueType(ProcessorReflectionContext* ctx);

/* ==========================================================================
 * Annotation Argument Access Methods
 * ========================================================================== */

/**
 * Get an argument by name, returns NULL if not found
 */
ProcessorAnnotationArg* Processor_getArg(ProcessorAnnotationArgs* args, const char* name);

/**
 * Get an argument by index, returns NULL if out of range
 */
ProcessorAnnotationArg* Processor_getArgAt(ProcessorAnnotationArgs* args, int index);

/**
 * Get argument count
 */
int Processor_getArgCount(ProcessorAnnotationArgs* args);

/**
 * Get argument name
 */
const char* Processor_argGetName(ProcessorAnnotationArg* arg);

/* ==========================================================================
 * Annotation Argument Access through ReflectionContext
 *
 * These methods allow accessing the arguments passed to the annotation
 * at the usage site, e.g., @MyAnnotation(name = "value", count = 42)
 * ========================================================================== */

/**
 * Get the number of arguments passed to the annotation
 */
int Processor_getAnnotationArgCount(ProcessorReflectionContext* ctx);

/**
 * Get annotation argument by name (type-aware)
 * Takes XXML String object as name parameter, returns XXML object for the value.
 * Returns NULL if not found
 */
void* Processor_getAnnotationArg(ProcessorReflectionContext* ctx, void* nameStr);

/**
 * Get annotation argument name by index (0-based)
 */
const char* Processor_getAnnotationArgNameAt(ProcessorReflectionContext* ctx, int index);

/**
 * Get annotation argument type by index (returns ProcessorArgType or -1 if invalid)
 */
int Processor_getAnnotationArgTypeAt(ProcessorReflectionContext* ctx, int index);

/**
 * Check if an annotation argument with the given name exists
 */
int Processor_hasAnnotationArg(ProcessorReflectionContext* ctx, const char* name);

/**
 * Get integer annotation argument by name, returns defaultValue if not found or wrong type
 */
int64_t Processor_getAnnotationIntArg(ProcessorReflectionContext* ctx, const char* name, int64_t defaultValue);

/**
 * Get string annotation argument by name, returns defaultValue if not found or wrong type
 */
const char* Processor_getAnnotationStringArg(ProcessorReflectionContext* ctx, const char* name, const char* defaultValue);

/**
 * Get boolean annotation argument by name, returns defaultValue if not found or wrong type
 */
int Processor_getAnnotationBoolArg(ProcessorReflectionContext* ctx, const char* name, int defaultValue);

/**
 * Get double annotation argument by name, returns defaultValue if not found or wrong type
 */
double Processor_getAnnotationDoubleArg(ProcessorReflectionContext* ctx, const char* name, double defaultValue);

/**
 * Get integer value from argument (returns 0 if wrong type)
 */
int64_t Processor_argAsInt(ProcessorAnnotationArg* arg);

/**
 * Get string value from argument (returns empty string if wrong type)
 */
const char* Processor_argAsString(ProcessorAnnotationArg* arg);

/**
 * Get boolean value from argument (returns 0 if wrong type)
 */
int Processor_argAsBool(ProcessorAnnotationArg* arg);

/**
 * Get double value from argument (returns 0.0 if wrong type)
 */
double Processor_argAsDouble(ProcessorAnnotationArg* arg);

/* ==========================================================================
 * Processor Entry Point
 *
 * User-defined processors must export this function with C linkage.
 * The compiler will call this function for each annotation usage.
 * ========================================================================== */

/**
 * Processor entry point - must be exported by processor DLLs
 *
 * @param reflection  Context with information about the annotated element
 * @param compilation Context for emitting messages/warnings/errors
 * @param args        The annotation arguments provided at the usage site
 */
#ifdef XXML_PROCESSOR_EXPORT
#ifdef _WIN32
#define XXML_PROCESSOR_API __declspec(dllexport)
#else
#define XXML_PROCESSOR_API __attribute__((visibility("default")))
#endif
#else
#ifdef _WIN32
#define XXML_PROCESSOR_API __declspec(dllimport)
#else
#define XXML_PROCESSOR_API
#endif
#endif

XXML_PROCESSOR_API void __xxml_processor_process(
    ProcessorReflectionContext* reflection,
    ProcessorCompilationContext* compilation,
    ProcessorAnnotationArgs* args
);

#ifdef __cplusplus
}
#endif

#endif /* XXML_PROCESSOR_API_H */
