# XXML Runtime Library

This directory contains the complete XXML standard library implementation.

The library is implemented in two layers:
1. **C++ Runtime** (`xxml_runtime.h`) - High-performance core implementations
2. **XXML Library Files** (`Language/`) - XXML interface definitions and wrappers

## C++ Runtime (`xxml_runtime.h`)

The C++ runtime provides optimized implementations for all standard library features:

### Core Types
- **String** - Full string manipulation with all operations
- **Integer** - 64-bit signed integers with arithmetic
- **Bool** - Boolean type with logical operations

### Collections (`Collections` namespace)
- **IntegerArrayData / StringArrayData** - Dynamic arrays with std::vector backing
- **IntegerHashMapData / StringHashMapData** - Hash maps with std::map
- **IntegerSetData / StringSetData** - Sets with std::set

### File I/O (`FileIO` namespace)
- **FileHandle** - File reading/writing with std::fstream
- **Directory** - Directory operations with std::filesystem

### Networking (`Network` namespace)
- **HTTPClient** - HTTP client (placeholder, requires libcurl for full implementation)
- **HTTPResponse** - HTTP response data structure

### Date/Time (`DateTime` namespace)
- **TimeStamp** - Timestamp with std::chrono

### Regular Expressions (`RegexLib` namespace)
- **Pattern** - Regex pattern matching with std::regex

### Threading (`Threading` namespace)
- **Mutex** - Mutual exclusion with std::mutex

### Environment (`Environment` namespace)
- **getEnv / setEnv** - Environment variables
- **sleep** - Thread sleep
- **exit** - Program exit

### Math (`Math` namespace)
- abs, min, max, clamp
- pow, sqrt
- random, randomRange
- gcd, lcm
- isPrime, factorial, fibonacci

### System (`System` namespace)
- Print, PrintLine
- ReadLine
- GetTime

## XXML Library Files (`Language/`)

### Core (`Language/Core/`)
- **String.XXML** - String class interface
- **Integer.XXML** - Integer class interface
- **Bool.XXML** - Boolean class interface

### Collections (`Language/Collections/`)
- **Array.XXML** - IntegerArray, StringArray classes
- **List.XXML** - IntegerList, StringList classes
- **HashMap.XXML** - IntegerHashMap, StringHashMap classes
- **Set.XXML** - IntegerSet, StringSet classes
- **Queue.XXML** - IntegerQueue, StringQueue classes
- **Stack.XXML** - IntegerStack, StringStack classes

### File I/O (`Language/IO/`)
- **File.XXML** - File reading/writing class
  - File operations: open, close, read, write
  - Static utilities: fileExists, deleteFile, copyFile, moveFile
- **Directory** - Directory operations
  - create, delete, exists, listFiles
  - getWorkingDirectory, setWorkingDirectory

### Format (`Language/Format/`)
- **JSON.XXML** - JSON parsing and generation
  - JSONObject - Object manipulation
  - JSONArray - Array manipulation

### Text (`Language/Text/`)
- **Regex.XXML** - Regular expression support
  - Pattern - Regex pattern matching
  - RegexUtil - Common validation utilities

### Network (`Language/Network/`)
- **HTTP.XXML** - HTTP client/server
  - HTTPClient - Web requests (GET, POST, PUT, DELETE)
  - HTTPResponse - Response data
  - HTTPServer - Server functionality (placeholder)

### Concurrent (`Language/Concurrent/`)
- **Threading.XXML** - Concurrency support
  - Thread - Thread management
  - Mutex - Mutual exclusion locks
  - Lock - RAII lock guards
  - Atomic - Atomic operations

### Time (`Language/Time/`)
- **DateTime.XXML** - Date and time manipulation
  - DateTime - Date/time with arithmetic
  - TimeSpan - Duration representation
  - Timer - Elapsed time measurement

### System (`Language/System/`)
- **Console.XXML** - Enhanced console I/O
  - Extended input/output operations
  - Environment variable access
  - Program control (exit, sleep)

### Math (`Language/Math/`)
- **Math.XXML** - Mathematical operations
  - Comprehensive math functions

## Usage

The standard library is automatically available to all XXML programs. The C++ runtime is included via:

```cpp
#include "runtime/xxml_runtime.h"
```

XXML programs can use standard library features directly:

```xxml
// String operations
Let <message> Types String^ Equals String::Constructor("Hello!");
System::PrintLine(message.toUpperCase());

// Math operations
Let <result> Types Integer^ Equals Math::factorial(Integer::Constructor(5));

// Collections (when fully connected)
Let <list> Types IntegerList^ Equals IntegerList::Constructor();
```

## Implementation Status

✅ **Fully Implemented (C++ Runtime)**:
- Core types (String, Integer, Bool)
- Math operations
- System I/O (Print, PrintLine, ReadLine, GetTime)
- File I/O primitives (FileHandle, Directory)
- Environment operations (getEnv, setEnv, sleep, exit)
- DateTime primitives (TimeStamp)
- Regex primitives (Pattern)
- Threading primitives (Mutex)
- Collection storage (Arrays, HashMaps, Sets)
- Network primitives (HTTPClient, HTTPResponse - placeholder)

✅ **Fully Defined (XXML Interfaces)**:
- All collection classes (Array, List, HashMap, Set, Queue, Stack)
- File and Directory classes
- JSON support (JSONObject, JSONArray)
- Regex support (Pattern, RegexUtil)
- HTTP support (HTTPClient, HTTPResponse, HTTPServer)
- Threading support (Thread, Mutex, Lock, Atomic)
- DateTime support (DateTime, TimeSpan, Timer)
- Console extensions

⚠️ **Requires Integration**:
- XXML classes need runtime binding to C++ implementations
- HTTP requires external library (libcurl) for full functionality
- Some advanced collection operations need additional C++ support

## Build Requirements

- C++17 or later (for std::filesystem, std::optional)
- Standard C++ libraries: `<string>`, `<iostream>`, `<fstream>`, `<vector>`, `<map>`, `<set>`, `<regex>`, `<thread>`, `<mutex>`, `<chrono>`, `<filesystem>`

## Architecture

The standard library follows a two-tier architecture:

1. **C++ Performance Layer**: Optimized implementations in `xxml_runtime.h` using STL containers and algorithms
2. **XXML Interface Layer**: Clean, type-safe XXML class definitions in `Language/` directory

This design provides:
- High performance through C++ implementations
- Clean APIs through XXML interfaces
- Type safety and consistency
- Self-hosting capability (XXML code can be compiled by XXML compiler)
