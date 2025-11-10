# Pure XXML Standard Library Architecture

## Overview

The XXML standard library is implemented entirely in XXML, with only primitive operations backed by system calls. This document explains the architecture, design patterns, and implementation approach.

## Core Principles

1. **No C++ Collection Backing**: All data structures are implemented in XXML code
2. **Syscall-Based**: Low-level operations use system calls for file I/O, memory, networking
3. **Manual Memory Management**: XXML code manages memory allocation and deallocation
4. **Self-Hosting**: Standard library is written in the language it serves

## Architecture Layers

```
┌─────────────────────────────────────────────┐
│   User XXML Code                            │
│   (Uses standard library)                   │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│   Pure XXML Standard Library                │
│   - Collections (List, Map, Set, etc.)      │
│   - File I/O, JSON, Regex, HTTP, etc.       │
│   - All algorithms in XXML                  │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│   Language Primitives                       │
│   - String, Integer, Bool (thin wrappers)   │
│   - Math operations                         │
│   - System::Print, PrintLine               │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│   Syscall Interface                         │
│   - Memory: malloc, free, memcpy           │
│   - File I/O: fopen, fread, fwrite         │
│   - System: getenv, time, sleep             │
│   - Network: socket, connect, send          │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│   Operating System                          │
└─────────────────────────────────────────────┘
```

## Syscall System

The Syscall namespace in `runtime/xxml_runtime.h` provides low-level operations:

### Memory Operations
```cpp
Syscall::malloc(size)      // Allocate memory
Syscall::free(ptr)          // Free memory
Syscall::realloc(ptr, size) // Resize allocation
Syscall::memcpy(dest, src, size)  // Copy memory
Syscall::memset(dest, val, size)  // Fill memory
Syscall::read_int64(ptr)    // Read 64-bit integer
Syscall::write_int64(ptr, val)    // Write 64-bit integer
```

### File I/O Operations
```cpp
Syscall::fopen(filename, mode)     // Open file
Syscall::fclose(file)               // Close file
Syscall::fread(buf, size, count, file)   // Read bytes
Syscall::fwrite(buf, size, count, file)  // Write bytes
Syscall::fseek(file, offset, origin)     // Seek position
Syscall::ftell(file)                // Get position
Syscall::feof(file)                 // Check end-of-file
```

### System Operations
```cpp
Syscall::getenv(name)       // Get environment variable
Syscall::setenv_impl(name, val)  // Set environment variable
Syscall::time_now()         // Get current time
Syscall::sleep_ms(ms)       // Sleep for milliseconds
Syscall::exit_program(code) // Exit program
```

### Network Operations
```cpp
Syscall::socket_create(domain, type, protocol)  // Create socket
Syscall::socket_connect(sock, addr, len)        // Connect
Syscall::socket_bind(sock, addr, len)           // Bind
Syscall::socket_listen(sock, backlog)           // Listen
Syscall::socket_accept(sock, addr, len)         // Accept
Syscall::socket_send(sock, buf, len, flags)     // Send data
Syscall::socket_recv(sock, buf, len, flags)     // Receive data
Syscall::socket_close(sock)                     // Close socket
```

## NativeType System (Conceptual)

For primitive storage in data structures:

```xxml
// Conceptual syntax - awaiting parser implementation
Property <dataPtr> Types NativeType<"ptr">;    // void* pointer
Property <count> Types NativeType<"int64">;    // 64-bit integer
Property <flags> Types NativeType<"bool">;     // Boolean flag
```

**Purpose**: Store raw primitive values without overhead of wrapper classes.

## Implementation Patterns

### Pattern 1: Dynamic Array (List)

**Algorithm**: Dynamically growing array with doubling strategy

```xxml
[ Class <IntegerList>
    [ Private <>
        Property <dataPtr> Types NativeType<"ptr">;  // malloc'd array
        Property <capacity> Types NativeType<"int64">;
        Property <count> Types NativeType<"int64">;

        Method <grow> Returns None Parameters () Do {
            // Double capacity
            Let <newCap> Types Integer^ Equals capacity * 2;
            Let <newPtr> Types NativeType<"ptr"> Equals Syscall::malloc(newCap * 8);

            // Copy old data
            Run Syscall::memcpy(newPtr, dataPtr, count * 8);
            Run Syscall::free(dataPtr);

            dataPtr = newPtr;
            capacity = newCap;
        }
    ]

    [ Public <>
        Method <add> Returns None Parameters (Parameter <value> Types Integer^) Do {
            If (count >= capacity) {
                Run grow();
            }

            // Write value to array
            Let <offset> Types NativeType<"ptr"> Equals dataPtr + (count * 8);
            Run Syscall::write_int64(offset, value);
            count = count + 1;
        }

        Method <get> Returns Integer^ Parameters (Parameter <index> Types Integer^) Do {
            Let <offset> Types NativeType<"ptr"> Equals dataPtr + (index * 8);
            Let <value> Types Integer^ Equals Syscall::read_int64(offset);
            Return value;
        }
    ]
]
```

