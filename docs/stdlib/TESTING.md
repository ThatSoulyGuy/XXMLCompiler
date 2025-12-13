# XXML Test Framework

The XXML Test Framework provides reflection-based test discovery and execution with assertion utilities.

**Location:** `Language/Test/TestFramework.XXML`

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Test Discovery](#test-discovery)
3. [Assert Class](#assert-class)
4. [TestResult Class](#testresult-class)
5. [TestSummary Class](#testsummary-class)
6. [TestRunner Class](#testrunner-class)
7. [Writing Tests](#writing-tests)
8. [Running Tests](#running-tests)

---

## Quick Start

```xxml
#import Language::Core;
#import Language::Test;

// Mark class as a test suite
@Test
[ Class <MathTests> Final Extends None
    [ Public <>
        // Test methods start with "test"
        Method <testAddition> Returns None Parameters () Do {
            Instantiate Integer^ As <result> = Integer::Constructor(2).add(Integer::Constructor(2));
            Run Assert::equals(
                Integer::Constructor(4),
                result,
                String::Constructor("2+2 should equal 4")
            );
        }

        Method <testSubtraction> Returns None Parameters () Do {
            Instantiate Integer^ As <result> = Integer::Constructor(5).subtract(Integer::Constructor(3));
            Run Assert::equals(
                Integer::Constructor(2),
                result,
                String::Constructor("5-3 should equal 2")
            );
        }
    ]
]

[ Entrypoint {
    Instantiate TestRunner^ As <runner> = TestRunner::Constructor();
    Instantiate Integer^ As <exitCode> = runner.run(String::Constructor("MathTests"));
    Exit(exitCode.toInt64());
}]
```

---

## Test Discovery

The Test Framework uses reflection to automatically discover test methods. Any method whose name starts with `test` is considered a test method.

### @Test Annotation

Use the `@Test` annotation to mark classes as test suites:

```xxml
@Test
[ Class <MyTestSuite> Final Extends None
    [ Public <>
        Method <testFeatureA> Returns None Parameters () Do { ... }
        Method <testFeatureB> Returns None Parameters () Do { ... }
        Method <helperMethod> Returns None Parameters () Do { ... }  // Not a test
    ]
]
```

### Discovery Process

1. `TestRunner.runTestClass()` receives a type name
2. Uses `Type::forName()` to get reflection info
3. Iterates through all methods via `type.getMethodCount()` and `type.getMethodAt()`
4. Checks if method name starts with "test" using `String.startsWith()`
5. Executes matching methods and collects results

---

## Assert Class

The `Assert` class provides static assertion methods for verifying test conditions.

### Methods

| Method | Parameters | Description |
|--------|------------|-------------|
| `isTrue` | `(condition: Bool&, message: String&)` | Assert condition is true |
| `isFalse` | `(condition: Bool&, message: String&)` | Assert condition is false |
| `equals` | `(expected: Integer&, actual: Integer&, message: String&)` | Assert integers are equal |
| `equalsString` | `(expected: String&, actual: String&, message: String&)` | Assert strings are equal |
| `notNull` | `(message: String&)` | Reserved for future nullable types |
| `fail` | `(message: String&)` | Explicitly fail the test |

### Usage Examples

```xxml
// Boolean assertions
Run Assert::isTrue(result.greaterThan(Integer::Constructor(0)), String::Constructor("Result should be positive"));
Run Assert::isFalse(list.isEmpty(), String::Constructor("List should not be empty"));

// Equality assertions
Run Assert::equals(
    Integer::Constructor(42),
    calculator.compute(),
    String::Constructor("Computation result")
);

Run Assert::equalsString(
    String::Constructor("hello"),
    greeting.toLowerCase(),
    String::Constructor("Greeting should be lowercase")
);

// Explicit failure
If (someCondition) -> {
    Run Assert::fail(String::Constructor("This condition should not occur"));
}
```

---

## TestResult Class

Represents the result of a single test execution.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `testName` | `String^` | Name of the test method |
| `passed` | `Bool^` | Whether the test passed |
| `message` | `String^` | Success/failure message |
| `durationMs` | `Integer^` | Execution time in milliseconds |

### Constructors

```xxml
// Default constructor
Instantiate TestResult^ As <result> = TestResult::Constructor();

// Parameterized constructor
Instantiate TestResult^ As <result> = TestResult::Constructor(
    String::Constructor("testAddition"),
    Bool::Constructor(True),
    String::Constructor("passed")
);
```

---

## TestSummary Class

Aggregates results from multiple tests.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `totalTests` | `Integer^` | Total number of tests run |
| `passedTests` | `Integer^` | Number of passing tests |
| `failedTests` | `Integer^` | Number of failing tests |
| `results` | `List<TestResult>^` | Individual test results |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `addResult(result)` | `None` | Add a test result |
| `allPassed()` | `Bool^` | Check if all tests passed |
| `printSummary()` | `None` | Print formatted summary to console |

### Summary Output

```
=== Test Summary ===
Total:  5
Passed: 4
Failed: 1

Failed tests:
  - testDivision: expected 3 but got 2

Some tests failed.
```

---

## TestRunner Class

The main test execution engine.

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `runTestClass(typeName)` | `TestSummary^` | Run all tests in a class |
| `run(typeName)` | `Integer^` | Run tests and return exit code |
| `fail(message)` | `None` | Mark current test as failed |

### Usage

```xxml
Instantiate TestRunner^ As <runner> = TestRunner::Constructor();

// Option 1: Get detailed summary
Instantiate TestSummary^ As <summary> = runner.runTestClass(String::Constructor("MyTests"));
Run summary.printSummary();

// Option 2: Run and get exit code directly
Instantiate Integer^ As <exitCode> = runner.run(String::Constructor("MyTests"));
// exitCode is 0 if all passed, 1 if any failed
```

---

## Writing Tests

### Test Method Conventions

1. **Naming**: Methods must start with `test` to be discovered
2. **Return Type**: Should return `None`
3. **Parameters**: Should take no parameters
4. **Independence**: Each test should be independent of others

### Example Test Suite

```xxml
#import Language::Core;
#import Language::Collections;
#import Language::Test;

@Test
[ Class <ListTests> Final Extends None
    [ Public <>
        Method <testEmptyList> Returns None Parameters () Do {
            Instantiate List<Integer>^ As <list> = List<Integer>::Constructor();
            Run Assert::equals(
                Integer::Constructor(0),
                list.size(),
                String::Constructor("New list should be empty")
            );
        }

        Method <testAddElement> Returns None Parameters () Do {
            Instantiate List<Integer>^ As <list> = List<Integer>::Constructor();
            Run list.add(Integer::Constructor(42));

            Run Assert::equals(
                Integer::Constructor(1),
                list.size(),
                String::Constructor("List should have one element")
            );

            Run Assert::equals(
                Integer::Constructor(42),
                list.get(Integer::Constructor(0)),
                String::Constructor("Element should be 42")
            );
        }

        Method <testClear> Returns None Parameters () Do {
            Instantiate List<Integer>^ As <list> = List<Integer>::Constructor();
            Run list.add(Integer::Constructor(1));
            Run list.add(Integer::Constructor(2));
            Run list.clear();

            Run Assert::equals(
                Integer::Constructor(0),
                list.size(),
                String::Constructor("Cleared list should be empty")
            );
        }
    ]
]
```

### Testing with Setup

```xxml
@Test
[ Class <DatabaseTests> Final Extends None
    [ Private <>
        Property <db> Types Database^;
    ]

    [ Public <>
        // Helper method (not a test - doesn't start with "test")
        Method <setup> Returns None Parameters () Do {
            Set db = Database::Constructor();
            Run db.connect(String::Constructor("test.db"));
        }

        Method <teardown> Returns None Parameters () Do {
            Run db.disconnect();
        }

        Method <testInsert> Returns None Parameters () Do {
            Run setup();
            // ... test logic ...
            Run teardown();
        }

        Method <testQuery> Returns None Parameters () Do {
            Run setup();
            // ... test logic ...
            Run teardown();
        }
    ]
]
```

---

## Running Tests

### From Entrypoint

```xxml
[ Entrypoint {
    Instantiate TestRunner^ As <runner> = TestRunner::Constructor();

    // Run single test class
    Instantiate Integer^ As <result> = runner.run(String::Constructor("MyTests"));

    // Run multiple test classes
    Instantiate TestSummary^ As <summary1> = runner.runTestClass(String::Constructor("MathTests"));
    Instantiate TestSummary^ As <summary2> = runner.runTestClass(String::Constructor("StringTests"));

    Run summary1.printSummary();
    Run summary2.printSummary();

    Exit(result.toInt64());
}]
```

### Compile and Run

```bash
# Compile test file
xxml MyTests.XXML -o mytests.exe

# Run tests
./mytests.exe

# Check exit code (0 = all passed, 1 = failures)
echo $?
```

---

## Limitations

- **No Dynamic Invocation**: Test methods are discovered but not dynamically invoked yet. The framework reports discovered tests; actual execution requires explicit method calls.
- **No Parameterized Tests**: Tests cannot receive parameters.
- **No Test Ordering**: Tests run in method discovery order.
- **No Timeouts**: Tests cannot specify timeout limits.

---

## See Also

- [Reflection](../advanced/REFLECTION.md) - Runtime type introspection
- [Annotations](../advanced/ANNOTATIONS.md) - Custom annotations
- [Collections](COLLECTIONS.md) - List and other collections
