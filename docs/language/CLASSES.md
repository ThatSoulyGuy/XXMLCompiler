# XXML Classes

Classes are the primary building block for object-oriented programming in XXML. Classes are heap-allocated reference types with support for inheritance, access modifiers, and encapsulation.

---

## Class Declaration

```xxml
[ Class <ClassName> Final Extends BaseClass
    [ Public <>
        // Public members
    ]
    [ Private <>
        // Private members
    ]
    [ Protected <>
        // Protected members
    ]
]
```

### Modifiers

| Modifier | Description |
|----------|-------------|
| `Final` | Class cannot be inherited from |
| `Extends BaseClass` | Inherits from BaseClass |
| `Extends None` | No inheritance (default) |

---

## Access Modifiers

### Public

Public members are accessible from anywhere.

```xxml
[ Public <>
    Property <name> Types String^;
    Method <getName> Returns String^ Parameters () Do {
        Return name;
    }
]
```

### Private

Private members are only accessible within the class.

```xxml
[ Private <>
    Property <internalData> Types Integer^;
]
```

### Protected

Protected members are accessible within the class and its subclasses.

```xxml
[ Protected <>
    Property <sharedData> Types String^;
]
```

---

## Properties

Properties are class fields that store data.

```xxml
Property <propertyName> Types TypeName^;

// Examples
Property <name> Types String^;
Property <age> Types Integer^;
Property <active> Types Bool^;
```

Properties typically use owned (`^`) types since the class owns its data.

---

## Constructors

### Default Constructor

The default constructor initializes public properties from positional arguments.

```xxml
Constructor = default;
```

With default constructor, instantiation passes values for each public property:

```xxml
[ Class <Person> Final Extends None
    [ Public <>
        Property <name> Types String^;
        Property <age> Types Integer^;
        Constructor = default;
    ]
]

// Usage: arguments match property order
Instantiate Person^ As <p> = Person::Constructor(
    String::Constructor("Alice"),
    Integer::Constructor(30)
);
```

### Custom Constructor

```xxml
Constructor Parameters (
    Parameter <value> Types Integer%
) -> {
    // Constructor body
    Set propertyName = value;
}
```

### Example

```xxml
[ Class <Counter> Final Extends None
    [ Public <>
        Property <count> Types Integer^;

        Constructor Parameters (
            Parameter <initial> Types Integer%
        ) -> {
            Set count = initial;
        }
    ]
]

// Usage
Instantiate Counter^ As <c> = Counter::Constructor(Integer::Constructor(0));
```

---

## Methods

### Instance Methods

```xxml
Method <methodName> Returns ReturnType Parameters (ParameterList) Do {
    // Method body
}
```

### Method Syntax Options

Methods can use either `Do` or `->` before the body:

```xxml
Method <add> Returns Integer^ Parameters (
    Parameter <a> Types Integer%,
    Parameter <b> Types Integer%
) Do {
    Return a.add(b);
}

// Alternative syntax
Method <subtract> Returns Integer^ Parameters (
    Parameter <a> Types Integer%,
    Parameter <b> Types Integer%
) -> {
    Return a.subtract(b);
}
```

### Accessing Properties

Within methods, properties are accessed directly by name:

```xxml
Method <getCount> Returns Integer^ Parameters () Do {
    Return count;  // Direct property access
}

Method <increment> Returns None Parameters () Do {
    Set count = count.add(Integer::Constructor(1));
}
```

### Static Methods

Static methods are typically declared in classes with no instance state and called with `Class::MethodName`:

```xxml
[ Class <Math> Final Extends None
    [ Public <>
        Method <abs> Returns Integer^ Parameters (
            Parameter <value> Types Integer%
        ) Do {
            // Implementation
        }
    ]
]

// Usage
Instantiate Integer^ As <result> = Math::abs(Integer::Constructor(-5));
```

---

## Inheritance

Classes can inherit from other classes using `Extends`:

```xxml
[ Class <Animal> Extends None
    [ Public <>
        Property <name> Types String^;
        Constructor = default;

        Method <speak> Returns None Parameters () Do {
            Run Console::printLine(String::Constructor("..."));
        }
    ]
]

[ Class <Dog> Final Extends Animal
    [ Public <>
        Constructor = default;

        Method <speak> Returns None Parameters () Do {
            Run Console::printLine(String::Constructor("Woof!"));
        }
    ]
]
```

### Final Classes

A class marked `Final` cannot be extended:

```xxml
[ Class <Singleton> Final Extends None
    // Cannot inherit from this class
]
```

---

## Complete Example

```xxml
#import Language::Core;
#import Language::System;

[ Namespace <MyApp>

    [ Class <BankAccount> Final Extends None
        [ Private <>
            Property <balance> Types Integer^;
        ]

        [ Public <>
            Property <accountNumber> Types String^;

            Constructor Parameters (
                Parameter <number> Types String%,
                Parameter <initialBalance> Types Integer%
            ) -> {
                Set accountNumber = number;
                Set balance = initialBalance;
            }

            Method <getBalance> Returns Integer^ Parameters () Do {
                Return balance;
            }

            Method <deposit> Returns None Parameters (
                Parameter <amount> Types Integer%
            ) Do {
                Set balance = balance.add(amount);
            }

            Method <withdraw> Returns Bool^ Parameters (
                Parameter <amount> Types Integer%
            ) Do {
                If (balance.greaterThan(amount).or(balance.equals(amount))) -> {
                    Set balance = balance.subtract(amount);
                    Return Bool::Constructor(true);
                }
                Return Bool::Constructor(false);
            }
        ]
    ]
]

[ Entrypoint
    {
        Instantiate MyApp::BankAccount^ As <account> = MyApp::BankAccount::Constructor(
            String::Constructor("12345"),
            Integer::Constructor(1000)
        );

        Run Console::printLine(String::Constructor("Initial balance: ").append(account.getBalance().toString()));

        Run account.deposit(Integer::Constructor(500));
        Run Console::printLine(String::Constructor("After deposit: ").append(account.getBalance().toString()));

        Instantiate Bool^ As <success> = account.withdraw(Integer::Constructor(200));
        If (success) -> {
            Run Console::printLine(String::Constructor("Withdrawal successful"));
        }

        Run Console::printLine(String::Constructor("Final balance: ").append(account.getBalance().toString()));

        Exit(0);
    }
]
```

---

## Generic Classes (Templates)

Classes can be parameterized with templates. See [Templates](TEMPLATES.md) for full documentation.

```xxml
[ Class <Container> Final Extends None Templates (T <Element>)
    [ Public <>
        Property <value> Types Element^;
        Constructor = default;

        Method <get> Returns Element^ Parameters () Do {
            Return value;
        }
    ]
]

// Usage with specific type
Instantiate Container@Integer^ As <intContainer> = Container@Integer::Constructor(
    Integer::Constructor(42)
);
```

---

## Destructors

Classes can have destructors for cleanup. See [Advanced Features](../advanced/DESTRUCTORS.md).

```xxml
Destructor Parameters () -> {
    // Cleanup code
}
```

---

## Key Differences from Structures

| Feature | Class | Structure |
|---------|-------|-----------|
| Allocation | Heap | Stack |
| Inheritance | Supported | Not supported |
| Reference semantics | Yes | No (value type) |
| Properties | Store pointers | Store values directly |

---

## See Also

- [Structures](STRUCTURES.md) - Stack-allocated value types
- [Templates](TEMPLATES.md) - Generic programming
- [Ownership](OWNERSHIP.md) - Memory ownership semantics
- [Destructors](../advanced/DESTRUCTORS.md) - RAII and cleanup