**Key Points**:
- Manual memory management with malloc/free
- Pointer arithmetic for array indexing
- Doubling strategy for amortized O(1) append

### Pattern 2: Hash Map

**Algorithm**: Separate chaining with linked lists

```xxml
[ Class <IntegerHashMap>
    [ Private <>
        Property <buckets> Types NativeType<"ptr">;  // Array of bucket pointers
        Property <numBuckets> Types NativeType<"int64">;
        Property <count> Types NativeType<"int64">;

        // Each bucket is a linked list node
        // struct Node { int64_t key; int64_t value; Node* next; }

        Method <hash> Returns Integer^ Parameters (Parameter <key> Types Integer^) Do {
            // Simple hash function
            Return key.modulo(numBuckets);
        }

        Method <findNode> Returns NativeType<"ptr"> Parameters (
            Parameter <key> Types Integer^
        ) Do {
            Let <bucket> Types Integer^ Equals hash(&key);
            Let <node> Types NativeType<"ptr"> Equals getBucketNode(bucket);

            // Traverse linked list
            While (node != 0) {
                Let <nodeKey> Types Integer^ Equals Syscall::read_int64(node);
                If (nodeKey == key) {
                    Return node;
                }
                // node = node->next (offset +16 for next pointer)
                node = Syscall::read_int64(node + 16);
            }

            Return 0;  // Not found
        }
    ]

    [ Public <>
        Method <put> Returns None Parameters (
            Parameter <key> Types Integer^,
            Parameter <value> Types Integer^
        ) Do {
            Let <node> Types NativeType<"ptr"> Equals findNode(&key);

            If (node != 0) {
                // Update existing
                Run Syscall::write_int64(node + 8, value);  // value at offset +8
            } Else {
                // Create new node
                Let <newNode> Types NativeType<"ptr"> Equals Syscall::malloc(24);  // key+value+next
                Run Syscall::write_int64(newNode, key);      // key at offset 0
                Run Syscall::write_int64(newNode + 8, value);  // value at offset 8

                // Insert at bucket head
                Let <bucket> Types Integer^ Equals hash(&key);
                Let <oldHead> Types NativeType<"ptr"> Equals getBucketNode(bucket);
                Run Syscall::write_int64(newNode + 16, oldHead);  // next pointer
                Run setBucketNode(bucket, newNode);

                count = count + 1;
            }
        }

        Method <get> Returns Integer^ Parameters (Parameter <key> Types Integer^) Do {
            Let <node> Types NativeType<"ptr"> Equals findNode(&key);
            If (node != 0) {
                Return Syscall::read_int64(node + 8);  // Read value
            }
            Return Integer::Constructor(0);
        }
    ]
]
```

**Key Points**:
- Hash function distributes keys across buckets
- Linked list for collision resolution
- Manual node allocation and pointer management

### Pattern 3: File I/O

**Algorithm**: Buffered file operations using syscalls

```xxml
[ Class <File>
    [ Private <>
        Property <fileHandle> Types NativeType<"ptr">;  // FILE*
        Property <isOpen> Types Bool^;
    ]

    [ Public <>
        Method <open> Returns Bool^ Parameters (
            Parameter <path> Types String^,
            Parameter <mode> Types String^
        ) Do {
            Let <pathCStr> Types NativeType<"ptr"> Equals path.toCString();
            Let <modeCStr> Types NativeType<"ptr"> Equals mode.toCString();

            fileHandle = Syscall::fopen(pathCStr, modeCStr);

            If (fileHandle != 0) {
                isOpen = Bool::Constructor(true);
                Return Bool::Constructor(true);
            }

            Return Bool::Constructor(false);
        }

        Method <readAll> Returns String^ Parameters () Do {
            // Get file size
            Run Syscall::fseek(fileHandle, 0, 2);  // SEEK_END
            Let <size> Types Integer^ Equals Syscall::ftell(fileHandle);
            Run Syscall::fseek(fileHandle, 0, 0);  // SEEK_SET

            // Allocate buffer
            Let <buffer> Types NativeType<"ptr"> Equals Syscall::malloc(size + 1);

            // Read file
            Run Syscall::fread(buffer, 1, size, fileHandle);

            // Null-terminate
            Run Syscall::write_int64(buffer + size, 0);

            // Create string
            Let <result> Types String^ Equals String::FromCString(buffer);
            Run Syscall::free(buffer);

            Return result;
        }
    ]
]
```

