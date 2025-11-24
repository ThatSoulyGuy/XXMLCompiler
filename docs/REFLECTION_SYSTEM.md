# XXML Reflection System

## Status: ✅ **FULLY OPERATIONAL**

The XXML Reflection System is **complete and functional**! All components are implemented:
- ✅ Runtime infrastructure (C structures and type registry)
- ✅ XXML API classes (Type, Method, Property, Parameter)
- ✅ Syscall implementations (~30 reflection functions)
- ✅ **Metadata generation in compiler** (LLVMBackend)
- ✅ Automatic type registration
- ✅ Test suite

## Overview

The XXML Reflection System provides Java-like runtime type introspection capabilities, allowing programs to inspect classes, methods, properties, and template parameters at runtime.

## Implementation Status

### ✅ Completed Components

#### 1. Runtime Infrastructure (`runtime/`)
- **xxml_reflection_runtime.h** - C structures for reflection metadata
  - `ReflectionTypeInfo` - Complete type information
  - `ReflectionMethodInfo` - Method signatures and metadata
  - `ReflectionPropertyInfo` - Property details with ownership
  - `ReflectionParameterInfo` - Parameter information
  - Global type registry with lookup functions

- **xxml_reflection_runtime.c** - Runtime implementation
  - Dynamic type registry with auto-resizing
  - Type lookup by name
  - Thread-safe registration (can be enhanced)

- **xxml_llvm_runtime.h/.c** - Reflection syscalls (~30 functions)
  - Type introspection syscalls
  - Method introspection syscalls
  - Property introspection syscalls
  - Parameter introspection syscalls

#### 2. XXML Reflection API (`Language/Reflection/`)

##### Type.XXML
```xxml
Language::Reflection::Type::forName(String)^ -> Type^
type.getName() -> String^
type.getFullName() -> String^
type.getNamespace() -> String^
type.isTemplate() -> Bool^
type.getTemplateParameterCount() -> Integer^
type.getPropertyCount() -> Integer^
type.getPropertyAt(Integer) -> Property^
type.getProperty(String) -> Property^
type.getMethodCount() -> Integer^
type.getMethodAt(Integer) -> Method^
type.getMethod(String) -> Method^
type.getInstanceSize() -> Integer^
```

##### Method.XXML
```xxml
method.getName() -> String^
method.getReturnType() -> String^
method.getReturnOwnership() -> Integer^
method.getParameterCount() -> Integer^
method.getParameterAt(Integer) -> Parameter^
method.isStatic() -> Bool^
method.isConstructor() -> Bool^
method.getSignature() -> String^
```

##### Property.XXML
```xxml
property.getName() -> String^
property.getTypeName() -> String^
property.getOwnership() -> Integer^
property.getOwnershipString() -> String^  // "Owned(^)", "Reference(&)", etc.
property.getOffset() -> Integer^
```

##### Parameter.XXML
```xxml
parameter.getName() -> String^
parameter.getTypeName() -> String^
parameter.getOwnership() -> Integer^
parameter.getOwnershipString() -> String^
```

#### 3. Test Suite (`tests/`)

##### reflection_basic.XXML
Tests basic class introspection:
- Type lookup by name
- Property enumeration
- Method enumeration
- Property/method lookup by name
- Ownership type inspection

##### reflection_template.XXML
Tests template class reflection:
- Template type detection
- Template parameter count
- Distinguishing `Box<Integer>` from `Box<String>`
- Template method signatures

##### reflection_methods.XXML
Tests detailed method introspection:
- Method signature generation
- Parameter details with ownership
- Constructor detection
- Return type analysis
- Ownership comparison across methods

#### 4. Build System
- CMakeLists.txt automatically includes new runtime files (uses `runtime/*.c` glob)

---

## ✅ Implementation Complete

### Metadata Generation - **IMPLEMENTED**

The reflection system is **fully operational**! The compiler now automatically generates complete reflection metadata when emitting LLVM IR.

#### Implementation in LLVMBackend.cpp

The `generateReflectionMetadata()` method (lines 3414-3664) performs the following:

1. **Emits string literals for names:**
```cpp
// For each class/method/property name
output_ << "@.str.reflect_" << className << " = private constant ["
        << name.size() + 1 << " x i8] c\"" << name << "\\00\"\n";
```

2. **Emits ReflectionPropertyInfo arrays:**
```cpp
// For each class
output_ << "@reflection_props_" << className << " = private constant ["
        << properties.size() << " x %ReflectionPropertyInfo] [\n";

for (each property) {
    output_ << "  %ReflectionPropertyInfo { "
            << "ptr @.str.prop_" << propName << ", "  // name
            << "ptr @.str.type_" << propType << ", "  // typeName
            << "i32 " << ownershipValue << ", "        // ownership (0-3)
            << "i64 " << offsetInBytes << " "          // offset
            << "}";
}
output_ << "]\n";
```

