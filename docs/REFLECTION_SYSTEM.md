# XXML Reflection System

## Status: **FULLY OPERATIONAL**

The XXML Reflection System is **complete and functional**! All components are implemented:
- Runtime infrastructure (C structures and type registry)
- XXML API classes (Type, MethodInfo, PropertyInfo, ParameterInfo, GetType)
- Syscall implementations (~30 reflection functions)
- Metadata generation in compiler (LLVMBackend)
- Automatic type registration
- Test suite

## Overview

The XXML Reflection System provides Java-like runtime type introspection capabilities, allowing programs to inspect classes, methods, properties, and template parameters at runtime.

---

## XXML Reflection API

The reflection API is located in `Language/Reflection/` and consists of five classes.

### Type.XXML

The central class for type introspection. Get a `Type^` instance via static lookup or the `GetType<T>` template.

```xxml
// Static type lookup by fully qualified name
Language::Reflection::Type::forName(String&) -> Type^  // Returns None if not found

// Instance methods
type.getName() -> String^                    // Simple name (e.g., "Person")
type.getFullName() -> String^                // Qualified name (e.g., "Container::Box<Integer>")
type.getNamespace() -> String^               // Namespace component
type.isTemplate() -> Bool^                   // Check if template type
type.getTemplateParameterCount() -> Integer^ // Number of template params
type.getPropertyCount() -> Integer^          // Number of properties
type.getPropertyAt(Integer&) -> PropertyInfo^    // Get property by index
type.getProperty(String&) -> PropertyInfo^       // Get property by name
type.getMethodCount() -> Integer^            // Number of methods
type.getMethodAt(Integer&) -> MethodInfo^        // Get method by index
type.getMethod(String&) -> MethodInfo^           // Get method by name
type.getInstanceSize() -> Integer^           // Size in bytes
```

### GetType.XXML

Template-based type-safe accessor for compile-time type lookup.

```xxml
// Usage:
Instantiate Language::Reflection::GetType<MyClass>^ As <getter> =
    Language::Reflection::GetType<MyClass>::Constructor();
Instantiate Language::Reflection::Type^ As <type> = getter.get();

// API
GetType<T>::get() -> Type^  // Returns Type info for T using __typename(T) intrinsic
```

The `GetType<T>` template uses the `__typename(T)` compiler intrinsic to get the type name at compile time, providing type safety over the string-based `Type::forName()`.

### MethodInfo.XXML

Method introspection with parameter details.

```xxml
method.getName() -> String^              // Method name
method.getReturnType() -> String^        // Return type name
method.getReturnOwnership() -> Integer^  // Return ownership (0=None, 1=^, 2=&, 3=%)
method.getParameterCount() -> Integer^   // Number of parameters
method.getParameterAt(Integer&) -> ParameterInfo^  // Get parameter by index
method.isStatic() -> Bool^               // Check if static method
method.isConstructor() -> Bool^          // Check if constructor
method.getSignature() -> String^         // Full signature (e.g., "String greet(Integer, String)")
```

### PropertyInfo.XXML

Property introspection with ownership tracking.

```xxml
property.getName() -> String^            // Property name
property.getTypeName() -> String^        // Type name
property.getOwnership() -> Integer^      // Ownership code (0-3)
property.getOwnershipString() -> String^ // Human-readable: "Owned(^)", "Reference(&)", "Copy(%)", "None"
property.getOffset() -> Integer^         // Byte offset from object start
```

### ParameterInfo.XXML

Method parameter introspection.

```xxml
parameter.getName() -> String^           // Parameter name
parameter.getTypeName() -> String^       // Type name
parameter.getOwnership() -> Integer^     // Ownership code (0-3)
parameter.getOwnershipString() -> String^// Human-readable ownership
```

### Ownership Codes

| Code | Symbol | Meaning | String |
|------|--------|---------|--------|
| 0 | (none) | Value/None | "None" |
| 1 | `^` | Owned | "Owned(^)" |
| 2 | `&` | Reference | "Reference(&)" |
| 3 | `%` | Copy | "Copy(%)" |

---

## Runtime Infrastructure

### C Structures (`runtime/xxml_reflection_runtime.h`)

```c
// Property metadata
struct ReflectionPropertyInfo {
    const char* name;
    const char* typeName;
    int32_t ownership;  // 0=None, 1=Owned(^), 2=Reference(&), 3=Copy(%)
    size_t offset;      // Byte offset from object start
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
    void* functionPointer;  // Pointer to method implementation
    bool isStatic;
    bool isConstructor;
};

// Template parameter metadata
struct ReflectionTemplateParamInfo {
    const char* name;
    bool isTypeParameter;   // true for type, false for value
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
};
```

