/**
 * XXML Derive API
 *
 * This header defines the C ABI interface between the XXML compiler and
 * user-defined derives compiled to DLLs.
 *
 * Derives are compiled separately and loaded dynamically during main compilation.
 * Unlike annotation processors, derives can generate code (methods/properties)
 * that gets injected into target classes.
 */

#ifndef XXML_DERIVE_API_H
#define XXML_DERIVE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Derive Context Structure
 * ========================================================================== */

/**
 * DeriveContext - Provides information about the target class and
 * code generation capabilities.
 *
 * This struct is passed to derives to give them:
 * - Introspection of the target class (properties, methods, ownership)
 * - Type system queries (check if types have methods/traits)
 * - Code generation (add methods/properties to the class)
 * - Diagnostics (emit errors/warnings)
 */
typedef struct {
    /* Target class information */
    const char* className;       /* Name of the class being derived */
    const char* namespaceName;   /* Containing namespace */
    const char* sourceFile;      /* Source file path */
    int lineNumber;              /* Line number of @Derive annotation */
    int columnNumber;            /* Column number */

    /* Opaque pointers to internal compiler state */
    void* _classInfo;            /* Extended class introspection data */
    void* _typeSystem;           /* Type system query interface */
    void* _codeGen;              /* Code generation state */
    void* _diagnostics;          /* Error/warning reporter */
} DeriveContext;

/* ==========================================================================
 * Generated Method/Property Storage
 *
 * These structures are used internally to collect generated code
 * from the derive handler before parsing and injection.
 * ========================================================================== */

/**
 * GeneratedMethod - Represents a method to be added to the target class
 */
typedef struct {
    const char* name;            /* Method name */
    const char* returnType;      /* Return type (e.g., "String^") */
    const char* parameters;      /* Parameters string (e.g., "Parameter <other> Types Point&") */
    const char* body;            /* Method body as XXML code string OR JSON AST */
    int isStatic;                /* 1 if static method, 0 otherwise */
    int isAST;                   /* 1 if body is JSON AST, 0 if XXML source code */
} DeriveGeneratedMethod;

/**
 * GeneratedProperty - Represents a property to be added to the target class
 */
typedef struct {
    const char* name;            /* Property name */
    const char* type;            /* Property type (without ownership) */
    const char* ownership;       /* Ownership marker: "^", "&", "%", or "" */
    const char* defaultValue;    /* Default value expression, or NULL */
} DeriveGeneratedProperty;

/**
 * DeriveResult - Collection of generated methods and properties
 */
typedef struct {
    int methodCount;
    DeriveGeneratedMethod* methods;
    int propertyCount;
    DeriveGeneratedProperty* properties;
    int hasErrors;               /* Set to 1 if derive failed */
} DeriveResult;

/* ==========================================================================
 * Target Class Introspection Methods
 * ========================================================================== */

/**
 * Get the name of the target class
 * Returns: XXML String object
 */
void* Derive_getClassName(DeriveContext* ctx);

/**
 * Get the containing namespace
 * Returns: XXML String object
 */
void* Derive_getNamespaceName(DeriveContext* ctx);

/**
 * Get the source file path
 * Returns: XXML String object
 */
void* Derive_getSourceFile(DeriveContext* ctx);

/**
 * Get the line number of the @Derive annotation
 */
int Derive_getLineNumber(DeriveContext* ctx);

/**
 * Get the column number
 */
int Derive_getColumnNumber(DeriveContext* ctx);

/* ==========================================================================
 * Property Inspection Methods
 * ========================================================================== */

/**
 * Get the number of properties in the target class
 */
int Derive_getPropertyCount(DeriveContext* ctx);

/**
 * Get property name by index (0-based)
 * Returns: XXML String object
 */
void* Derive_getPropertyNameAt(DeriveContext* ctx, int index);

/**
 * Get property type by index (0-based), without ownership marker
 * Returns: XXML String object
 */
void* Derive_getPropertyTypeAt(DeriveContext* ctx, int index);

/**
 * Get property ownership by index ("^", "&", "%", or "")
 * Returns: XXML String object
 */
void* Derive_getPropertyOwnershipAt(DeriveContext* ctx, int index);