3. **Emits ReflectionMethodInfo arrays:**
```cpp
// Similar structure for methods
output_ << "@reflection_methods_" << className << " = private constant ["
        << methods.size() << " x %ReflectionMethodInfo] [\n";

for (each method) {
    // Emit method info with parameters array
    // Include function pointer for potential dynamic invocation
}
output_ << "]\n";
```

4. **Emits ReflectionTypeInfo structure:**
```cpp
output_ << "@reflection_type_" << className
        << " = global %ReflectionTypeInfo {\n"
        << "  ptr @.str.name_" << className << ",\n"
        << "  ptr @.str.namespace_" << namespace << ",\n"
        << "  ptr @.str.fullname_" << fullName << ",\n"
        << "  i1 " << (isTemplate ? "true" : "false") << ",\n"
        << "  i32 " << templateParamCount << ",\n"
        << "  ptr " << (templateParamCount > 0 ? "@template_params_" + className : "null") << ",\n"
        << "  i32 " << propertyCount << ",\n"
        << "  ptr @reflection_props_" << className << ",\n"
        << "  i32 " << methodCount << ",\n"
        << "  ptr @reflection_methods_" << className << ",\n"
        << "  i32 " << constructorCount << ",\n"
        << "  ptr @reflection_constructors_" << className << ",\n"
        << "  ptr " << (baseClass.empty() ? "null" : "@.str.base_" + baseClass) << ",\n"
        << "  i64 " << sizeInBytes << "\n"
        << "}\n";
```

5. **Registers types at module initialization:**
```cpp
// In module initialization function or global constructor
output_ << "define internal void @__reflection_init() {\n";
for (each type) {
    output_ << "  call void @Reflection_registerType("
            << "ptr @reflection_type_" << className << ")\n";
}
output_ << "  ret void\n";
output_ << "}\n";

// Add to llvm.global_ctors
output_ << "@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] "
        << "[{ i32, ptr, ptr } { i32 65535, ptr @__reflection_init, ptr null }]\n";
```

#### Integration in LLVMBackend

**✅ Fully Integrated** - The metadata generation is automatically invoked:

1. **Metadata Collection** (LLVMBackend.cpp:849-912):
   - During `visit(ClassDecl&)`, metadata is collected for each class
   - Properties, methods, parameters, template params, and ownership info are stored
   - Stored in `reflectionMetadata_` map for later emission

2. **Metadata Emission** (LLVMBackend.cpp:62):
   - After all classes are visited, `generateReflectionMetadata()` is called
   - Emits complete LLVM IR structures for all reflection data
   - Registers types via global constructor `@__reflection_init`

#### Class Metadata Structure

**✅ Implemented** in LLVMBackend.h:246-260:

```cpp
struct ReflectionClassMetadata {
    std::string name;
    std::string namespaceName;
    std::string fullName;
    std::vector<std::pair<std::string, std::string>> properties;  // name, type
    std::vector<std::string> propertyOwnerships;  // ownership chars (^, &, %)
    std::vector<std::pair<std::string, std::string>> methods;  // name, return type
    std::vector<std::string> methodReturnOwnerships;
    std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> methodParameters;
    bool isTemplate;
    std::vector<std::string> templateParams;
    size_t instanceSize;  // Calculated via calculateClassSize()
    Parser::ClassDecl* astNode;
};
```

The `calculateClassSize()` method (LLVMBackend.cpp:3383-3412) computes instance sizes based on property types and ownership.

---

## Usage Examples

### Basic Type Introspection

```xxml
Instantiate Language::Reflection::Type^ As <type> =
    Language::Reflection::Type::forName(String::Constructor("MyClass"));

Instantiate Integer^ As <propCount> = type.getPropertyCount();
Instantiate Integer^ As <methodCount> = type.getMethodCount();
```

### Property Enumeration

```xxml
Instantiate NativeType<"int64">^ As <i> = 0;
While (i < propCount.toInt64()) -> {
    Instantiate Language::Reflection::Property^ As <prop> =
        type.getPropertyAt(Integer::Constructor(i));

    Instantiate String^ As <name> = prop.getName();
    Instantiate String^ As <typeName> = prop.getTypeName();
    Instantiate String^ As <ownership> = prop.getOwnershipString();

    // Use property info...

    Set i = i + 1;
}
```

### Method Introspection

```xxml
Instantiate Language::Reflection::Method^ As <method> =
    type.getMethod(String::Constructor("myMethod"));

Instantiate String^ As <returnType> = method.getReturnType();
Instantiate Integer^ As <paramCount> = method.getParameterCount();
Instantiate String^ As <signature> = method.getSignature();
```