### Global Type Registry (`runtime/xxml_reflection_runtime.c`)

The runtime maintains a global type registry with these functions:

```c
// Register a new type (called automatically at module init)
void* Reflection_registerType(ReflectionTypeInfo* typeInfo);

// Look up type by full name (e.g., "Math::Vector2")
ReflectionTypeInfo* Reflection_getTypeInfo(const char* typeName);

// Get total number of registered types
int32_t Reflection_getTypeCount();

// Get array of all registered type names
const char** Reflection_getAllTypeNames();
```

The registry uses a dynamic array with automatic resizing (doubling strategy, starting at 16 entries).

### Syscall Functions

Type introspection:
- `xxml_reflection_getTypeByName(const char*)` - Lookup type by name
- `xxml_reflection_type_getName(void*)` - Get simple name
- `xxml_reflection_type_getFullName(void*)` - Get qualified name
- `xxml_reflection_type_getNamespace(void*)` - Get namespace
- `xxml_reflection_type_isTemplate(void*)` - Check if template
- `xxml_reflection_type_getTemplateParamCount(void*)` - Count template params
- `xxml_reflection_type_getPropertyCount(void*)` - Count properties
- `xxml_reflection_type_getProperty(void*, int64_t)` - Get property by index
- `xxml_reflection_type_getPropertyByName(void*, const char*)` - Get property by name
- `xxml_reflection_type_getMethodCount(void*)` - Count methods
- `xxml_reflection_type_getMethod(void*, int64_t)` - Get method by index
- `xxml_reflection_type_getMethodByName(void*, const char*)` - Get method by name
- `xxml_reflection_type_getInstanceSize(void*)` - Get size in bytes

Property introspection:
- `xxml_reflection_property_getName(void*)` - Get property name
- `xxml_reflection_property_getTypeName(void*)` - Get type name
- `xxml_reflection_property_getOwnership(void*)` - Get ownership code
- `xxml_reflection_property_getOffset(void*)` - Get byte offset

Method introspection:
- `xxml_reflection_method_getName(void*)` - Get method name
- `xxml_reflection_method_getReturnType(void*)` - Get return type
- `xxml_reflection_method_getReturnOwnership(void*)` - Get return ownership
- `xxml_reflection_method_getParameterCount(void*)` - Count parameters
- `xxml_reflection_method_getParameter(void*, int64_t)` - Get parameter by index
- `xxml_reflection_method_isStatic(void*)` - Check if static
- `xxml_reflection_method_isConstructor(void*)` - Check if constructor

Parameter introspection:
- `xxml_reflection_parameter_getName(void*)` - Get parameter name
- `xxml_reflection_parameter_getTypeName(void*)` - Get type name
- `xxml_reflection_parameter_getOwnership(void*)` - Get ownership code

---

## Compiler Integration

### Metadata Collection (LLVMBackend)

During `visit(ClassDecl&)`, the compiler collects metadata for each class:

```cpp
struct ReflectionClassMetadata {
    std::string name;                  // "Person"
    std::string namespaceName;         // "" or "Container"
    std::string fullName;              // "Person" or "Container::Box<Integer>"
    std::vector<std::pair<std::string, std::string>> properties;  // (name, type)
    std::vector<std::string> propertyOwnerships;  // "^", "&", "%", or ""
    std::vector<std::pair<std::string, std::string>> methods;     // (name, returnType)
    std::vector<std::string> methodReturnOwnerships;
    std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> methodParameters;
    bool isTemplate;
    std::vector<std::string> templateParams;
    size_t instanceSize;              // Calculated via calculateClassSize()
    Parser::ClassDecl* astNode;
};
```

### Metadata Generation

The `generateReflectionMetadata()` method emits LLVM IR:

1. **String literals** for all names
2. **ReflectionPropertyInfo arrays** per class
3. **ReflectionParameterInfo arrays** per method
4. **ReflectionMethodInfo arrays** per class
5. **ReflectionTemplateParamInfo arrays** for template classes
6. **ReflectionTypeInfo global structures** per class
7. **Module initialization function** (`__reflection_init`)
8. **Global constructor registration** at priority 65535

