# Standard Library Overview

The XXML Standard Library provides fundamental types, collections, and utilities for building applications.

## Module Structure

```
Language/
├── Core/           # Primitive types and core traits
├── Collections/    # Generic data structures
├── System/         # Console I/O
├── IO/             # File operations
├── Math/           # Mathematical functions
├── Text/           # String utilities, Regex
├── Time/           # Date/time handling
├── Format/         # JSON parsing
├── Network/        # HTTP client
├── Reflection/     # Runtime type introspection
└── Concurrent/     # Threading and synchronization
```

## Importing Modules

Use `#import` to include standard library modules:

```xxml
#import Language::Core;           // Integer, String, Bool, Float, Double
#import Language::Collections;    // List, HashMap, Set, Array, Stack, Queue
#import Language::System;         // Console
#import Language::IO;             // File
#import Language::Math;           // Math utilities
#import Language::Text;           // StringUtils, Pattern
#import Language::Time;           // DateTime, Timer
#import Language::Format;         // JSONObject, JSONArray
#import Language::Network;        // HTTPClient
#import Language::Reflection;     // Type introspection
#import Language::Concurrent;     // Threading
```

## Core Module

The `Language::Core` module is automatically available and provides:

| Type | Description |
|------|-------------|
| `Integer` | 64-bit signed integer |
| `String` | UTF-8 text string |
| `Bool` | Boolean value |
| `Float` | 32-bit floating point |
| `Double` | 64-bit floating point |
| `None` | Void/unit type |

## Collections Module

Generic data structures:

| Type | Description |
|------|-------------|
| `List<T>` | Dynamic resizable array |
| `HashMap<K,V>` | Hash table (K must be Hashable + Equatable) |
| `Set<T>` | Unique elements (T must be Hashable + Equatable) |
| `Array<T,N>` | Fixed-size array |
| `Stack<T>` | LIFO stack |
| `Queue<T>` | FIFO queue |

## Constraints

The standard library defines these constraints for generic types:

| Constraint | Required Methods |
|------------|-----------------|
| `Hashable<T>` | `hash(): NativeType<"int64">^` |
| `Equatable<T>` | `equals(other: T&): Bool^` |

## See Also

- [Core Types](CORE.md) - Integer, String, Bool, Float, Double
- [Collections](COLLECTIONS.md) - List, HashMap, Set, Array, Stack, Queue
- [Iterators](ITERATORS.md) - Iterator types and protocols