/**
 * Check if the target class has a property with the given name
 * Returns: XXML Bool object
 */
void* Derive_hasProperty(DeriveContext* ctx, void* nameStr);

/* ==========================================================================
 * Method Inspection Methods
 * ========================================================================== */

/**
 * Get the number of methods in the target class
 */
int Derive_getMethodCount(DeriveContext* ctx);

/**
 * Get method name by index (0-based)
 * Returns: XXML String object
 */
void* Derive_getMethodNameAt(DeriveContext* ctx, int index);

/**
 * Get method return type by index (0-based)
 * Returns: XXML String object
 */
void* Derive_getMethodReturnTypeAt(DeriveContext* ctx, int index);

/**
 * Check if the target class has a method with the given name
 * Returns: XXML Bool object
 */
void* Derive_hasMethod(DeriveContext* ctx, void* nameStr);

/**
 * Get base class name (empty string if no base class)
 * Returns: XXML String object
 */
void* Derive_getBaseClassName(DeriveContext* ctx);

/**
 * Check if target class is marked as Final
 * Returns: XXML Bool object
 */
void* Derive_isClassFinal(DeriveContext* ctx);

/* ==========================================================================
 * Type System Query Methods
 *
 * These methods allow derives to validate that types support required
 * operations before generating code.
 * ========================================================================== */

/**
 * Check if a type has a method with the given name
 * @param typeName XXML String object - the type to check
 * @param methodName XXML String object - the method to look for
 * Returns: XXML Bool object (true if method exists)
 */
void* Derive_typeHasMethod(DeriveContext* ctx, void* typeName, void* methodName);

/**
 * Check if a type has a property with the given name
 * @param typeName XXML String object - the type to check
 * @param propertyName XXML String object - the property to look for
 * Returns: XXML Bool object (true if property exists)
 */
void* Derive_typeHasProperty(DeriveContext* ctx, void* typeName, void* propertyName);

/**
 * Check if a type implements a trait (has @Derive for that trait)
 * @param typeName XXML String object - the type to check
 * @param traitName XXML String object - the trait to look for (e.g., "Stringable")
 * Returns: XXML Bool object (true if trait is implemented)
 */
void* Derive_typeImplementsTrait(DeriveContext* ctx, void* typeName, void* traitName);

/**
 * Check if a type is a built-in type (Integer, String, Bool, etc.)
 * @param typeName XXML String object - the type to check
 * Returns: XXML Bool object
 */
void* Derive_isBuiltinType(DeriveContext* ctx, void* typeName);

/* ==========================================================================
 * Code Generation Methods
 *
 * These methods allow derives to add new methods and properties to the
 * target class. The generated code is provided as XXML source strings
 * which are parsed and validated before injection.
 * ========================================================================== */

/**
 * Add a method to the target class
 *
 * @param name XXML String object - method name
 * @param returnType XXML String object - return type (e.g., "String^")
 * @param parameters XXML String object - parameter declarations
 *                   e.g., "Parameter <other> Types Point&"
 *                   or empty string for no parameters
 * @param body XXML String object - method body as XXML code
 *             This will be parsed and validated before injection
 *
 * The generated method will be added to the class's public section.
 */
void Derive_addMethod(DeriveContext* ctx, void* name, void* returnType,
                      void* parameters, void* body);

/**
 * Add a static method to the target class
 *
 * Same as Derive_addMethod but marks the method as static.
 */
void Derive_addStaticMethod(DeriveContext* ctx, void* name, void* returnType,
                            void* parameters, void* body);

/**
 * Add a method to the target class using AST JSON
 *
 * @param name XXML String object - method name
 * @param returnType XXML String object - return type (e.g., "String^")
 * @param parameters XXML String object - parameter declarations
 * @param bodyAST XXML String object - method body as JSON-serialized AST
 *                This is produced by Quote { } blocks in XXML derives
 *
 * The JSON AST will be deserialized and injected directly into the target class.
 */
void Derive_addMethodAST(DeriveContext* ctx, void* name, void* returnType,
                         void* parameters, void* bodyAST);

/**
 * Add a static method using AST JSON
 *
 * Same as Derive_addMethodAST but marks the method as static.
 */