Example generated IR:
```llvm
; String literals
@.str.reflect_Person = private constant [7 x i8] c"Person\00"

; Property array
@reflection_props_Person = private constant [2 x %ReflectionPropertyInfo] [
  %ReflectionPropertyInfo { ptr @.str.name, ptr @.str.String, i32 1, i64 0 },
  %ReflectionPropertyInfo { ptr @.str.age, ptr @.str.Integer, i32 1, i64 8 }
]

; Type info structure
@reflection_type_Person = global %ReflectionTypeInfo {
  ptr @.str.name_Person,      ; name
  ptr @.str.ns_,              ; namespace
  ptr @.str.full_Person,      ; fullName
  i1 false,                   ; isTemplate
  i32 0,                      ; templateParamCount
  ptr null,                   ; templateParams
  i32 2,                      ; propertyCount
  ptr @reflection_props_Person,
  i32 3,                      ; methodCount
  ptr @reflection_methods_Person,
  i32 0,                      ; constructorCount
  ptr null,
  ptr null,                   ; baseClassName
  i64 16                      ; instanceSize
}

; Module initialization
define internal void @__reflection_init() {
  call void @Reflection_registerType(ptr @reflection_type_Person)
  ret void
}

; Global constructor
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 65535, ptr @__reflection_init, ptr null }
]
```

---

## Usage Examples

### Basic Type Introspection

```xxml
#import Language::Reflection;

// Look up type by name
Instantiate Language::Reflection::Type^ As <type> =
    Language::Reflection::Type::forName(String::Constructor("Person"));

// Check if type was found
If (type == None::Constructor()) -> {
    Run Console::WriteLine(String::Constructor("Type not found!"));
    Return Integer::Constructor(1);
}

// Get type information
Instantiate String^ As <name> = type.getName();
Instantiate String^ As <fullName> = type.getFullName();
Instantiate Integer^ As <size> = type.getInstanceSize();

Run Console::WriteLine(String::Constructor("Type: "));
Run Console::WriteLine(fullName);
```

### Type-Safe Lookup with GetType<T>

```xxml
#import Language::Reflection;

// Compile-time type-safe lookup
Instantiate Language::Reflection::GetType<Person>^ As <getter> =
    Language::Reflection::GetType<Person>::Constructor();
Instantiate Language::Reflection::Type^ As <type> = getter.get();

// This fails at compile time if Person doesn't exist
```

### Property Enumeration

```xxml
Instantiate Integer^ As <propCount> = type.getPropertyCount();
Instantiate NativeType<"int64">^ As <i> = 0;

While (i < propCount.toInt64()) -> {
    Instantiate Language::Reflection::PropertyInfo^ As <prop> =
        type.getPropertyAt(Integer::Constructor(i));

    Instantiate String^ As <propName> = prop.getName();
    Instantiate String^ As <typeName> = prop.getTypeName();
    Instantiate String^ As <ownership> = prop.getOwnershipString();
    Instantiate Integer^ As <offset> = prop.getOffset();

    Run Console::Write(String::Constructor("  "));
    Run Console::Write(propName);
    Run Console::Write(String::Constructor(": "));
    Run Console::Write(typeName);
    Run Console::Write(String::Constructor(" "));
    Run Console::WriteLine(ownership);

    Set i = i + 1;
}
```

### Method Introspection

```xxml
Instantiate Language::Reflection::MethodInfo^ As <method> =
    type.getMethod(String::Constructor("greet"));

If (method != None::Constructor()) -> {
    Instantiate String^ As <signature> = method.getSignature();
    Instantiate Bool^ As <isStatic> = method.isStatic();
    Instantiate Bool^ As <isCtor> = method.isConstructor();

    Run Console::Write(String::Constructor("Signature: "));
    Run Console::WriteLine(signature);

    // Enumerate parameters
    Instantiate Integer^ As <paramCount> = method.getParameterCount();
    Instantiate NativeType<"int64">^ As <p> = 0;

    While (p < paramCount.toInt64()) -> {
        Instantiate Language::Reflection::ParameterInfo^ As <param> =
            method.getParameterAt(Integer::Constructor(p));

        Instantiate String^ As <paramName> = param.getName();
        Instantiate String^ As <paramType> = param.getTypeName();
        Instantiate String^ As <paramOwnership> = param.getOwnershipString();

        Run Console::Write(String::Constructor("    Param: "));
        Run Console::Write(paramName);
        Run Console::Write(String::Constructor(" : "));
        Run Console::Write(paramType);
        Run Console::Write(String::Constructor(" "));
        Run Console::WriteLine(paramOwnership);

        Set p = p + 1;
    }
}
```

### Template Type Detection

```xxml
Instantiate Language::Reflection::Type^ As <boxType> =
    Language::Reflection::Type::forName(String::Constructor("Container::Box<Integer>"));

Instantiate Bool^ As <isTemplate> = boxType.isTemplate();
If (isTemplate.getValue()) -> {
    Instantiate Integer^ As <templateParamCount> = boxType.getTemplateParameterCount();
    Run Console::Write(String::Constructor("Template with "));
    Run Console::Write(templateParamCount.toString());
    Run Console::WriteLine(String::Constructor(" type parameters"));
}
```

