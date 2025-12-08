# XXML Roadmap

**Safe Reflective Metaprogramming & Ownership-Aware Introspection**

---

## Vision

XXML aims to be the first language that unifies:
1. **Explicit ownership semantics** for memory safety without garbage collection
2. **Rich runtime reflection** that respects ownership rules
3. **Compile-time metaprogramming** using reflection APIs (not macros or DSLs)

This roadmap outlines the path from current state to that vision.

---

## Phase 0: Groundwork & Codebase Triage ✅

**Goal**: Stabilize the foundation before building advanced features.

### Tasks

| Task | Description | Status |
|------|-------------|--------|
| Dead code cleanup | Remove unused passes and legacy code | Done |
| Documentation overhaul | Restructure docs into language/, stdlib/, advanced/, tools/ | Done |
| Test suite expansion | Add tests for templates, lambdas, ownership | Done |
| Build system modernization | CMake presets, CI/CD pipeline | Done |

### Deliverables
- Clean codebase with no dead code paths
- Comprehensive documentation structure
- Reliable test suite covering core features

---

## Phase 1: Runtime Reflection Completion ✅

**Goal**: Complete the `TypeInfo` and `FieldInfo` APIs for full runtime introspection.

### Current State
- Basic `TypeInfo` exists with name, size, field count
- `FieldInfo` provides field name, type name, offset
- Missing: method reflection, dynamic field access, type relationships

### Tasks

| Task | Description |
|------|-------------|
| Method reflection | `MethodInfo` with name, parameter types, return type |
| Dynamic field get/set | `fieldInfo.getValue(obj)`, `fieldInfo.setValue(obj, val)` |
| Type relationships | `typeInfo.getBaseType()`, `typeInfo.implementsConstraint()` |
| Constructor reflection | Enumerate and invoke constructors dynamically |

### API Design

```xxml
// Get type info at runtime
Instantiate TypeInfo^ As <info> = Reflection::getType<Person>();

// Iterate fields
For (Integer^ <i> = 0 .. info.getFieldCount()) -> {
    Instantiate FieldInfo^ As <field> = info.getField(i);
    Run Console::printLine(field.getName());
}

// Dynamic field access (ownership-aware)
Instantiate String& As <name> = field.getReference<String>(person);  // Borrow
Instantiate String^ As <nameCopy> = field.getCopy<String>(person);   // Copy
```

### Deliverables
- Complete `TypeInfo` API
- Complete `FieldInfo` API with ownership-aware access
- `MethodInfo` API (read-only, invocation in later phase)

---

## Phase 2: Compile-Time Reflection & Metaprogramming ✅

**Goal**: Enable code generation at compile-time using reflection APIs.

### Concept

```xxml
// Derive ToString at compile time
[ Derive <ToString> For <Person>
    Compiletime {
        Instantiate Compiletime TypeInfo^ As <info> = Reflection::getType<Person>();
        // Generate toString method based on field info
        For (Compiletime Integer^ <i> = 0 .. info.getFieldCount()) -> {
            Instantiate Compiletime FieldInfo^ As <field> = info.getField(i);
            // Emit code to append field.getName() + ": " + field.getValue().toString()
        }
    }
]
```

### Tasks

| Task | Description |
|------|-------------|
| Compile-time TypeInfo | `Reflection::getType<T>()` available at compile time |
| Code emission API | `Emit::method()`, `Emit::statement()`, `Emit::expression()` |
| Derive framework | `[ Derive <Trait> For <Type> ]` syntax |
| Built-in derives | `ToString`, `Eq`, `Hash`, `Clone`, `JSON` |

### Built-in Derives

| Derive | Generated Code |
|--------|----------------|
| `ToString` | `toString()` method iterating all fields |
| `Eq` | `equals(other)` comparing all fields |
| `Hash` | `hashCode()` combining field hashes |
| `Clone` | `clone()` deep-copying all fields |
| `JSON` | `toJson()` and `fromJson()` methods |

### Deliverables
- Compile-time reflection API
- Code emission primitives
- 5 built-in derive implementations

---

## Phase 3: Ownership Introspection & Diagnostics ✅

**Goal**: Make ownership visible and queryable at both compile-time and runtime.

### Tasks

| Task | Description |
|------|-------------|
| Ownership in TypeInfo | `fieldInfo.getOwnership()` returns `Owned`, `Reference`, `Copy` |
| Borrow checking visualization | Emit diagnostics showing borrow lifetimes |
| Move tracking | `Reflection::isMoved(var)` compile-time check |
| Lifetime annotations | Optional explicit lifetime parameters |

### API Design

```xxml
// Query ownership at compile time
Instantiate Compiletime FieldInfo^ As <field> = info.getField(0);
If (field.getOwnership().equals(Ownership::Owned)) -> {
    // Field owns its value
}

// Compile-time move check
Instantiate String^ As <s> = String::Constructor("hello");
Run consume(s);
// Reflection::isMoved(s) returns true here
```

