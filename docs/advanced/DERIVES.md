# XXML Derive System

The derive system allows automatic generation of common methods (like `equals()`, `hash()`, and `toString()`) for classes based on their structure. This is similar to Rust's derive macros or Haskell's automatic deriving.

---

## Table of Contents

1. [Overview](#overview)
2. [Built-in Derives](#built-in-derives)
   - [Derive<Stringable>](#derivestringable)
   - [Derive<Equatable>](#deriveequatable)
   - [Derive<Hashable>](#derivehashable)
   - [Derive<Sendable>](#derivesendable)
   - [Derive<Sharable>](#derivesharable)
   - [Derive<JSON>](#derivejson)
3. [Using Derives](#using-derives)
4. [Writing Custom Derive Handlers](#writing-custom-derive-handlers)
   - [Handler Architecture](#handler-architecture)
   - [Creating a Handler](#creating-a-handler)
   - [AST Construction Helpers](#ast-construction-helpers)
   - [Registering Handlers](#registering-handlers)
5. [Examples](#examples)
6. [Limitations](#limitations)
7. [Implementation Details](#implementation-details)

---

## Overview

Derives provide automatic code generation for common boilerplate methods. Instead of manually implementing methods like `equals()` or `toString()` for every class, you can use the `@Derive` annotation to have the compiler generate them automatically based on the class's public properties.

### Basic Syntax

```xxml
@Derive(trait = "Stringable")
@Derive(trait = "Equatable")
@Derive(trait = "Hashable")
[ Class <Point> Final Extends None
    [ Public <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;

        Constructor = default;
    ]
]
```

The compiler will automatically generate:
- `toString()` - Returns `"Point{x=<value>, y=<value>}"`
- `equals(Point&)` - Compares all public properties
- `hash()` - Computes a hash code from all public properties

---

## Built-in Derives

### Derive<Stringable>

Generates a `toString()` method that returns a human-readable representation of the object.

**Generated Method Signature:**
```xxml
Method <toString> Returns String^ Parameters ()
```

**Output Format:**
```
ClassName{prop1=value1, prop2=value2, ...}
```

**Requirements:**
- All public properties must have a `toString()` method

**Example:**
```xxml
@Derive(trait = "Stringable")
[ Class <Person> Final Extends None
    [ Public <>
        Property <name> Types String^;
        Property <age> Types Integer^;
        Constructor = default;
    ]
]

// Generated toString() returns: "Person{name=John, age=30}"
```

### Derive<Equatable>

Generates an `equals()` method for structural equality comparison.

**Generated Method Signature:**
```xxml
Method <equals> Returns Bool^ Parameters (Parameter <other> Types ClassName&)
```

**Behavior:**
- Compares each public property using its `equals()` method
- Returns `Bool::Constructor(true)` if all properties match
- Returns `Bool::Constructor(false)` if any property differs
- Empty classes (no public properties) are always equal

**Requirements:**
- All public properties must have an `equals()` method that returns `Bool^`

**Example:**
```xxml
@Derive(trait = "Equatable")
[ Class <Point> Final Extends None
    [ Public <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;
        Constructor = default;
    ]
]

// Usage:
Instantiate Point^ As <p1> = Point::Constructor();
Instantiate Point^ As <p2> = Point::Constructor();
Instantiate Bool^ As <areEqual> = p1.equals(p2);
```

### Derive<Hashable>

Generates a `hash()` method for computing hash codes.

**Generated Method Signature:**
```xxml
Method <hash> Returns NativeType<"int64">^ Parameters ()
```

**Algorithm:**
```
hash = 17
for each property:
    hash = hash * 31 + property.hash()
```

**Requirements:**
- All public properties must have a `hash()` method that returns `NativeType<"int64">^`

**Example:**
```xxml
@Derive(trait = "Hashable")
[ Class <Point> Final Extends None
    [ Public <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;
        Constructor = default;
    ]
]

// Usage:
Instantiate Point^ As <p> = Point::Constructor();
Instantiate NativeType<"int64">^ As <hashCode> = p.hash();
```

### Derive<Sendable>

Marks a type as safe to **move** across thread boundaries. This is a **marker trait** that generates no methods but enables compile-time thread safety checking.

**Generated:** No methods (marker constraint)

**Behavior:**
- Validates that all public properties are of Sendable types
- Rejects types with reference (`&`) fields
- Allows types with owned (`^`) or copy (`%`) fields of Sendable types

**Requirements:**
- All owned fields must themselves be Sendable
- No reference fields allowed (references become dangling across threads)
- Primitives (`Integer`, `String`, `Bool`, etc.) are implicitly Sendable

**Example:**
```xxml
@Derive(trait = "Sendable")
[ Class <Message> Final Extends None
    [ Public <>
        Property <id> Types Integer^;
        Property <content> Types String^;
        Constructor = default;
    ]
]

// Message can now be safely moved to another thread
```

**Invalid Example:**
```xxml
// ERROR: Cannot derive Sendable - contains reference field
@Derive(trait = "Sendable")
[ Class <InvalidMessage> Final Extends None
    [ Public <>
        Property <ref> Types Integer&;  // Reference field blocks Sendable
        Constructor = default;
    ]
]
```

See [Threading](THREADING.md#sendable) for more details on the Sendable constraint.

### Derive<Sharable>

Marks a type as safe to **share** (reference) across threads simultaneously. This is a **marker trait** for immutable or synchronized types.

**Generated:** No methods (marker constraint)

**Behavior:**
- Validates that the type is suitable for shared access
- Typically used for immutable types (state set only during construction)
- Or types with synchronized mutable state

**Requirements:**
- Type should be immutable after construction
- OR mutable state must be protected by synchronization primitives

**Example:**
```xxml
@Derive(trait = "Sharable")
[ Class <Config> Final Extends None
    [ Public <>
        Property <maxConnections> Types Integer^;
        Property <timeout> Types Integer^;

        // Values set only during construction - immutable after
        Constructor Parameters (Parameter <max> Types Integer^, Parameter <t> Types Integer^) -> {
            Set maxConnections = max;
            Set timeout = t;
        }
    ]
]

// Config can be safely shared between threads
```

See [Threading](THREADING.md#sharable) for more details on the Sharable constraint.

### Derive<JSON>

Generates JSON serialization and deserialization methods for a class.

**Generated Method Signatures:**
```xxml
Method <toJSON> Returns JSONObject^ Parameters ()
Method <fromJSON> Returns ClassName^ Parameters (Parameter <json> Types JSONObject&)
```

**Behavior:**
- `toJSON()` creates a `JSONObject` with all public properties as key-value pairs
- `fromJSON()` constructs a new instance from a `JSONObject`
- Property names become JSON keys
- Nested objects with `@Derive(trait = "JSON")` are recursively serialized

**Requirements:**
- All public properties must be JSON-serializable types:
  - Primitives: `Integer`, `String`, `Bool`, `Float`, `Double`
  - Collections: `List<T>` where T is JSON-serializable
  - Objects: Classes with `@Derive(trait = "JSON")`

**Example:**
```xxml
#import Language::Core;
#import Language::Format;

@Derive(trait = "JSON")
[ Class <Person> Final Extends None
    [ Public <>
        Property <name> Types String^;
        Property <age> Types Integer^;
        Constructor = default;
    ]
]

// Serialize to JSON
Instantiate Person^ As <p> = Person::Constructor();
Set p.name = String::Constructor("Alice");
Set p.age = Integer::Constructor(30);

Instantiate JSONObject^ As <json> = p.toJSON();
// json = {"name": "Alice", "age": 30}

// Deserialize from JSON
Instantiate JSONObject^ As <input> = JSONObject::parse(String::Constructor("{\"name\":\"Bob\",\"age\":25}"));
Instantiate Person^ As <p2> = Person::fromJSON(input);
```

**Nested Objects:**
```xxml
@Derive(trait = "JSON")
[ Class <Address> Final Extends None
    [ Public <>
        Property <street> Types String^;
        Property <city> Types String^;
        Constructor = default;
    ]
]

@Derive(trait = "JSON")
[ Class <Employee> Final Extends None
    [ Public <>
        Property <name> Types String^;
        Property <address> Types Address^;  // Nested JSON-serializable
        Constructor = default;
    ]
]
// Produces: {"name": "...", "address": {"street": "...", "city": "..."}}
```

---

## Using Derives

### Multiple Derives

You can apply multiple derives to the same class:

```xxml
@Derive(trait = "Equatable")
@Derive(trait = "Hashable")
@Derive(trait = "Stringable")
[ Class <User> Final Extends None
    [ Public <>
        Property <id> Types Integer^;
        Property <name> Types String^;
        Constructor = default;
    ]
]
```

### Property Requirements

Derives only consider **public properties**. Private properties are ignored:

```xxml
@Derive(trait = "Equatable")
[ Class <Account> Final Extends None
    [ Private <>
        Property <internalId> Types Integer^;  // Ignored by derives
    ]

    [ Public <>
        Property <accountNumber> Types String^;  // Used by derives
        Constructor = default;
    ]
]
```

### Empty Classes

Classes with no public properties are handled gracefully:
- `ToString` generates `"ClassName{}"`
- `Eq` always returns `true` (nothing to compare)
- `Hash` returns the initial seed value (17)

---

## Writing Custom Derive Handlers

You can extend the derive system by creating custom derive handlers.

### Handler Architecture

Each derive handler extends the `DeriveHandler` base class:

```cpp
// include/Semantic/DeriveHandler.h

class DeriveHandler {
public:
    virtual ~DeriveHandler() = default;

    // Return the name of this derive (e.g., "ToString", "Eq")
    virtual std::string getDeriveName() const = 0;

    // Generate methods for the given class
    virtual DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) = 0;

    // Validate that derive can be applied (return empty string if OK)
    virtual std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer);

protected:
    // Helper methods for AST construction
    std::vector<Parser::PropertyDecl*> getPublicProperties(Parser::ClassDecl* classDecl);

    std::unique_ptr<Parser::TypeRef> makeType(
        const std::string& typeName,
        Parser::OwnershipType ownership);

    std::unique_ptr<Parser::IdentifierExpr> makeIdent(const std::string& name);

    std::unique_ptr<Parser::MemberAccessExpr> makeMemberAccess(
        std::unique_ptr<Parser::Expression> object,
        const std::string& member);

    std::unique_ptr<Parser::CallExpr> makeCall(
        std::unique_ptr<Parser::Expression> callee,
        std::vector<std::unique_ptr<Parser::Expression>> args);

    std::unique_ptr<Parser::CallExpr> makeStaticCall(
        const std::string& className,
        const std::string& methodName,
        std::vector<std::unique_ptr<Parser::Expression>> args);

    std::unique_ptr<Parser::ParameterDecl> makeParam(
        const std::string& name,
        const std::string& typeName,
        Parser::OwnershipType ownership);

    std::unique_ptr<Parser::ReturnStmt> makeReturn(
        std::unique_ptr<Parser::Expression> value);
};
```

### Creating a Handler

Here's a complete example of creating a custom derive handler:

```cpp
// include/Semantic/Derives/CloneDerive.h

#pragma once
#include "Semantic/DeriveHandler.h"

namespace XXML {
namespace Semantic {
namespace Derives {

class CloneDeriveHandler : public DeriveHandler {
public:
    std::string getDeriveName() const override { return "Clone"; }

    DeriveResult generate(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;

    std::string canDerive(
        Parser::ClassDecl* classDecl,
        SemanticAnalyzer& analyzer) override;
};

} // namespace Derives
} // namespace Semantic
} // namespace XXML
```

```cpp
// src/Semantic/Derives/CloneDerive.cpp

#include "Semantic/Derives/CloneDerive.h"
#include "Semantic/SemanticAnalyzer.h"

namespace XXML {
namespace Semantic {
namespace Derives {

DeriveResult CloneDeriveHandler::generate(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    DeriveResult result;

    // Get all public properties
    auto properties = getPublicProperties(classDecl);

    std::vector<std::unique_ptr<Parser::Statement>> bodyStmts;

    // Create new instance: Instantiate ClassName^ As <clone> = ClassName::Constructor();
    auto constructorCall = makeStaticCall(classDecl->name, "Constructor", {});
    auto cloneDecl = std::make_unique<Parser::InstantiateStmt>(
        makeType(classDecl->name, Parser::OwnershipType::Owned),
        "clone",
        std::move(constructorCall),
        Common::SourceLocation{}
    );
    bodyStmts.push_back(std::move(cloneDecl));

    // For each property, copy the value
    for (auto* prop : properties) {
        // Set clone.propName = this.propName;
        auto lhs = makeMemberAccess(makeIdent("clone"), prop->name);
        auto rhs = makeIdent(prop->name);

        auto assignStmt = std::make_unique<Parser::AssignmentStmt>(
            std::move(lhs),
            std::move(rhs),
            Common::SourceLocation{}
        );
        bodyStmts.push_back(std::move(assignStmt));
    }

    // Return clone;
    bodyStmts.push_back(makeReturn(makeIdent("clone")));

    // Create method: Method <clone> Returns ClassName^ Parameters ()
    std::vector<std::unique_ptr<Parser::ParameterDecl>> params;

    auto method = std::make_unique<Parser::MethodDecl>(
        "clone",
        makeType(classDecl->name, Parser::OwnershipType::Owned),
        std::move(params),
        std::move(bodyStmts),
        Common::SourceLocation{}
    );

    result.methods.push_back(std::move(method));
    return result;
}

std::string CloneDeriveHandler::canDerive(
    Parser::ClassDecl* classDecl,
    SemanticAnalyzer& analyzer) {

    // Check that the class has a default constructor
    bool hasDefaultConstructor = false;
    for (auto& section : classDecl->sections) {
        for (auto& member : section->members) {
            if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(member.get())) {
                if (ctor->isDefault) {
                    hasDefaultConstructor = true;
                    break;
                }
            }
        }
    }

    if (!hasDefaultConstructor) {
        return "Class must have a default constructor for Derive<Clone>";
    }

    return "";  // OK
}

} // namespace Derives
} // namespace Semantic
} // namespace XXML
```

### AST Construction Helpers

The `DeriveHandler` base class provides several helper methods for constructing AST nodes:

| Helper | Description |
|--------|-------------|
| `getPublicProperties()` | Returns all public properties of a class |
| `makeType(name, ownership)` | Creates a TypeRef node |
| `makeIdent(name)` | Creates an IdentifierExpr node |
| `makeMemberAccess(obj, member)` | Creates a MemberAccessExpr node |
| `makeCall(callee, args)` | Creates a CallExpr node |
| `makeStaticCall(class, method, args)` | Creates a static method call |
| `makeParam(name, type, ownership)` | Creates a ParameterDecl node |
| `makeReturn(value)` | Creates a ReturnStmt node |

### Registering Handlers

Register your custom handler in `SemanticAnalyzer::registerBuiltinDerives()`:

```cpp
// src/Semantic/SemanticAnalyzer.cpp

void SemanticAnalyzer::registerBuiltinDerives() {
    deriveRegistry_.registerHandler(
        std::make_unique<Derives::ToStringDeriveHandler>());
    deriveRegistry_.registerHandler(
        std::make_unique<Derives::EqDeriveHandler>());
    deriveRegistry_.registerHandler(
        std::make_unique<Derives::HashDeriveHandler>());

    // Add your custom handler
    deriveRegistry_.registerHandler(
        std::make_unique<Derives::CloneDeriveHandler>());
}
```

---

## Examples

### Complete Example: Data Class

```xxml
#import Language::Core;

@Derive(trait = "Equatable")
@Derive(trait = "Hashable")
@Derive(trait = "Stringable")
[ Class <Product> Final Extends None
    [ Public <>
        Property <id> Types Integer^;
        Property <name> Types String^;
        Property <price> Types Integer^;

        Constructor = default;

        Method <init> Returns Product^ Parameters (
            Parameter <pId> Types Integer^,
            Parameter <pName> Types String^,
            Parameter <pPrice> Types Integer^
        ) Do {
            Set id = pId;
            Set name = pName;
            Set price = pPrice;
            Return this;
        }
    ]
]

[ Entrypoint {
    // Create products
    Instantiate Product^ As <p1> = Product::Constructor();
    Run p1.init(Integer::Constructor(1), String::Constructor("Widget"), Integer::Constructor(100));

    Instantiate Product^ As <p2> = Product::Constructor();
    Run p2.init(Integer::Constructor(1), String::Constructor("Widget"), Integer::Constructor(100));

    // Test equality
    If (p1.equals(p2)) -> {
        Run System::Console::printLine(String::Constructor("Products are equal"));
    }

    // Test hash
    If (p1.hash() == p2.hash()) -> {
        Run System::Console::printLine(String::Constructor("Hashes match"));
    }

    // Test toString
    Run System::Console::printLine(p1.toString());
    // Output: Product{id=1, name=Widget, price=100}

    Exit(0);
}]
```

### Example: Value Object Pattern

```xxml
#import Language::Core;

// Money value object - immutable with structural equality
@Derive(trait = "Equatable")
@Derive(trait = "Hashable")
@Derive(trait = "Stringable")
[ Class <Money> Final Extends None
    [ Public <>
        Property <amount> Types Integer^;
        Property <currency> Types String^;

        Constructor = default;

        Method <of> Returns Money^ Parameters (
            Parameter <amt> Types Integer^,
            Parameter <curr> Types String^
        ) Do {
            Set amount = amt;
            Set currency = curr;
            Return this;
        }

        Method <add> Returns Money^ Parameters (Parameter <other> Types Money&) Do {
            Instantiate Money^ As <result> = Money::Constructor();
            Run result.of(amount.add(other.amount), currency);
            Return result;
        }
    ]
]
```

---

## Limitations

### Current Limitations

1. **Template Classes**: Derives on template/generic classes are not yet fully supported. Use derives on non-template classes.

2. **Chained Method Calls**: Generated code uses intermediate variables to avoid type inference issues with chained method calls.

3. **Private Properties**: Only public properties are considered by derives. Private properties are ignored.

4. **Inheritance**: Derives don't automatically consider inherited properties. Only properties directly defined in the class are used.

5. **Custom Types**: Properties must have the required methods (`equals`, `hash`, `toString`) already defined. The derive system validates this at compile time.

### Supported Property Types

| Type | ToString | Eq | Hash |
|------|----------|-----|------|
| Integer | Yes | Yes | Yes |
| String | Yes | Yes | Yes |
| Bool | Yes | Yes | No |
| Float | Yes | Yes | No |
| Double | Yes | Yes | No |
| Char | Yes | Yes | No |
| Custom classes | If has toString() | If has equals() | If has hash() |

---

## Implementation Details

### Files

| File | Purpose |
|------|---------|
| `include/Semantic/DeriveHandler.h` | Base class and registry |
| `src/Semantic/DeriveHandler.cpp` | Helper implementations |
| `include/Semantic/Derives/ToStringDerive.h` | ToString handler header |
| `src/Semantic/Derives/ToStringDerive.cpp` | ToString implementation |
| `include/Semantic/Derives/EqDerive.h` | Eq handler header |
| `src/Semantic/Derives/EqDerive.cpp` | Eq implementation |
| `include/Semantic/Derives/HashDerive.h` | Hash handler header |
| `src/Semantic/Derives/HashDerive.cpp` | Hash implementation |

### Processing Pipeline

```
Source with @Derive annotations
          |
          v
    [Parser] - Parse @Derive annotations
          |
          v
[Semantic Analysis Phase 1] - Register types
          |
          v
[Derive Processing] - For each @Derive:
    1. Look up handler by trait name
    2. Call canDerive() to validate
    3. Call generate() to create methods
    4. Insert methods into class AST
          |
          v
[Semantic Analysis Phase 2] - Analyze generated methods
          |
          v
    [Codegen] - Generate LLVM IR including derived methods
```

### Implementation Status

| Feature | Status |
|---------|--------|
| @Derive annotation parsing | Yes |
| DeriveHandler base class | Yes |
| DeriveRegistry | Yes |
| Derive<Stringable> | Yes |
| Derive<Equatable> | Yes |
| Derive<Hashable> | Yes |
| Derive<Sendable> | Yes |
| Derive<Sharable> | Yes |
| Derive<JSON> | Yes |
| Validation (canDerive) | Yes |
| Error messages for invalid derives | Yes |
| Template class support | Partial |
| Multiple derives on same class | Yes |

---

## See Also

- [Annotations](ANNOTATIONS.md) - Annotation system documentation
- [Reflection](REFLECTION.md) - Runtime type introspection
- [Language Specification](../language/LANGUAGE_SPEC.md) - Complete language syntax

---

**XXML Derive System v2.0**

*Last updated: Added Sendable, Sharable, and JSON derives for XXML 3.0.0*