**Key Points**:
- Direct use of C file API
- Manual buffer management
- String conversion helpers needed

## Required Language Features

To fully implement this architecture, XXML needs:

### 1. NativeType Support
```xxml
Property <ptr> Types NativeType<"ptr">;
Property <count> Types NativeType<"int64">;
Property <flag> Types NativeType<"bool">;
```

### 2. Syscall Syntax
```xxml
Let <ptr> Types NativeType<"ptr"> Equals Syscall::malloc(100);
Run Syscall::free(ptr);
```

### 3. Conditional Statements
```xxml
If (condition) {
    // then branch
} Else {
    // else branch
}
```

### 4. While Loops
```xxml
While (condition) {
    // loop body
}
```

### 5. Break/Continue
```xxml
While (true) {
    If (condition) {
        Break;
    }
}
```

### 6. Comparison Operators Returning Bool
```xxml
If (x == y) { }
If (ptr != 0) { }
If (count < capacity) { }
```

### 7. Pointer Arithmetic
```xxml
Let <offset> Types NativeType<"ptr"> Equals ptr + (index * 8);
```

### 8. Destructor Support
```xxml
Destructor() {
    Run Syscall::free(dataPtr);
}
```

### 9. String Conversion Helpers
```xxml
Let <cstr> Types NativeType<"ptr"> Equals str.toCString();
Let <str> Types String^ Equals String::FromCString(cstr);
```

## Example Implementations

See these files for complete examples:

- **`Language/Collections/List_PureXXML.XXML`** - Dynamic array implementation
- **`Language/IO/File_PureXXML.XXML`** - File I/O implementation
- **`Examples/SyscallExample.XXML`** - Syscall usage examples

## Performance Characteristics

### Collections
- **List**:
  - add(): O(1) amortized
  - get/set(): O(1)
  - insert/remove(): O(n)
- **HashMap**:
  - put/get(): O(1) average
  - Worst case: O(n) with poor hash
- **Set**:
  - Similar to HashMap
- **Stack/Queue**:
  - push/pop/enqueue/dequeue(): O(1)

### Memory
- Manual management allows fine-grained control
- No garbage collection overhead
- Potential for memory leaks if not careful
- RAII patterns with destructors help prevent leaks

## Benefits

1. **Full Control**: Complete control over memory layout and allocation
2. **No Hidden Costs**: All operations are explicit in XXML code
3. **Portable**: Only relies on standard C library syscalls
4. **Educ

ational**: Clear understanding of how data structures work
5. **Self-Hosting**: Demonstrates language maturity
6. **Extensible**: Easy to add new collection types

## Current Status

### Implemented ✅
- Syscall infrastructure in C++ runtime
- Memory operations (malloc, free, memcpy, etc.)
- File I/O operations (fopen, fread, fwrite, etc.)
- System operations (getenv, time, sleep, etc.)
- Network operations (socket API)
- Example implementations showing design patterns
- Documentation of architecture

### In Progress 🚧
- Parser support for NativeType and Syscall syntax
- Code generator integration
- Complete XXML implementations of all collections

### Planned 📋
- Conditional statements (If/Else)
- While loops
- Break/Continue statements
- Destructor support
- String conversion helpers (toCString, FromCString)
- Comparison operators returning Bool

## Migration Path

To fully enable pure XXML implementations:

1. **Phase 1**: Add conditional statements to parser
2. **Phase 2**: Add NativeType support
3. **Phase 3**: Integrate Syscall into code generator
4. **Phase 4**: Implement String helpers
5. **Phase 5**: Complete all collection implementations
6. **Phase 6**: Comprehensive testing

## Conclusion

The pure XXML standard library architecture provides a foundation for implementing all data structures and algorithms in XXML itself, using only system calls for primitive operations. This approach ensures the language is self-hosting and gives complete transparency and control to developers.