---

## Test Suite

Located in `tests/`:

### reflection_basic.XXML
- Type lookup by name
- Property enumeration
- Method enumeration
- Property/method lookup by name
- Ownership type inspection

### reflection_template.XXML
- Template type detection
- Template parameter counting
- Distinguishing `Box<Integer>` from `Box<String>`
- Template method signatures

### reflection_methods.XXML
- Method signature generation
- Parameter details with ownership
- Constructor detection
- Return type analysis
- Ownership comparison across methods

---

## Architecture Decisions

### 1. C Runtime Foundation
- Simple C structures for maximum compatibility
- No C++ STL dependencies in runtime
- Easy to marshal across language boundaries

### 2. Syscall-Based API
- All XXML reflection calls go through syscalls
- Syscalls are thin wrappers around C functions
- Enables future optimizations and caching

### 3. Global Type Registry
- Single source of truth for all type information
- Populated at module initialization (priority 65535)
- O(n) lookup (can be optimized to hash map)

### 4. Ownership Awareness
- Full ownership type tracking (`^`, `&`, `%`)
- Enables memory safety analysis via reflection
- Supports advanced metaprogramming

### 5. Template Monomorphization Support
- Each instantiation (`List<Integer>`, `List<String>`) is a distinct type
- Template parameter information preserved
- Enables type-safe generic programming

---

## Performance Considerations

### Metadata Size
- Each class adds ~100-500 bytes of metadata
- Scales linearly with number of types
- Consider lazy loading for large programs

### Lookup Performance
- Current: O(n) linear search in type registry
- Recommended optimization: Hash map for O(1) lookup
- Cache frequently accessed TypeInfo pointers

### Runtime Overhead
- Reflection calls have syscall overhead
- Minimize reflection in hot paths
- Cache reflection results when possible

---

## Security Considerations

### Information Disclosure
- Reflection exposes all members (no access control)
- Consider access control in future (see [ANNOTATIONS.md](ANNOTATIONS.md) for annotation-based metadata)
- May expose sensitive class structure

### Dynamic Invocation
- Method invocation via reflection not yet implemented
- When added, validate all reflected calls
- Consider sandboxing for untrusted code

---

## Future Enhancements

### Short Term
- [ ] Dynamic method invocation
- [ ] Property get/set via reflection
- [ ] Hash map type registry for O(1) lookup
- [x] Attribute/annotation support (completed)

### Long Term
- [ ] Reflection-based serialization/deserialization
- [ ] Automatic toString() generation
- [ ] Dependency injection framework
- [ ] Unit test framework using reflection
- [ ] ORM (Object-Relational Mapping) support

---

## File Inventory

### Runtime Files
- `runtime/xxml_reflection_runtime.h` - C structures
- `runtime/xxml_reflection_runtime.c` - Registry implementation
- `runtime/xxml_llvm_runtime.h` - Syscall declarations

### XXML API Classes
- `Language/Reflection/Type.XXML` - Type introspection
- `Language/Reflection/MethodInfo.XXML` - Method introspection
- `Language/Reflection/PropertyInfo.XXML` - Property introspection
- `Language/Reflection/ParameterInfo.XXML` - Parameter introspection
- `Language/Reflection/GetType.XXML` - Type-safe template accessor
- `Language/Reflection.XXML` - Module aggregation

> **Note:** Annotation-related classes (`AnnotationInfo.XXML`, `AnnotationArg.XXML`) and runtime files are documented in [ANNOTATIONS.md](ANNOTATIONS.md).

### Compiler Backend
- `include/Backends/LLVMBackend.h` - ReflectionClassMetadata struct
- `src/Backends/LLVMBackend.cpp` - Metadata collection and generation

### Tests
- `tests/reflection_basic.XXML`
- `tests/reflection_template.XXML`
- `tests/reflection_methods.XXML`

---

## Contributing

When adding reflection features:

1. Update C structures in `xxml_reflection_runtime.h`
2. Add implementations in `xxml_reflection_runtime.c`
3. Add syscall declarations to `xxml_llvm_runtime.h`
4. Extend XXML classes in `Language/Reflection/`
5. Add tests in `tests/reflection_*.XXML`
6. Update this documentation

---

## See Also

- [Annotations](ANNOTATIONS.md) - Java-style annotation system with runtime retention
- [Language Specification](LANGUAGE_SPEC.md) - Complete language syntax
- [Advanced Features](ADVANCED_FEATURES.md) - Native types and syscalls
- [Templates](TEMPLATES.md) - Generic programming
- [Limitations](LIMITATIONS.md) - Known reflection limitations