void Derive_addStaticMethodAST(DeriveContext* ctx, void* name, void* returnType,
                               void* parameters, void* bodyAST);

/**
 * Add a property to the target class
 *
 * @param name XXML String object - property name
 * @param type XXML String object - property type (without ownership)
 * @param ownership XXML String object - ownership marker ("^", "&", "%", or "")
 * @param defaultValue XXML String object - default value expression, or empty string
 *
 * The generated property will be added to the class's public section.
 */
void Derive_addProperty(DeriveContext* ctx, void* name, void* type,
                        void* ownership, void* defaultValue);

/* ==========================================================================
 * Diagnostics Methods
 * ========================================================================== */

/**
 * Emit an error message and fail the derive
 * @param message XXML String object
 *
 * After calling this, the derive is considered failed and no code
 * will be injected into the target class.
 */
void Derive_error(DeriveContext* ctx, void* message);

/**
 * Emit a warning message
 * @param message XXML String object
 *
 * The derive will continue, but the warning will be reported.
 */
void Derive_warning(DeriveContext* ctx, void* message);

/**
 * Emit an informational message
 * @param message XXML String object
 */
void Derive_message(DeriveContext* ctx, void* message);

/**
 * Check if any errors have been emitted
 * Returns: XXML Bool object
 */
void* Derive_hasErrors(DeriveContext* ctx);

/* ==========================================================================
 * AST/Quote Splice Substitution Methods
 *
 * These methods help derives substitute splice placeholders in AST JSON
 * generated by Quote { } blocks.
 * ========================================================================== */

/**
 * Substitute a splice placeholder in AST JSON
 *
 * @param json XXML String object - the JSON containing splice markers
 * @param varName XXML String object - the splice variable name to substitute
 * @param value XXML String object - the value to substitute
 * Returns: XXML String object - new JSON with substitution applied
 *
 * This replaces {"__SPLICE__":"varName"} with "value" in the JSON.
 * For member name splices, it replaces the member name in SplicedMemberAccess nodes.
 */
void* Derive_substituteSplice(void* json, void* varName, void* value);

/**
 * Substitute all splice placeholders for a single variable in AST JSON
 *
 * This is like substituteSplice but handles all occurrences including:
 * - Identifier splices: {"__SPLICE__":"var"} -> "value"
 * - Member splices in SplicedMemberAccess
 *
 * @param json XXML String object - the JSON containing splice markers
 * @param varName XXML String object - the splice variable name
 * @param value XXML String object - the value to substitute
 * Returns: XXML String object - new JSON with all substitutions applied
 */
void* Derive_substituteSpliceAll(void* json, void* varName, void* value);

/* ==========================================================================
 * Derive Entry Points
 *
 * User-defined derives must export these functions with C linkage.
 * The compiler will call these functions when processing @Derive annotations.
 * ========================================================================== */

#ifdef XXML_DERIVE_EXPORT
#ifdef _WIN32
#define XXML_DERIVE_API __declspec(dllexport)
#else
#define XXML_DERIVE_API __attribute__((visibility("default")))
#endif
#else
#ifdef _WIN32
#define XXML_DERIVE_API __declspec(dllimport)
#else
#define XXML_DERIVE_API
#endif
#endif

/**
 * Get the name of the trait this derive implements
 * Returns: C string (const char*) - e.g., "Stringable", "Equatable"
 */
XXML_DERIVE_API const char* __xxml_derive_name(void);

/**
 * Validate whether the derive can be applied to the target class
 *
 * @param ctx Derive context with target class information
 * Returns: XXML String object - empty string if valid, error message if not
 *
 * This is called before generate() to allow early validation.
 * If this returns a non-empty string, generate() will not be called.
 */
XXML_DERIVE_API void* __xxml_derive_canDerive(DeriveContext* ctx);

/**
 * Generate methods and properties for the target class
 *
 * @param ctx Derive context with introspection and code generation APIs
 *
 * Use the Derive_addMethod() and Derive_addProperty() functions to
 * add generated code to the target class.
 */
XXML_DERIVE_API void __xxml_derive_generate(DeriveContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* XXML_DERIVE_API_H */
