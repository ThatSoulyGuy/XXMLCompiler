# Destructors and RAII

XXML supports automatic resource cleanup through destructors with RAII (Resource Acquisition Is Initialization) semantics.

---

## Overview

Destructors are special methods called automatically when owned objects go out of scope. They enable safe, deterministic resource cleanup.

---

## Declaring Destructors

### Syntax

```xxml
Destructor Parameters () -> {
    // Cleanup code
}
```

**Important Notes:**
- The `Parameters()` must be empty - destructors cannot have parameters
- Return type is implicitly `void` (no return statement needed)
- Only one destructor per class

### Example

```xxml
[ Class <Resource> Final Extends None
    [ Private <>
        Property <handle> Types NativeType<"void*">^;
        Property <isValid> Types Bool^;
    ]

    [ Public <>
        Constructor Parameters (Parameter <size> Types Integer%) -> {
            Set handle = Syscall::malloc(size.toInt64());
            Set isValid = Bool::Constructor(true);
        }

        Destructor Parameters () -> {
            If (isValid) -> {
                Run Syscall::free(handle);
            }
        }
    ]
]
```

---

## When Destructors Are Called

### Scope Exit

When an owned (`^`) variable goes out of scope:

```xxml
[ Entrypoint
    {
        Instantiate Resource^ As <r> = Resource::Constructor(Integer::Constructor(1024));
        Run r.doSomething();
        // Destructor called automatically here
    }
]
```

### Block Exit

At the end of any block:

```xxml
If (condition) -> {
    Instantiate Resource^ As <temp> = Resource::Constructor(Integer::Constructor(100));
    Run temp.process();
    // temp's destructor called here
}
// temp no longer exists
```

### Loop Iterations

Variables declared in loops are destroyed each iteration:

```xxml
For (Integer <i> = 0 .. 10) -> {
    Instantiate Resource^ As <r> = Resource::Constructor(i);
    Run r.use();
    // r's destructor called at end of each iteration
}
```

### Return Statements

Before returning from a function:

```xxml
Method <processData> Returns Integer^ Parameters () Do {
    Instantiate Resource^ As <r> = Resource::Constructor(Integer::Constructor(500));
    Instantiate Integer^ As <result> = r.calculate();
    Return result;  // r's destructor called before return
}
```

---

## Ownership and Destruction

Only owned (`^`) objects have their destructors called automatically:

| Ownership | Destructor Called? |
|-----------|-------------------|
| Owned (`^`) | Yes, automatically |
| Reference (`&`) | No |
| Copy (`%`) | No |

### Example

```xxml
Method <useResource> Returns None Parameters (
    Parameter <owned> Types Resource^,    // Destructor WILL be called
    Parameter <ref> Types Resource&,      // Destructor NOT called
    Parameter <copy> Types Resource%      // Destructor NOT called
) Do {
    // Use resources...
}
// Only 'owned' is destroyed when method exits
```

---

## Destruction Order

### Properties

Class properties are destroyed in **reverse declaration order**:

```xxml
[ Class <Container> Final Extends None
    [ Public <>
        Property <first> Types Resource^;   // Destroyed last
        Property <second> Types Resource^;  // Destroyed second
        Property <third> Types Resource^;   // Destroyed first

        Destructor Parameters () -> {
            // Called before property destructors
            Run Console::printLine(String::Constructor("Container destroying"));
        }
    ]
]
```

### Nested Objects

When an object contains owned objects, destruction cascades:

```xxml
[ Class <Outer> Final Extends None
    [ Public <>
        Property <inner> Types Inner^;

        Destructor Parameters () -> {
            Run Console::printLine(String::Constructor("Outer destroying"));
            // inner's destructor called automatically after this
        }
    ]
]

[ Class <Inner> Final Extends None
    [ Public <>
        Destructor Parameters () -> {
            Run Console::printLine(String::Constructor("Inner destroying"));
        }
    ]
]

// Usage: Output will be:
// "Outer destroying"
// "Inner destroying"
```

---

## RAII Pattern

RAII ensures resources are released when objects go out of scope.

### File Handle Example