### Template Type Detection

```xxml
Instantiate Language::Reflection::Type^ As <listType> =
    Language::Reflection::Type::forName(String::Constructor("List<Integer>"));

Instantiate Bool^ As <isTemplate> = listType.isTemplate();
If (isTemplate.getValue()) -> {
    Instantiate Integer^ As <paramCount> = listType.getTemplateParameterCount();
    // Template-specific handling
}
```

---

## Architecture Decisions

### 1. C Runtime Foundation
- Uses simple C structures for maximum compatibility
- No C++ STL dependencies
- Easy to marshal across language boundaries

### 2. Syscall-Based API
- All XXML reflection calls go through syscalls
- Syscalls are thin wrappers around C functions
- Enables future optimizations and caching

### 3. Global Type Registry
- Single source of truth for all type information
- Populated at module initialization
- Fast O(n) lookup (can be optimized to hash map)

### 4. Ownership Awareness
- Full ownership type tracking (^, &, %)
- Enables memory safety analysis via reflection
- Supports advanced metaprogramming

### 5. Template Monomorphization Support
- Each instantiation (`List<Integer>`, `List<String>`) is a distinct type
- Template parameter information preserved
- Enables type-safe generic programming

---

## Testing Strategy

1. **Unit Tests**: Test each reflection class in isolation
2. **Integration Tests**: Test cross-class reflection
3. **Template Tests**: Verify template type distinction
4. **Ownership Tests**: Verify ownership tracking accuracy
5. **Performance Tests**: Measure reflection overhead

---

## Future Enhancements

### Short Term
- [ ] Add method invocation support (dynamic dispatch)
- [ ] Add property get/set via reflection
- [ ] Optimize type registry with hash map
- [ ] Add attribute/annotation support

### Long Term
- [ ] Reflection-based serialization/deserialization
- [ ] Automatic toString() generation
- [ ] Dependency injection framework
- [ ] Unit test framework using reflection
- [ ] ORM (Object-Relational Mapping) support
- [ ] RPC (Remote Procedure Call) with reflection

---

## Performance Considerations

### Metadata Size
- Each class adds ~100-500 bytes of metadata
- Scales linearly with number of types
- Consider lazy loading for large programs

### Lookup Performance
- Current: O(n) linear search in type registry
- Recommended: Hash map for O(1) lookup
- Cache frequently accessed TypeInfo pointers

### Runtime Overhead
- Reflection calls have syscall overhead
- Minimize reflection in hot paths
- Cache reflection results when possible

---

## Security Considerations

### Information Disclosure
- Reflection exposes all private members
- Consider access control annotations
- May expose sensitive class structure

### Dynamic Invocation
- Method invocation via reflection bypasses type safety
- Validate all reflected calls
- Consider sandboxing for untrusted code

---

## Comparison with Other Languages

### vs. Java Reflection
- **Similar**: Type introspection, method invocation, field access
- **Different**: XXML adds ownership type tracking
- **Missing**: Annotations (planned), dynamic proxies

### vs. C# Reflection
- **Similar**: Type hierarchy, property/method info
- **Different**: No attributes yet, simpler API
- **Missing**: Emit (dynamic code generation)

### vs. Python `dir()`/`inspect`
- **Similar**: Runtime introspection
- **Different**: Statically compiled with metadata
- **Better**: Type safety, performance

---

## Development Roadmap

### Phase 1: Foundation ✅ **COMPLETE**
- ✅ Runtime structures
- ✅ XXML API classes
- ✅ Syscall implementations
- ✅ Test suite

### Phase 2: Metadata Generation ✅ **COMPLETE**
- ✅ LLVMBackend integration (LLVMBackend.cpp:3414-3664)
- ✅ Metadata collection during class visits (LLVMBackend.cpp:849-912)
- ✅ Automatic type registration via global constructors
- ✅ Complete reflection metadata emission

### Phase 3 (Current): Testing & Refinement 🚧
- Test reflection API with user programs
- Debug any edge cases
- Optimize type registry (hash map)
- Improve documentation

### Phase 4 (Next): Advanced Features
- Method invocation via reflection
- Property get/set via reflection
- Attribute/annotation system

### Phase 5 (Future): Ecosystem
- Serialization library using reflection
- Testing framework
- ORM framework
- Dependency injection

---

## Contributing

When adding reflection features:

1. Update C structures in `xxml_reflection_runtime.h`
2. Add syscalls to `xxml_llvm_runtime.h/.c`
3. Extend XXML classes in `Language/Reflection/`
4. Add tests in `tests/reflection_*.XXML`
5. Update this documentation

---

## License

Same as XXML Compiler project.

---

## Contact

For questions about the reflection system implementation, please open an issue on the XXML Compiler repository.
