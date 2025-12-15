# XXML Annotation System

XXML supports a Java-style annotation system for adding metadata to code elements. Annotations can be used for compile-time checks, documentation, and runtime reflection.

---

## Table of Contents

1. [Overview](#overview)
2. [Defining Annotations](#defining-annotations)
3. [Annotation Targets](#annotation-targets)
4. [Retention Modes](#retention-modes)
5. [Using Annotations](#using-annotations)
6. [Annotation Parameters](#annotation-parameters)
7. [Built-in Annotations](#built-in-annotations)
8. [Annotation Processors](#annotation-processors)
   - [User-Defined Processors (DLL-Based)](#user-defined-processors-dll-based)
   - [Inline Annotation Processors](#inline-annotation-processors)
   - [Type-Aware Intrinsic Methods](#type-aware-intrinsic-methods)
   - [ReflectionContext API](#reflectioncontext-api)
   - [CompilationContext API](#compilationcontext-api)
9. [Quick Reference: Processor APIs](#quick-reference-processor-apis)
10. [Runtime Reflection API](#runtime-reflection-api)
11. [Examples](#examples)
12. [Grammar Reference](#grammar-reference)
13. [Implementation Details](#implementation-details)

---

## Overview

Annotations provide a way to attach metadata to classes, methods, properties, and other code elements. This metadata can be:

- **Processed at compile-time** for validation, warnings, or code generation
- **Retained at runtime** for reflection-based frameworks (serialization, validation, dependency injection)

### Basic Syntax

```xxml
// Define an annotation
[ Annotation <MyAnnotation> Allows (AnnotationAllow::Classes) ]

// Use an annotation
@MyAnnotation
[ Class <MyClass> Final Extends None ... ]
```

---

## Defining Annotations

Annotations are defined using the `Annotation` keyword within brackets:

```xxml
[ Annotation <AnnotationName> Allows (targets) ]
```

### Simple Annotation

```xxml
// Marker annotation with no parameters
[ Annotation <Immutable> Allows (AnnotationAllow::Classes) ]
```

### Annotation with Runtime Retention

```xxml
// Annotation preserved at runtime for reflection
[ Annotation <Serializable> Allows (AnnotationAllow::Classes) Retain ]
```

### Annotation with Parameters

```xxml
// Annotation with typed parameters
[ Annotation <Range> Allows (AnnotationAllow::Properties) Retain
    Annotate (Integer^)(min);
    Annotate (Integer^)(max);
]
```

### Annotation with Multiple Targets

```xxml
// Can be applied to both classes and properties
[ Annotation <Validated> Allows (AnnotationAllow::Classes, AnnotationAllow::Properties) Retain ]
```

---

## Annotation Targets

The `Allows` clause specifies where an annotation can be applied using `AnnotationAllow` values:

| Target | Description |
|--------|-------------|
| `AnnotationAllow::Classes` | Can be applied to class declarations |
| `AnnotationAllow::Methods` | Can be applied to method declarations |
| `AnnotationAllow::Properties` | Can be applied to property declarations |
| `AnnotationAllow::Parameters` | Can be applied to method parameters |
| `AnnotationAllow::Constructors` | Can be applied to constructor declarations |
| `AnnotationAllow::All` | Can be applied to any target |

### Example: Target-Specific Annotations

```xxml
// Only for classes
[ Annotation <Entity> Allows (AnnotationAllow::Classes) Retain ]

// Only for methods
[ Annotation <Transactional> Allows (AnnotationAllow::Methods) Retain ]

// Only for properties
[ Annotation <Column> Allows (AnnotationAllow::Properties) Retain
    Annotate (String^)(name);
    Annotate (Bool^)(nullable);
]

// For multiple targets
[ Annotation <Documented> Allows (AnnotationAllow::Classes, AnnotationAllow::Methods) ]
```

---

## Retention Modes

### Compile-Time Only (Default)

Without the `Retain` keyword, annotations are processed during compilation and discarded. They do not appear in the generated executable.

```xxml
// Compile-time only - used for static checks
[ Annotation <Deprecated> Allows (AnnotationAllow::All) ]
```

Use cases:
- Compiler warnings (`@Deprecated`)
- Static analysis
- Documentation generation

### Runtime Retention

With the `Retain` keyword, annotation metadata is embedded in the executable and accessible via reflection.

```xxml
// Runtime retained - accessible via reflection API
[ Annotation <Serializable> Allows (AnnotationAllow::Classes) Retain ]
```

Use cases:
- Serialization frameworks
- Dependency injection
- Validation frameworks
- ORM mapping

---

## Using Annotations

Apply annotations using the `@` symbol before the target element.

### Class Annotations

```xxml
@Entity
@Serializable(format = "json")
[ Class <User> Final Extends None
    [ Public <>
        Property <id> Types Integer^;
        Property <name> Types String^;
    ]
]
```

### Property Annotations

```xxml
[ Class <Product> Final Extends None
    [ Public <>
        @Column(name = "product_id", nullable = false)
        Property <id> Types Integer^;

        @Column(name = "product_name", nullable = false)
        @MinLength(value = 1)
        Property <name> Types String^;

        @Range(min = 0, max = 999999)
        Property <price> Types Integer^;
    ]
]
```

### Method Annotations

```xxml
[ Class <OrderService> Final Extends None
    [ Public <>
        @Transactional
        @Logged
        Method <processOrder> Returns None Parameters (Integer^ <orderId>) ->
        {
            // Implementation
        }
    ]
]
```

### Multiple Annotations

Multiple annotations can be stacked on the same element:

```xxml
@Deprecated
@Serializable
@Validated
[ Class <LegacyData> Final Extends None ... ]
```

---

## Annotation Parameters

Annotations can have typed parameters that are passed as named arguments.

### Supported Parameter Types

- `Integer^` - Integer values
- `String^` - String literals
- `Bool^` - Boolean values (`true` or `false`)
- `Double^` - Floating-point values

### Defining Parameters

```xxml
[ Annotation <HttpRoute> Allows (AnnotationAllow::Methods) Retain
    Annotate (String^)(path);
    Annotate (String^)(method);
    Annotate (Bool^)(authenticated);
]
```

### Using Parameters

```xxml
@HttpRoute(path = "/api/users", method = "GET", authenticated = true)
Method <getUsers> Returns String^ Parameters () ->
{
    // Implementation
}
```

### Default Values

Annotation parameters can have default values, making them optional:

```xxml
[ Annotation <Config> Allows (AnnotationAllow::Classes)
    Annotate (String^)(name);                    // Required - no default
    Annotate (Integer^)(version) = 1;            // Optional - default is 1
    Annotate (Bool^)(debug) = false;             // Optional - default is false
]

// Only required parameters need to be specified
@Config(name = "MyApp")  // version=1, debug=false by default
[ Class <App> ... ]

// Or override defaults
@Config(name = "MyApp", version = 2, debug = true)
[ Class <App> ... ]
```

---

## Built-in Annotations

XXML provides built-in annotations with compile-time processors:

### @Deprecated

Marks an element as deprecated. The compiler generates a warning during compilation.

```xxml
@Deprecated
[ Class <OldApi> Final Extends None ... ]

// Compiler output: warning: class 'OldApi' is deprecated: This element is deprecated

// With custom reason:
@Deprecated(reason = "Use NewApi instead")
Method <oldMethod> Returns None Parameters () -> { ... }
```

### @Override (Planned)

Indicates that a method overrides a parent class method. The compiler verifies the override is valid.

```xxml
[ Class <Child> Final Extends Parent
    [ Public <>
        @Override
        Method <doSomething> Returns None Parameters () ->
        {
            // Override implementation
        }
    ]
]
```

### @Suppress (Planned)

Suppresses specific compiler warnings.

```xxml
@Suppress(warning = "unused")
Property <reservedField> Types Integer^;
```

---

## Annotation Processors

Annotation processors run during compilation to perform validation, generate warnings, or trigger errors.

### Built-in Processor Architecture

The compiler includes an annotation processor framework with:

- **ReflectionContext** - Provides information about the annotated element (name, type, location)
- **CompilationContext** - Allows processors to emit messages, warnings, and errors

```cpp
// Built-in processor signature (C++)
void processAnnotation(
    const PendingAnnotation& annotation,
    ReflectionContext& reflection,
    CompilationContext& compilation
);
```

### Current Implementation

Built-in processors:

| Annotation | Processor Action |
|------------|------------------|
| `@Deprecated` | Emits deprecation warning with target info |

### User-Defined Processors (DLL-Based)

User-defined annotation processors can be compiled to DLLs and loaded at compile time.

#### CLI Usage

```bash
# Compile a processor to a DLL
xxml --processor MyProcessor.XXML -o MyProcessor.dll

# Use a processor DLL during compilation (explicit)
xxml --use-processor=MyProcessor.dll App.XXML -o app.exe

# Use multiple processor DLLs
xxml --use-processor=Validate.dll --use-processor=Log.dll App.XXML -o app.exe
```

#### Auto-Discovery

The compiler automatically discovers and loads processor DLLs from these locations:

1. `<source_file_directory>/processors/` - Next to your source file
2. `<compiler_directory>/processors/` - Next to the xxml executable
3. `./processors/` - Current working directory

Simply place your processor DLLs in any of these directories, and they will be loaded automatically:

```
MyProject/
├── App.XXML              # Your source file
├── processors/           # Auto-discovered processors
│   ├── Validate.dll
│   └── MyAnnotation.dll
└── output/
    └── app.exe
```

No `--use-processor` flag needed when using auto-discovery.

#### Processor C API

User-defined processors must export a C-compatible entry point:

```c
// xxml_processor_api.h - Included in processor DLLs

// Reflection context - information about the annotated element
typedef struct {
    const char* targetKind;      // "class", "method", "property", "variable"
    const char* targetName;      // Name of the annotated element
    const char* typeName;        // Type of the element
    const char* className;       // Containing class (for methods/properties)
    const char* namespaceName;   // Containing namespace
    const char* sourceFile;      // Source file path
    int lineNumber;              // Line number in source
    int columnNumber;            // Column number in source
    void* _internal;             // Opaque pointer for deep inspection (class/method/property info)
} ProcessorReflectionContext;

// Compilation context - for emitting warnings/errors
typedef struct {
    void* _internal;             // Opaque pointer to compiler state
} ProcessorCompilationContext;

// Annotation argument
typedef struct {
    const char* name;
    ProcessorArgType type;       // PROCESSOR_ARG_INT, STRING, BOOL, DOUBLE
    union {
        int64_t intValue;
        const char* stringValue;
        int boolValue;
        double doubleValue;
    } value;
} ProcessorAnnotationArg;

// Collection of annotation arguments
typedef struct {
    int count;
    ProcessorAnnotationArg* args;
} ProcessorAnnotationArgs;

// Processor entry point - must be exported from DLL
void __xxml_processor_process(
    ProcessorReflectionContext* reflection,
    ProcessorCompilationContext* compilation,
    ProcessorAnnotationArgs* args
);

// API functions callable by processors
void Processor_message(ProcessorCompilationContext* ctx, const char* msg);
void Processor_warning(ProcessorCompilationContext* ctx, const char* msg);
void Processor_error(ProcessorCompilationContext* ctx, const char* msg);

// Class inspection (targetKind == "class")
int Processor_getPropertyCount(ProcessorReflectionContext* ctx);
const char* Processor_getPropertyNameAt(ProcessorReflectionContext* ctx, int index);
const char* Processor_getPropertyTypeAt(ProcessorReflectionContext* ctx, int index);
const char* Processor_getPropertyOwnershipAt(ProcessorReflectionContext* ctx, int index);
int Processor_getMethodCount(ProcessorReflectionContext* ctx);
const char* Processor_getMethodNameAt(ProcessorReflectionContext* ctx, int index);
const char* Processor_getMethodReturnTypeAt(ProcessorReflectionContext* ctx, int index);
int Processor_hasMethod(ProcessorReflectionContext* ctx, const char* name);
int Processor_hasProperty(ProcessorReflectionContext* ctx, const char* name);
const char* Processor_getBaseClassName(ProcessorReflectionContext* ctx);
int Processor_isClassFinal(ProcessorReflectionContext* ctx);

// Method inspection (targetKind == "method")
int Processor_getParameterCount(ProcessorReflectionContext* ctx);
const char* Processor_getParameterNameAt(ProcessorReflectionContext* ctx, int index);
const char* Processor_getParameterTypeAt(ProcessorReflectionContext* ctx, int index);
const char* Processor_getReturnTypeName(ProcessorReflectionContext* ctx);
int Processor_isMethodStatic(ProcessorReflectionContext* ctx);

// Property inspection (targetKind == "property")
const char* Processor_getOwnership(ProcessorReflectionContext* ctx);
int Processor_hasDefaultValue(ProcessorReflectionContext* ctx);

// Target value access
void* Processor_getTargetValue(ProcessorReflectionContext* ctx);
int Processor_hasTargetValue(ProcessorReflectionContext* ctx);
int Processor_getTargetValueType(ProcessorReflectionContext* ctx);  // 0=none, 1=int, 2=string, 3=bool, 4=double

// Annotation argument access (via ReflectionContext)
int Processor_getAnnotationArgCount(ProcessorReflectionContext* ctx);
void* Processor_getAnnotationArg(ProcessorReflectionContext* ctx, const char* name);  // Type-aware generic access
const char* Processor_getAnnotationArgNameAt(ProcessorReflectionContext* ctx, int index);
```

#### Example Processor DLL (C)

```c
#include "xxml_processor_api.h"

// Optional: Export the annotation name
XXML_PROCESSOR_API const char* __xxml_processor_annotation_name() {
    return "NotNull";
}

// Required: Process function
XXML_PROCESSOR_API void __xxml_processor_process(
    ProcessorReflectionContext* reflection,
    ProcessorCompilationContext* compilation,
    ProcessorAnnotationArgs* args
) {
    // Example: Emit warning for NotNull annotation
    char message[256];
    snprintf(message, sizeof(message),
        "@NotNull applied to %s '%s'",
        reflection->targetKind,
        reflection->targetName);

    Processor_message(compilation, message);
}
```

#### Inline Annotation Processors

XXML supports **inline annotation processors** where the processor code is defined directly inside the annotation definition using a nested `[ Processor ]` block. The processor acts like a nested class within the annotation.

##### Syntax

```xxml
[ Annotation <MyAnnotation> Allows (AnnotationAllow::Classes, AnnotationAllow::Methods)
    Annotate (String^)(message);

    [ Processor
        // This acts like a class nested within the annotation
        // Accessed as MyAnnotation::Processor

        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <reflectionContext> Types ReflectionContext&,
                Parameter <compilationContext> Types CompilationContext&
            ) -> {
                // Runs for every time the annotation is used
                // Can issue a message, warning, error, or stop compilation
            }
        ]
    ]
]
```

##### Complete Example

```xxml
// Define annotation with inline processor
[ Annotation <Review> Allows (AnnotationAllow::Classes, AnnotationAllow::Methods)
    Annotate (String^)(message);

    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <reflectionContext> Types ReflectionContext&,
                Parameter <compilationContext> Types CompilationContext&
            ) -> {
                // Emit a warning for every @Review usage
                Run compilationContext.warning(String::Constructor("Code is under review"));
            }
        ]
    ]
]

// Using the annotation - processor runs automatically
@Review(message = "Needs optimization")
[ Class <MyClass> Final Extends None
    [ Public <>
        Constructor = default;

        @Review(message = "Consider refactoring")
        Method <oldMethod> Returns None Parameters () -> { }
    ]
]
```

##### Key Concepts

**1. Nested Processor Class**

The `[ Processor ]` block acts like a class nested within the annotation definition. It can be conceptually accessed as `AnnotationName::Processor`. This follows the pattern of nested classes in other languages.

**2. The `onAnnotate` Method**

The processor must define a method named `onAnnotate` with this exact signature:

```xxml
Method <onAnnotate> Returns None Parameters (
    Parameter <reflectionContext> Types ReflectionContext&,
    Parameter <compilationContext> Types CompilationContext&
) -> {
    // Processor logic
}
```

- **Returns `None`** - The method cannot return a value
- **First parameter**: `ReflectionContext&` - Information about the annotated element
- **Second parameter**: `CompilationContext&` - Interface to the compiler for messages/warnings/errors

**3. Compiler-Intrinsic Types**

`ReflectionContext` and `CompilationContext` are **compiler-intrinsic types**, similar to `NativeType<T>`. They:

- Are only available within the `onAnnotate` method of a processor
- Have no XXML definition - they are backed directly by the compiler
- Cannot be instantiated or used outside of processor methods
- Provide a bridge between XXML code and the compiler's internal state
- Support **type-aware method returns** - the compiler determines return types contextually

**4. Type-Aware Intrinsic Methods**

Because `ReflectionContext` is compiler-intrinsic, certain methods have **type-aware return values** that the compiler determines based on context:

| Method | Return Type Determination |
|--------|---------------------------|
| `getTargetValue()` | Returns `<T>&` where `<T>` is the type of the annotated element |
| `getAnnotationArg(name)` | Returns `<T>^` where `<T>` is the declared type of the annotation parameter |

This means the same method returns different types depending on usage:

```xxml
// Annotating an Integer^ variable
@MyAnnotation
Instantiate Integer^ As <count> = Integer::Constructor(42);
// → getTargetValue() returns Integer&

// Annotating a String^ variable
@MyAnnotation
Instantiate String^ As <name> = String::Constructor("test");
// → getTargetValue() returns String&

// Annotation with Integer^ parameter 'min' and String^ parameter 'name'
Instantiate Integer^ As <minVal> = ctx.getAnnotationArg(String::Constructor("min"));   // Returns Integer^
Instantiate String^ As <nameVal> = ctx.getAnnotationArg(String::Constructor("name"));  // Returns String^
```

The compiler validates type correctness at compile time - attempting to assign to the wrong type produces a type mismatch error.

**5. Automatic Execution**

When an annotation with an inline processor is used:

1. The compiler detects the processor during semantic analysis
2. The processor is auto-compiled to a temporary DLL
3. The DLL is loaded into the processor registry
4. `onAnnotate` is invoked for **each usage** of the annotation

##### Implementation Status

| Feature | Status |
|---------|--------|
| Parser support for `[ Processor ]` | ✅ Implemented |
| `onAnnotate` signature validation | ✅ Implemented |
| `ReflectionContext` intrinsic type | ✅ Implemented |
| `CompilationContext` intrinsic type | ✅ Implemented |
| Inline processor detection | ✅ Implemented |
| Auto-compilation to temp DLL | ✅ Implemented |
| Processor DLL loading | ✅ Implemented |
| Processor invocation | ✅ Implemented |

##### How Auto-Compilation Works

```
1. Semantic Analysis
   └─> Detect [ Processor ] blocks in annotations
   └─> Collect imports and user-defined classes from source file

2. Source Generation
   └─> Serialize processor AST back to standalone XXML source
   └─> Include all imports from original source
   └─> Include user-defined class definitions

3. Subprocess Compilation
   └─> xxml --processor <generated.XXML> -o <temp.dll>

4. Registry Loading
   └─> Load compiled DLL into processor registry

5. Invocation
   └─> Call onAnnotate for each @Annotation usage
```

##### Processor Code Access

Inline processors have full access to:

1. **All imported modules** - Every `#import` from the source file is included in the processor
2. **User-defined classes** - Classes defined in the same file are serialized and included

This allows processors to:
- Reference custom types defined alongside the annotation
- Use library classes from imports
- Create instances of user-defined classes within the processor

**Example: Processor using a user-defined class**

```xxml
// Define a helper class
[ Class <ValidationHelper> Final Extends None
    [ Public <>
        Constructor = default;

        Method <isValidRange> Returns Bool^ Parameters (
            Parameter <min> Types Integer^,
            Parameter <max> Types Integer^
        ) -> {
            Return min.lessThan(max);
        }
    ]
]

// Annotation with processor that uses the helper class
[ Annotation <ValidRange> Allows (AnnotationAllow::Properties)
    Annotate (Integer^)(min);
    Annotate (Integer^)(max);

    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <ctx> Types ReflectionContext&,
                Parameter <comp> Types CompilationContext&
            ) -> {
                Instantiate Integer^ As <min> = ctx.getAnnotationArg(String::Constructor("min"));
                Instantiate Integer^ As <max> = ctx.getAnnotationArg(String::Constructor("max"));

                // Use the user-defined helper class
                Instantiate ValidationHelper^ As <helper> = ValidationHelper::Constructor();
                If (helper.isValidRange(min, max) == false) -> {
                    Run comp.error(String::Constructor("Invalid range: min must be less than max"));
                }
            }
        ]
    ]
]
```

##### ReflectionContext API

`ReflectionContext` is a **compiler-intrinsic type** that provides information about the element the annotation is applied to. It is only available as the first parameter of the `onAnnotate` method in a processor.

**Target Information Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `getTargetKind()` | `String^` | The kind of element: `"class"`, `"method"`, `"property"`, or `"variable"` |
| `getTargetName()` | `String^` | Name of the annotated element (e.g., `"MyClass"`, `"doSomething"`) |
| `getTypeName()` | `String^` | Type name for properties/variables (e.g., `"Integer"`, `"String"`). Empty for classes/methods. |
| `getClassName()` | `String^` | Containing class name for methods/properties. Empty for top-level classes. |
| `getNamespaceName()` | `String^` | Containing namespace (e.g., `"MyApp::Services"`). Empty if no namespace. |

**Source Location Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `getSourceFile()` | `String^` | Absolute path to the source file containing the annotated element |
| `getLineNumber()` | `Integer^` | Line number where the annotation is applied (1-based) |
| `getColumnNumber()` | `Integer^` | Column number where the annotation is applied (1-based) |

**Class Inspection Methods** (only valid when `targetKind == "class"`):

| Method | Returns | Description |
|--------|---------|-------------|
| `getPropertyCount()` | `Integer^` | Number of properties in the annotated class |
| `getPropertyNameAt(Integer^ index)` | `String^` | Name of property at index (0-based) |
| `getPropertyTypeAt(Integer^ index)` | `String^` | Type name of property at index (e.g., `"Integer"`, `"String"`) |
| `getPropertyOwnershipAt(Integer^ index)` | `String^` | Ownership marker: `"^"` (owned), `"&"` (reference), `"%"` (copy), or `""` |
| `getMethodCount()` | `Integer^` | Number of methods in the annotated class |
| `getMethodNameAt(Integer^ index)` | `String^` | Name of method at index (0-based) |
| `getMethodReturnTypeAt(Integer^ index)` | `String^` | Return type of method at index (e.g., `"None"`, `"Integer"`) |
| `hasMethod(String^ name)` | `Bool^` | Check if the class has a method with the given name |
| `hasProperty(String^ name)` | `Bool^` | Check if the class has a property with the given name |
| `getBaseClassName()` | `String^` | Name of the base class (empty string if no base class) |
| `isClassFinal()` | `Bool^` | Check if the class is marked as `Final` |

**Method Inspection Methods** (only valid when `targetKind == "method"`):

| Method | Returns | Description |
|--------|---------|-------------|
| `getParameterCount()` | `Integer^` | Number of parameters in the annotated method |
| `getParameterNameAt(Integer^ index)` | `String^` | Name of parameter at index (0-based) |
| `getParameterTypeAt(Integer^ index)` | `String^` | Type name of parameter at index |
| `getReturnTypeName()` | `String^` | Return type of the annotated method |
| `isMethodStatic()` | `Bool^` | Check if the method is static |

**Property Inspection Methods** (only valid when `targetKind == "property"`):

| Method | Returns | Description |
|--------|---------|-------------|
| `getOwnership()` | `String^` | Ownership marker: `"^"`, `"&"`, `"%"`, or `""` |
| `hasDefaultValue()` | `Bool^` | Check if property has a default value/initializer |

**Target Value Access**:

| Method | Returns | Description |
|--------|---------|-------------|
| `hasTargetValue()` | `Bool^` | Check if a target value is available |
| `getTargetValueType()` | `Integer^` | Value type: 0=none, 1=int, 2=string, 3=bool, 4=double |
| `getTargetValue()` | `<T>&` | Reference to the annotated element's value |

**Annotation Argument Access**:

These methods provide access to the arguments passed to the annotation at the usage site (e.g., `@Range(min = 0, max = 100)`):

| Method | Returns | Description |
|--------|---------|-------------|
| `getAnnotationArg(String^ name)` | `<T>^` | **Type-aware**: Returns the declared type of the parameter |
| `getAnnotationArgCount()` | `Integer^` | Number of arguments passed to the annotation |
| `getAnnotationArgNameAt(Integer^ index)` | `String^` | Name of argument at index (0-based) |

The `getAnnotationArg(name)` method is **type-aware** - similar to `getTargetValue()`, since `ReflectionContext` is a compiler-intrinsic type, the compiler determines the return type based on the annotation's parameter declaration:

```xxml
[ Annotation <Config> Allows (AnnotationAllow::Classes)
    Annotate (Integer^)(version);
    Annotate (String^)(name);
]

// In the processor:
Method <onAnnotate> Returns None Parameters (
    Parameter <ctx> Types ReflectionContext&,
    Parameter <comp> Types CompilationContext&
) -> {
    // getAnnotationArg("version") returns Integer^ because 'version' is declared as Integer^
    Instantiate Integer^ As <ver> = ctx.getAnnotationArg(String::Constructor("version"));

    // getAnnotationArg("name") returns String^ because 'name' is declared as String^
    Instantiate String^ As <nm> = ctx.getAnnotationArg(String::Constructor("name"));
}
```

The compiler validates that the parameter name is known at compile time and reports an error if the parameter doesn't exist in the annotation definition.

The `getTargetValue()` method is **type-aware** - it returns a reference of the same type as the annotated element. Since `ReflectionContext` is a compiler-intrinsic type, the compiler determines the return type based on context:

```xxml
// If annotating an Integer^, getTargetValue() returns Integer&
@MyAnnotation
Instantiate Integer^ As <count> = Integer::Constructor(42);

// In the processor:
Method <onAnnotate> Returns None Parameters (
    Parameter <ctx> Types ReflectionContext&,
    Parameter <comp> Types CompilationContext&
) -> {
    // getTargetValue() returns Integer& because we're annotating an Integer^
    Instantiate Integer& As <value> = ctx.getTargetValue();

    // Now we can use the value directly with its proper type
    If (value.lessThan(Integer::Constructor(0))) -> {
        Run comp.error(String::Constructor("Value must be non-negative"));
    }
}
```

For variables with constant initializers, the value is evaluated at compile time. For other targets, availability depends on context.

**Example: Accessing Annotation Arguments:**

```xxml
[ Annotation <Range> Allows (AnnotationAllow::Properties, AnnotationAllow::Variables)
    Annotate (Integer^)(min);
    Annotate (Integer^)(max);

    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <ctx> Types ReflectionContext&,
                Parameter <comp> Types CompilationContext&
            ) -> {
                // Type-aware access - compiler knows min and max are Integer^
                Instantiate Integer^ As <minVal> = ctx.getAnnotationArg(String::Constructor("min"));
                Instantiate Integer^ As <maxVal> = ctx.getAnnotationArg(String::Constructor("max"));

                // Validate the range is sensible
                If (minVal.greaterThan(maxVal)) -> {
                    Run comp.error(String::Constructor("@Range: min cannot be greater than max"));
                }

                // If annotating a variable with a constant value, validate it
                If (ctx.hasTargetValue()) -> {
                    Instantiate Integer& As <value> = ctx.getTargetValue();
                    If (value.lessThan(minVal)) -> {
                        Run comp.error(String::Constructor("Value is below minimum"));
                    }
                    If (value.greaterThan(maxVal)) -> {
                        Run comp.error(String::Constructor("Value exceeds maximum"));
                    }
                }
            }
        ]
    ]
]

// Usage - processor validates at compile time
@Range(min = 0, max = 100)
Instantiate Integer^ As <percentage> = Integer::Constructor(50);  // OK

@Range(min = 1, max = 10)
Instantiate Integer^ As <rating> = Integer::Constructor(11);  // Error: Value exceeds maximum
```

**Example Usage in Processor:**

```xxml
Method <onAnnotate> Returns None Parameters (
    Parameter <ctx> Types ReflectionContext&,
    Parameter <comp> Types CompilationContext&
) -> {
    // Get information about the annotated element
    Instantiate String^ As <kind> = ctx.getTargetKind();
    Instantiate String^ As <name> = ctx.getTargetName();
    Instantiate String^ As <file> = ctx.getSourceFile();
    Instantiate Integer^ As <line> = ctx.getLineNumber();

    // Log what we're processing
    Run comp.message(String::Constructor("Processing @MyAnnotation on "));
    Run comp.message(kind);
    Run comp.message(String::Constructor(" '"));
    Run comp.message(name);
    Run comp.message(String::Constructor("' at line "));
    Run comp.message(line.toString());
}
```

**Target Kind Values:**

| Kind | When Returned | Example |
|------|---------------|---------|
| `"class"` | Annotation on a class declaration | `@Entity [ Class <User> ... ]` |
| `"method"` | Annotation on a method | `@Transactional Method <save> ...` |
| `"property"` | Annotation on a property | `@Column Property <id> ...` |
| `"variable"` | Annotation on a local variable | `@NotNull Instantiate String^ As <name>` |

**Example: Class Structure Inspection**

```xxml
[ Annotation <DataClass> Allows (AnnotationAllow::Classes)
    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <ctx> Types ReflectionContext&,
                Parameter <comp> Types CompilationContext&
            ) -> {
                // Iterate over all properties
                Instantiate Integer^ As <propCount> = ctx.getPropertyCount();
                Instantiate Integer^ As <i> = Integer::Constructor(0);

                While (i.lessThan(propCount)) -> {
                    Instantiate String^ As <propName> = ctx.getPropertyNameAt(i);
                    Instantiate String^ As <propType> = ctx.getPropertyTypeAt(i);
                    Instantiate String^ As <ownership> = ctx.getPropertyOwnershipAt(i);

                    // Check for required ownership
                    If (ownership.equals(String::Constructor(""))) -> {
                        Run comp.warning(String::Constructor("Property '"));
                        Run comp.warning(propName);
                        Run comp.warning(String::Constructor("' should have explicit ownership"));
                    }

                    Set i = i.add(Integer::Constructor(1));
                }

                // Check for required methods
                If (ctx.hasMethod(String::Constructor("equals")) == false) -> {
                    Run comp.error(String::Constructor("@DataClass requires an 'equals' method"));
                }
            }
        ]
    ]
]
```

---

##### CompilationContext API

`CompilationContext` is a **compiler-intrinsic type** that provides an interface for processors to communicate with the compiler. It allows emitting messages, warnings, and errors during compilation.

**Message Output Methods:**

| Method | Parameters | Description |
|--------|------------|-------------|
| `message(String^)` | Message text | Emit an informational message. Displayed as `[Annotation Processor] <message>` during compilation. Does not affect compilation success. |
| `warning(String^)` | Warning text | Emit a compiler warning at the annotation's location. Compilation continues but warning count is incremented. |
| `warningAt(String^, String^, Integer^, Integer^)` | Message, file, line, column | Emit a warning at a specific source location (not just the annotation site). |
| `error(String^)` | Error text | Emit a compiler error at the annotation's location. **Stops compilation** after all processors run. |
| `errorAt(String^, String^, Integer^, Integer^)` | Message, file, line, column | Emit an error at a specific source location. |

**Compilation State Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `hasErrors()` | `Bool^` | Returns `true` if any errors have been reported (by this or other processors) |

**Example: Validation Processor**

```xxml
[ Annotation <NotEmpty> Allows (AnnotationAllow::Properties)
    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <ctx> Types ReflectionContext&,
                Parameter <comp> Types CompilationContext&
            ) -> {
                // Validate that @NotEmpty is only applied to String properties
                Instantiate String^ As <typeName> = ctx.getTypeName();

                If (typeName.equals(String::Constructor("String")) == false) -> {
                    Run comp.error(String::Constructor(
                        "@NotEmpty can only be applied to String properties"
                    ));
                }
            }
        ]
    ]
]
```

**Example: Deprecation Processor with Custom Message**

```xxml
[ Annotation <Deprecated> Allows (AnnotationAllow::Classes, AnnotationAllow::Methods)
    Annotate (String^)(reason) = String::Constructor("No reason provided");

    [ Processor
        [ Public <>
            Method <onAnnotate> Returns None Parameters (
                Parameter <ctx> Types ReflectionContext&,
                Parameter <comp> Types CompilationContext&
            ) -> {
                Instantiate String^ As <kind> = ctx.getTargetKind();
                Instantiate String^ As <name> = ctx.getTargetName();

                // Build warning message
                Instantiate String^ As <msg> = String::Constructor("'");
                Set msg = msg.append(name);
                Set msg = msg.append(String::Constructor("' is deprecated"));

                Run comp.warning(msg);
            }
        ]
    ]
]
```

**Example: Location-Specific Warnings**

```xxml
// Emit warnings at specific locations (not just annotation site)
Method <onAnnotate> Returns None Parameters (
    Parameter <ctx> Types ReflectionContext&,
    Parameter <comp> Types CompilationContext&
) -> {
    // Warning at a different location
    Run comp.warningAt(
        String::Constructor("Consider refactoring this code"),
        ctx.getSourceFile(),
        ctx.getLineNumber(),
        Integer::Constructor(1)  // Column 1
    );
}
```

---

##### Annotation Arguments Access

Within processors, you can access the arguments passed to the annotation at the usage site. For inline processors, arguments are accessible through `ReflectionContext` methods (see above). For DLL-based processors, arguments are also passed directly to the processor function.

**For inline processors (via ReflectionContext):**

```xxml
// Type-aware access - compiler determines return type from annotation definition
Instantiate Integer^ As <min> = ctx.getAnnotationArg(String::Constructor("min"));
Instantiate String^ As <name> = ctx.getAnnotationArg(String::Constructor("name"));
```

**For DLL-based processors (C API):**

| Function | Returns | Description |
|----------|---------|-------------|
| `Processor_getArgCount(args)` | `int` | Number of arguments passed to annotation |
| `Processor_getArg(args, name)` | `ProcessorAnnotationArg*` | Get argument by name, NULL if not found |
| `Processor_getArgAt(args, index)` | `ProcessorAnnotationArg*` | Get argument by index |
| `Processor_argGetName(arg)` | `const char*` | Get argument name |
| `Processor_argAsInt(arg)` | `int64_t` | Get integer value (0 if wrong type) |
| `Processor_argAsString(arg)` | `const char*` | Get string value (empty if wrong type) |
| `Processor_argAsBool(arg)` | `int` | Get boolean value (0/1) |
| `Processor_argAsDouble(arg)` | `double` | Get double value (0.0 if wrong type) |

**Argument Types:**

| Type | Enum Value | Description |
|------|------------|-------------|
| Integer | `PROCESSOR_ARG_INT` | 64-bit signed integer |
| String | `PROCESSOR_ARG_STRING` | String literal |
| Bool | `PROCESSOR_ARG_BOOL` | Boolean (true/false) |
| Double | `PROCESSOR_ARG_DOUBLE` | 64-bit floating point |

---

##### Use Cases

Inline processors enable compile-time logic for:

- **Custom validation rules** - Enforce coding standards and constraints
- **Deprecation warnings** - Custom deprecation messages with migration hints
- **Code generation triggers** - Signal to external tools or generate code
- **Compile-time computations** - Validate annotation arguments
- **Documentation generation** - Extract metadata during compilation
- **Access control checks** - Verify proper usage of sensitive APIs
- **Naming convention enforcement** - Check element names follow patterns
- **Type constraints** - Ensure annotations are applied to correct types

---

## Quick Reference: Processor APIs

### ReflectionContext Methods Summary

| Category | Method | Returns | Description |
|----------|--------|---------|-------------|
| **Target Info** | `getTargetKind()` | `String^` | `"class"`, `"method"`, `"property"`, `"variable"` |
| | `getTargetName()` | `String^` | Name of annotated element |
| | `getTypeName()` | `String^` | Type for properties/variables |
| | `getClassName()` | `String^` | Containing class name |
| | `getNamespaceName()` | `String^` | Containing namespace |
| **Location** | `getSourceFile()` | `String^` | Source file path |
| | `getLineNumber()` | `Integer^` | Line number (1-based) |
| | `getColumnNumber()` | `Integer^` | Column number (1-based) |
| **Class** | `getPropertyCount()` | `Integer^` | Number of properties |
| | `getPropertyNameAt(i)` | `String^` | Property name at index |
| | `getPropertyTypeAt(i)` | `String^` | Property type at index |
| | `getPropertyOwnershipAt(i)` | `String^` | `"^"`, `"&"`, `"%"`, or `""` |
| | `getMethodCount()` | `Integer^` | Number of methods |
| | `getMethodNameAt(i)` | `String^` | Method name at index |
| | `getMethodReturnTypeAt(i)` | `String^` | Method return type |
| | `hasMethod(name)` | `Bool^` | Check method exists |
| | `hasProperty(name)` | `Bool^` | Check property exists |
| | `getBaseClassName()` | `String^` | Base class name |
| | `isClassFinal()` | `Bool^` | Is class final? |
| **Method** | `getParameterCount()` | `Integer^` | Number of parameters |
| | `getParameterNameAt(i)` | `String^` | Parameter name at index |
| | `getParameterTypeAt(i)` | `String^` | Parameter type at index |
| | `getReturnTypeName()` | `String^` | Method return type |
| | `isMethodStatic()` | `Bool^` | Is method static? |
| **Property** | `getOwnership()` | `String^` | Ownership marker |
| | `hasDefaultValue()` | `Bool^` | Has default value? |
| **Value** | `hasTargetValue()` | `Bool^` | Is value available? |
| | `getTargetValueType()` | `Integer^` | 0=none, 1=int, 2=string, 3=bool, 4=double |
| | `getTargetValue()` | `<T>&` | **Type-aware** reference to value |
| **Annotation Args** | `getAnnotationArg(name)` | `<T>^` | **Type-aware** value by name |
| | `getAnnotationArgCount()` | `Integer^` | Number of annotation arguments |
| | `getAnnotationArgNameAt(i)` | `String^` | Argument name at index |

### CompilationContext Methods Summary

| Method | Parameters | Description |
|--------|------------|-------------|
| `message(String^)` | Message text | Emit informational message |
| `warning(String^)` | Warning text | Emit warning at annotation site |
| `warningAt(String^, String^, Integer^, Integer^)` | msg, file, line, col | Emit warning at location |
| `error(String^)` | Error text | Emit error (stops compilation) |
| `errorAt(String^, String^, Integer^, Integer^)` | msg, file, line, col | Emit error at location |
| `hasErrors()` | — | Check if errors occurred |

---

## Runtime Reflection API

For annotations marked with `Retain`, you can query them at runtime using the reflection API.

### Required Import

```xxml
#import Language::Reflection;
```

### AnnotationInfo Class

Provides access to annotation metadata:

```xxml
// Get type information
Instantiate Type^ As <type> = Type::forName(String::Constructor("MyClass"));

// Get annotation count
Instantiate Integer^ As <count> = type.getAnnotationCount();

// Get annotation by index
Instantiate AnnotationInfo^ As <annot> = type.getAnnotationAt(Integer::Constructor(0));

// Get annotation name
Instantiate String^ As <name> = annot.getName();

// Check if annotation exists
Instantiate Bool^ As <hasAnnot> = annot.hasArgument(String::Constructor("format"));
```

### AnnotationArg Class

Provides access to annotation argument values:

```xxml
// Get argument count
Instantiate Integer^ As <argCount> = annot.getArgumentCount();

// Get argument by index
Instantiate AnnotationArg^ As <arg> = annot.getArgumentAt(Integer::Constructor(0));

// Get argument by name
Instantiate AnnotationArg^ As <formatArg> = annot.getArgumentByName(String::Constructor("format"));

// Access argument properties
Instantiate String^ As <argName> = arg.getName();
Instantiate String^ As <argType> = arg.getTypeString();

// Get typed values
Instantiate Integer^ As <intVal> = arg.asInteger();
Instantiate String^ As <strVal> = arg.asString();
Instantiate Bool^ As <boolVal> = arg.asBool();
Instantiate Double^ As <dblVal> = arg.asDouble();
```

### Complete Example

```xxml
#import Language::Core;
#import Language::Reflection;

// Define retained annotation
[ Annotation <Config> Allows (AnnotationAllow::Classes) Retain
    Annotate (String^)(name);
    Annotate (Integer^)(version);
]

@Config(name = "MyApp", version = 1)
[ Class <Application> Final Extends None
    [ Public <>
        Constructor = default;
    ]
]

[ Entrypoint
    {
        // Get type info
        Instantiate Type^ As <type> = Type::forName(String::Constructor("Application"));

        // Read annotation
        Instantiate AnnotationInfo^ As <config> = type.getAnnotationAt(Integer::Constructor(0));
        Run Console::printLine(config.getName());

        // Read arguments
        Instantiate AnnotationArg^ As <nameArg> = config.getArgumentByName(String::Constructor("name"));
        Run Console::printLine(nameArg.asString());

        Instantiate AnnotationArg^ As <verArg> = config.getArgumentByName(String::Constructor("version"));
        Run Console::printLine(verArg.asInteger().toString());

        Exit(0);
    }
]
```

---

## Examples

### Serialization Framework

```xxml
// Define serialization annotations
[ Annotation <Serializable> Allows (AnnotationAllow::Classes) Retain
    Annotate (String^)(format);
]

[ Annotation <SerializedName> Allows (AnnotationAllow::Properties) Retain
    Annotate (String^)(value);
]

[ Annotation <Transient> Allows (AnnotationAllow::Properties) Retain ]

// Use annotations
@Serializable(format = "json")
[ Class <User> Final Extends None
    [ Public <>
        @SerializedName(value = "user_id")
        Property <id> Types Integer^;

        @SerializedName(value = "full_name")
        Property <name> Types String^;

        @Transient  // Not serialized
        Property <sessionToken> Types String^;

        Constructor = default;
    ]
]
```

### Validation Framework

```xxml
// Define validation annotations
[ Annotation <NotNull> Allows (AnnotationAllow::Properties) Retain ]

[ Annotation <Range> Allows (AnnotationAllow::Properties) Retain
    Annotate (Integer^)(min);
    Annotate (Integer^)(max);
]

[ Annotation <Pattern> Allows (AnnotationAllow::Properties) Retain
    Annotate (String^)(regex);
]

// Use annotations
[ Class <Registration> Final Extends None
    [ Public <>
        @NotNull
        @Pattern(regex = "^[a-zA-Z0-9_]+$")
        Property <username> Types String^;

        @NotNull
        @Range(min = 8, max = 128)
        Property <passwordLength> Types Integer^;

        @NotNull
        @Pattern(regex = "^[^@]+@[^@]+\\.[^@]+$")
        Property <email> Types String^;

        Constructor = default;
    ]
]
```

### Dependency Injection

```xxml
// Define DI annotations
[ Annotation <Injectable> Allows (AnnotationAllow::Classes) Retain ]

[ Annotation <Inject> Allows (AnnotationAllow::Properties, AnnotationAllow::Constructors) Retain ]

[ Annotation <Named> Allows (AnnotationAllow::Properties) Retain
    Annotate (String^)(value);
]

// Use annotations
@Injectable
[ Class <UserService> Final Extends None
    [ Public <>
        @Inject
        @Named(value = "primaryDatabase")
        Property <database> Types Database^;

        @Inject
        Property <logger> Types Logger^;

        Constructor = default;
    ]
]
```

---

## Grammar Reference

```ebnf
annotation_definition ::=
    "[" "Annotation" "<" identifier ">"
        "Allows" "(" annotation_targets ")"
        retention?
        annotate_statement*
    "]"

annotation_targets ::=
    annotation_target ("," annotation_target)*

annotation_target ::=
    "AnnotationAllow" "::" target_name

target_name ::=
    "Classes" | "Methods" | "Properties" | "Parameters" | "Constructors" | "All"

retention ::=
    "Retain"

annotate_statement ::=
    "Annotate" "(" type_specifier ")" "(" identifier ")" default_value? ";"

default_value ::=
    "=" literal

annotation_usage ::=
    "@" identifier annotation_arguments?

annotation_arguments ::=
    "(" annotation_argument ("," annotation_argument)* ")"

annotation_argument ::=
    identifier "=" literal

literal ::=
    integer_literal | string_literal | boolean_literal | double_literal
```

---

## Implementation Details

### Implementation Status

| Feature | Status |
|---------|--------|
| Annotation definition syntax | ✅ Implemented |
| Annotation usage (`@Name(args)`) | ✅ Implemented |
| Annotation parameters (`Annotate (Type^)(name)`) | ✅ Implemented |
| Default parameter values (`= value`) | ✅ Implemented |
| Target validation (`Allows`) | ✅ Implemented |
| Required argument validation | ✅ Implemented |
| Unknown argument detection | ✅ Implemented |
| Undefined annotation detection | ✅ Implemented |
| Multiple annotations on same element | ✅ Implemented |
| Runtime retention (`Retain`) | ✅ Implemented |
| Runtime reflection API | ✅ Implemented |
| Built-in `@Deprecated` processor | ✅ Implemented |
| Processor C API (`xxml_processor_api.h`) | ✅ Implemented |
| Processor DLL loading (`--use-processor`) | ✅ Implemented |
| Processor DLL auto-discovery | ✅ Implemented |
| Processor registry infrastructure | ✅ Implemented |
| Built-in `@Override` processor | ❌ Not yet implemented |
| Built-in `@Suppress` processor | ❌ Not yet implemented |
| XXML processor compilation (`--processor`) | ✅ Implemented |
| Processor DLL generation (MSVC/GNU) | ✅ Implemented |
| **Inline Processors** | |
| Inline `[ Processor ]` parsing | ✅ Implemented |
| `onAnnotate` method validation | ✅ Implemented |
| `ReflectionContext` intrinsic type | ✅ Implemented |
| `CompilationContext` intrinsic type | ✅ Implemented |
| Inline processor detection | ✅ Implemented |
| Inline processor auto-compilation | ✅ Implemented |
| Inline processor execution | ✅ Implemented |
| Processor access to imports | ✅ Implemented |
| Processor access to user classes | ✅ Implemented |
| **Extended Reflection** | |
| Class inspection (properties, methods) | ✅ Implemented |
| Method inspection (parameters, return type) | ✅ Implemented |
| Property inspection (ownership) | ✅ Implemented |
| Type-aware `getTargetValue()` | ✅ Implemented |
| Type-aware `getAnnotationArg()` | ✅ Implemented |
| Constant initializer evaluation | ✅ Implemented |

### Validation Errors

The compiler validates annotation usage and reports clear error messages:

| Validation | Error Message |
|------------|---------------|
| Unknown annotation | `Unknown annotation '@Name'` |
| Invalid target | `Annotation '@Name' cannot be applied to <target>. Allowed targets: <list>` |
| Missing required arg | `Missing required argument 'argName' for annotation '@Name'` |
| Unknown argument | `Unknown argument 'argName' for annotation '@Name'` |

### Compiler Processing Pipeline

```
Source (.XXML)
     ↓
[Lexer] - Tokenize @, Annotation keywords
     ↓
[Parser] - Build AST with AnnotationDecl, AnnotationUsage, ProcessorDecl nodes
     ↓
[Semantic Analyzer] - Validate annotations, detect inline processors,
                      collect imports and user-defined classes
     ↓
[Inline Processor Compiler] - Generate processor source with imports and user classes,
                              compile to DLL via subprocess
     ↓
[Processor Registry] - Load compiled processor DLLs
     ↓
[Annotation Processor] - Run built-in and user-defined processors,
                         execute onAnnotate for each usage
     ↓
[Error Check] - Stop compilation if any processor reported errors
     ↓
[LLVM Backend] - Generate code including runtime metadata
     ↓
[Linker] - Link object file to executable or DLL
     ↓
Native Executable / Processor DLL
```

### Runtime Storage

Retained annotations are stored in global metadata structures:

```c
// C runtime structures
typedef struct {
    const char* name;
    int type;  // 0=int, 1=string, 2=bool, 3=double
    union {
        int64_t int_value;
        const char* string_value;
        int bool_value;
        double double_value;
    };
} ReflectionAnnotationArg;

typedef struct {
    const char* name;
    int arg_count;
    ReflectionAnnotationArg* args;
} ReflectionAnnotationInfo;
```

### File Inventory

**Compiler Source:**
- `include/AnnotationProcessor/AnnotationProcessor.h` - Processor framework interface
- `src/AnnotationProcessor/AnnotationProcessor.cpp` - Built-in processor implementations
- `include/AnnotationProcessor/ProcessorLoader.h` - DLL loading interface
- `src/AnnotationProcessor/ProcessorLoader.cpp` - Platform-specific DLL loading
- `include/AnnotationProcessor/ProcessorCompiler.h` - Inline processor compilation interface
- `src/AnnotationProcessor/ProcessorCompiler.cpp` - AST serialization and subprocess compilation

**Processor API Runtime:**
- `runtime/xxml_processor_api.h` - C API header for processor DLLs
- `runtime/xxml_processor_api.c` - Processor API implementation

**Annotation Runtime:**
- `runtime/xxml_annotation_runtime.h` - C structures for runtime annotations
- `runtime/xxml_annotation_runtime.c` - Annotation registry implementation

**XXML API:**
- `Language/Reflection/AnnotationInfo.XXML` - XXML wrapper for annotation metadata
- `Language/Reflection/AnnotationArg.XXML` - XXML wrapper for annotation arguments

---

## See Also

- [Language Specification](LANGUAGE_SPEC.md) - Complete language syntax
- [Reflection System](REFLECTION_SYSTEM.md) - Runtime type introspection
- [Advanced Features](ADVANCED_FEATURES.md) - Native types and syscalls
- [Templates](TEMPLATES.md) - Generic programming

---

**XXML Annotation System v3.0**

*Last updated: Full DLL generation, processor access to user-defined code, and error reporting implemented.*