```xxml
[ Class <File> Final Extends None
    [ Private <>
        Property <handle> Types NativeType<"void*">^;
        Property <isOpen> Types Bool^;
    ]

    [ Public <>
        Constructor Parameters (Parameter <path> Types String%) -> {
            Set handle = NativeFile::open(path);
            Set isOpen = Bool::Constructor(true);
        }

        Destructor Parameters () -> {
            If (isOpen) -> {
                Run NativeFile::close(handle);
                Set isOpen = Bool::Constructor(false);
            }
        }

        Method <read> Returns String^ Parameters () Do {
            // Read from file...
        }
    ]
]

// Usage - file automatically closed
Method <processFile> Returns None Parameters (Parameter <path> Types String%) Do {
    Instantiate File^ As <f> = File::Constructor(path);
    Instantiate String^ As <content> = f.read();
    Run processContent(content);
    // f's destructor closes the file here
}
```

### Lock Guard Example

```xxml
[ Class <LockGuard> Final Extends None
    [ Private <>
        Property <mutex> Types Mutex&;
    ]

    [ Public <>
        Constructor Parameters (Parameter <m> Types Mutex&) -> {
            Set mutex = m;
            Run mutex.lock();
        }

        Destructor Parameters () -> {
            Run mutex.unlock();
        }
    ]
]

// Usage - mutex automatically unlocked
Method <criticalSection> Returns None Parameters (Parameter <mutex> Types Mutex&) Do {
    Instantiate LockGuard^ As <guard> = LockGuard::Constructor(mutex);
    // Protected code here...
    // guard's destructor unlocks mutex
}
```

---

## Best Practices

### Keep Destructors Simple

```xxml
// Good: Simple cleanup
Destructor Parameters () -> {
    Run Syscall::free(buffer);
}

// Good: Check validity first
Destructor Parameters () -> {
    If (isOpen) -> {
        Run close();
    }
}
```

### Release All Resources

```xxml
Destructor Parameters () -> {
    // Release in reverse acquisition order
    Run releaseConnections();
    Run closeFile();
    Run Syscall::free(buffer);
}
```

### Avoid Complex Logic

```xxml
// Avoid: Complex logic in destructor
Destructor Parameters () -> {
    // Don't do complex operations
    // Don't throw exceptions
    // Don't acquire new resources
    Run simpleCleanup();
}
```

### Never Call Manually

```xxml
// Wrong: Manual destructor call
// Run resource.Destructor();  // Never do this!

// Right: Let scope handle it
{
    Instantiate Resource^ As <r> = Resource::Constructor();
}  // Destructor called automatically
```

---

## Complete Example

```xxml
#import Language::Core;
#import Language::System;

[ Class <DatabaseConnection> Final Extends None
    [ Private <>
        Property <connectionHandle> Types NativeType<"void*">^;
        Property <isConnected> Types Bool^;
    ]

    [ Public <>
        Constructor Parameters (Parameter <connectionString> Types String%) -> {
            Set connectionHandle = NativeDB::connect(connectionString);
            Set isConnected = Bool::Constructor(true);
            Run Console::printLine(String::Constructor("Database connected"));
        }

        Destructor Parameters () -> {
            If (isConnected) -> {
                Run NativeDB::disconnect(connectionHandle);
                Run Console::printLine(String::Constructor("Database disconnected"));
            }
        }

        Method <query> Returns String^ Parameters (Parameter <sql> Types String%) Do {
            Return NativeDB::execute(connectionHandle, sql);
        }
    ]
]

[ Entrypoint
    {
        // Connection opened
        Instantiate DatabaseConnection^ As <db> = DatabaseConnection::Constructor(
            String::Constructor("localhost:5432")
        );

        // Use the connection
        Instantiate String^ As <result> = db.query(String::Constructor("SELECT * FROM users"));
        Run Console::printLine(result);

        // Connection automatically closed here
        Exit(0);
    }
]
```

Output:
```
Database connected
[query results]
Database disconnected
```

---

## Destructor Rules Summary

| Rule | Description |
|------|-------------|
| Syntax | `Destructor Parameters () -> { }` |
| Parameters | Must be empty |
| Return | Implicitly void |
| Called for | Owned (`^`) objects only |
| Timing | At scope exit |
| Order | Reverse of declaration |
| Manual calling | Never |

---

## See Also

- [Ownership](../language/OWNERSHIP.md) - Memory ownership semantics
- [Classes](../language/CLASSES.md) - Class declarations
- [Native Types](NATIVE_TYPES.md) - Low-level types and syscalls