### Deliverables
- Ownership reflection API
- Enhanced compiler diagnostics for ownership errors
- Move tracking utilities

---

## Phase 4: Concurrency & Send/Share Constraints ✅

**Goal**: Enforce thread-safety at compile time through type constraints.

### Concepts

| Constraint | Meaning |
|------------|---------|
| `Sendable` | Type can be moved between threads |
| `Sharable` | Type can be shared (referenced) across threads |

### Tasks

| Task | Description |
|------|-------------|
| Sendable constraint | Types with only `Sendable` fields are `Sendable` |
| Sharable constraint | Immutable types or those with synchronization |
| Thread spawn checking | `Thread::spawn` requires `Sendable` closure |
| Shared reference checking | Cross-thread references require `Sharable` |

### Example

```xxml
// Counter is Sharable because it uses atomic operations
[ Class <Counter> Final Extends None Implements Sharable
    [ Private <>
        Property <value> Types Atomic<Integer>^;
    ]
]

// Connection is Sendable (can be moved to another thread) but not Sharable
[ Class <Connection> Final Extends None Implements Sendable
    [ Private <>
        Property <socket> Types Socket^;
    ]
]

// Compile error: String& is not Sharable
Run Thread::spawn([ Lambda [&someString] ... ]);  // Error!
```

### Deliverables
- `Sendable` and `Sharable` constraints
- Automatic derivation based on field types
- Thread-safety enforcement in `Thread::spawn`

---

## Phase 5: Tooling & Language Server Integration ✅

**Goal**: Excellent developer experience through IDE integration.

### Tasks

| Task | Description |
|------|-------------|
| Language Server Protocol | LSP implementation for VS Code, etc. |
| Go to definition | Navigate to type/method definitions |
| Find references | Find all usages of a symbol |
| Hover information | Show type info, ownership, documentation |
| Inline diagnostics | Real-time error highlighting |
| Ownership visualization | Visual indication of borrows and moves |

### Deliverables
- LSP server binary
- VS Code extension
- Ownership visualization overlays

---

## Phase 6: Flagship Demos ✅

**Goal**: Demonstrate XXML's unique capabilities through compelling examples.

### Demo Projects

| Demo | Purpose |
|------|---------|
| JSON Library | Serialize any type using compile-time reflection |
| ORM | Database mapping with reflection-derived queries |
| RPC Framework | Generate client/server stubs from interface definitions |
| Test Framework | Auto-discover and run test methods via reflection |
| Plugin System | Load and introspect plugins at runtime |

### JSON Library Example

```xxml
// User defines a class
[ Class <User> Final Extends None
    [ Public <>
        Property <name> Types String^;
        Property <age> Types Integer^;
    ]
]

// Derive JSON serialization
[ Derive <JSON> For <User> ]

// Usage - no manual serialization code needed
Instantiate User^ As <user> = User::Constructor(String::Constructor("Alice"), Integer::Constructor(30));
Instantiate String^ As <json> = user.toJson();
// {"name": "Alice", "age": 30}

Instantiate User^ As <parsed> = User::fromJson(json);
```

### Deliverables
- 5 flagship demo projects
- Documentation and tutorials for each

---

## Phase 7: Cleanup, Stabilization, and Versioning ✅

**Goal**: Prepare for 1.0 release.

### Tasks

| Task | Description | Status |
|------|-------------|--------|
| API review | Finalize public APIs | Done |
| Breaking change cleanup | Remove deprecated features | Done |
| Performance optimization | Profile and optimize hot paths | Done |
| Documentation polish | Complete API reference | Done |
| Version 1.0 | Tag stable release | Done |

### Version Milestones

| Version | Content | Status |
|---------|---------|--------|
| 0.3.0 | Phase 0-1 complete (runtime reflection) | ✅ |
| 0.4.0 | Phase 2 complete (compile-time reflection) | ✅ |
| 0.5.0 | Phase 3 complete (ownership introspection) | ✅ |
| 0.6.0 | Phase 4 complete (concurrency constraints) | ✅ |
| 0.7.0 | Phase 5 complete (tooling) | ✅ |
| 0.9.0 | Phase 6 complete (demos) | ✅ |
| **1.0.0** | **Phase 7 complete (stable release)** | ✅ **Current** |

---

## Timeline Philosophy

This roadmap deliberately omits time estimates. Software development timelines are notoriously unreliable, and XXML is developed with a focus on correctness over speed.

Each phase will be considered complete when:
1. All tasks are implemented
2. Tests pass
3. Documentation is updated
4. At least one real-world use case validates the feature

---

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for how to contribute to XXML development.

Priority areas for contribution:
- Test case development
- Documentation improvements
- Standard library expansion
- Demo project implementations

---

## See Also

- [Philosophy](../README.md#philosophy) - Why XXML exists
- [Features](../README.md#features) - Current capabilities
- [Limitations](LIMITATIONS.md) - Known limitations
