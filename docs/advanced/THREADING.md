# XXML Threading and Concurrency

XXML provides comprehensive multithreading support through high-level classes that wrap platform-native threading primitives. On Windows, these use `_beginthreadex`, `CRITICAL_SECTION`, and `CONDITION_VARIABLE`. On POSIX systems (Linux, macOS), they use pthreads.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Thread Safety Constraints](#thread-safety-constraints)
   - [Sendable](#sendable)
   - [Sharable](#sharable)
   - [@Unsafe Annotation](#unsafe-annotation)
3. [Thread Class](#thread-class)
4. [Mutex Class](#mutex-class)
5. [LockGuard Class](#lockguard-class)
6. [Atomic Class](#atomic-class)
7. [ConditionVariable Class](#conditionvariable-class)
8. [Semaphore Class](#semaphore-class)
9. [ThreadLocal Class](#threadlocal-class)
10. [Best Practices](#best-practices)
11. [Examples](#examples)

---

## Quick Start

```xxml
#import Language::Core;
#import Language::Concurrent;

[ Entrypoint
    {
        // Create and start a thread with a lambda
        Instantiate F(None^)()^ As <work> = [ Lambda [] Returns None^ Parameters () {
            Run Console::printLine(String::Constructor("Hello from thread!"));
            Return None::Constructor();
        }];

        Instantiate Concurrent::Thread^ As <t> = Concurrent::Thread::spawn(work);

        // Wait for thread to complete
        Run t.join();

        Run Console::printLine(String::Constructor("Thread finished!"));
        Exit(0);
    }
]
```

---

## Thread Safety Constraints

XXML provides two marker constraints—`Sendable` and `Sharable`—that enable compile-time verification of thread safety. These constraints allow the type system to prevent common concurrency bugs.

### Sendable

The `Sendable` constraint marks types that can be safely **moved** across thread boundaries.

**Location:** `Language/Core/Sendable.XXML`

**Requirements for a type to be Sendable:**
- All owned (`^`) fields must be of Sendable types (recursively)
- No reference (`&`) fields (references would become dangling across threads)
- Copy (`%`) values are implicitly Sendable
- Primitive types (`Integer`, `Bool`, `Float`, `Double`, `String`, `Char`) are Sendable
- `Atomic<T>` is Sendable

```xxml
#import Language::Core;

// This class is Sendable because all fields are Sendable
@Derive(trait = "Sendable")
[ Class <Message> Final Extends None
    [ Public <>
        Property <id> Types Integer^;
        Property <content> Types String^;
        Constructor = default;
    ]
]

// Use in thread spawn
Instantiate Message^ As <msg> = Message::Constructor();
// msg can be safely moved to another thread
```

**Non-Sendable Example:**

```xxml
// This class is NOT Sendable - has a reference field
[ Class <NotSendable> Final Extends None
    [ Public <>
        Property <ref> Types Integer&;  // Reference field prevents Sendable
        Constructor = default;
    ]
]
```

### Sharable

The `Sharable` constraint marks types that can be safely **shared** (referenced) across threads simultaneously.

**Location:** `Language/Core/Sharable.XXML`

**Requirements for a type to be Sharable:**
- Immutable (no mutable state, or state is only set during construction)
- OR all mutable state is protected by synchronization primitives
- Primitive types (`Integer`, `Bool`, `Float`, `Double`, `String`, `Char`) are Sharable (immutable)
- `Atomic<T>` is Sharable (provides atomic access)
- Types with unprotected mutable state are NOT Sharable

```xxml
#import Language::Core;

// Immutable class - automatically Sharable
@Derive(trait = "Sharable")
[ Class <Config> Final Extends None
    [ Public <>
        Property <maxConnections> Types Integer^;
        Property <timeout> Types Integer^;

        // Only constructor sets values - immutable after construction
        Constructor Parameters (
            Parameter <max> Types Integer^,
            Parameter <t> Types Integer^
        ) -> {
            Set maxConnections = max;
            Set timeout = t;
        }
    ]
]
```

### @Unsafe Annotation

For FFI types or manually verified thread-safe types, use `@Unsafe` to override automatic constraint checking.

**Location:** `Language/Annotations/Unsafe.XXML`

```xxml
#import Language::Core;
#import Language::Annotations;

// FFI handle that has been manually verified thread-safe
@Derive(trait = "Sendable")
@Unsafe(reason = "External library guarantees thread-safety via internal locking")
[ Class <ExternalHandle> Final Extends None
    [ Public <>
        Property <handle> Types NativeType<"ptr">^;
        Constructor = default;
    ]
]
```

**Important:**
- `@Unsafe` requires a `reason` parameter explaining why the override is safe
- The compiler emits a warning when `@Unsafe` is used
- Misuse can lead to data races and undefined behavior

### Constraint Usage in Generic Code

Use `Sendable` and `Sharable` as generic constraints:

```xxml
// A channel that only accepts Sendable types
[ Class <Channel> Templates <T Constrains Sendable> Final Extends None
    [ Public <>
        Method <send> Returns None Parameters (Parameter <value> Types T^) Do {
            // Safe to move value to another thread
        }
    ]
]

// A cache that only accepts Sharable types
[ Class <SharedCache> Templates <T Constrains Sharable> Final Extends None
    [ Public <>
        Method <get> Returns T& Parameters () Do {
            // Safe to return reference - multiple threads can read
        }
    ]
]
```

---

## Thread Class

The `Thread` class represents an execution thread.

### Creating Threads

```xxml
#import Language::Concurrent;

// Create a thread with a lambda
Instantiate F(None^)()^ As <workerFunc> = [ Lambda [] Returns None^ Parameters () {
    Run Console::printLine(String::Constructor("Working..."));
    Return None::Constructor();
}];

// Option 1: Using spawn (static method)
Instantiate Concurrent::Thread^ As <t1> = Concurrent::Thread::spawn(workerFunc);

// Option 2: Using constructor directly
Instantiate Concurrent::Thread^ As <t2> = Concurrent::Thread::Constructor(workerFunc);
```

### Thread Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `spawn(func)` | `Thread^` | Static: Create and start a new thread |
| `join()` | `Bool^` | Wait for thread to complete |
| `detach()` | `Bool^` | Let thread run independently |
| `isJoinable()` | `Bool^` | Check if thread can be joined |
| `sleep(ms)` | `None` | Static: Sleep current thread for milliseconds |
| `yield()` | `None` | Static: Yield time slice to other threads |
| `currentId()` | `Integer^` | Static: Get current thread ID |

### Thread Utilities

```xxml
// Sleep for 100 milliseconds
Run Concurrent::Thread::sleep(Integer::Constructor(100));

// Yield to other threads
Run Concurrent::Thread::yield();

// Get current thread ID
Instantiate Integer^ As <tid> = Concurrent::Thread::currentId();
Run Console::printLine(tid.toString());
```

---

## Mutex Class

The `Mutex` class provides mutual exclusion for thread synchronization.

### Creating a Mutex

```xxml
// Mutex is automatically initialized on construction
Instantiate Concurrent::Mutex^ As <mutex> = Concurrent::Mutex::Constructor();

// Mutex is automatically destroyed when it goes out of scope
```

### Mutex Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `lock()` | `Bool^` | Acquire the lock (blocking) |
| `unlock()` | `Bool^` | Release the lock |
| `tryLock()` | `Bool^` | Try to acquire without blocking |
| `isValid()` | `Bool^` | Check if mutex was created successfully |

### Basic Mutex Usage

```xxml
Instantiate Concurrent::Mutex^ As <mutex> = Concurrent::Mutex::Constructor();

// Lock the mutex
Run mutex.lock();

// Critical section - only one thread can execute this
// ... protected code ...

// Unlock the mutex
Run mutex.unlock();
```

### Try Lock (Non-blocking)

```xxml
Instantiate Bool^ As <acquired> = mutex.tryLock();
If (acquired) -> {
    // Got the lock, do work
    // ...
    Run mutex.unlock();
} Else -> {
    // Lock was held by another thread
    Run Console::printLine(String::Constructor("Could not acquire lock"));
}
```

---

## LockGuard Class

The `LockGuard` class provides RAII-style scoped locking. It automatically acquires the lock on construction and releases it on destruction.

### Using LockGuard

```xxml
Instantiate Concurrent::Mutex^ As <mutex> = Concurrent::Mutex::Constructor();

// Create scope for automatic lock management
{
    // Lock is acquired here
    Instantiate Concurrent::LockGuard^ As <guard> = Concurrent::LockGuard::Constructor(mutex);

    // Critical section - mutex is held
    Run Console::printLine(String::Constructor("In critical section"));

    // Lock is automatically released when guard goes out of scope
}
```

### LockGuard Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `ownsTheLock()` | `Bool^` | Check if lock is currently held |
| `unlock()` | `None` | Manually release the lock early |

---

## Atomic Class

The `Atomic` class provides thread-safe atomic integer operations without locks.

### Creating an Atomic

```xxml
// Default constructor - initializes to 0
Instantiate Concurrent::Atomic^ As <counter> = Concurrent::Atomic::Constructor();

// Constructor with initial value
Instantiate Concurrent::Atomic^ As <counter2> = Concurrent::Atomic::Constructor(Integer::Constructor(42));
```

### Atomic Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `get()` | `Integer^` | Atomically load value |
| `set(value)` | `None` | Atomically store value |
| `add(value)` | `Integer^` | Add and return new value |
| `subtract(value)` | `Integer^` | Subtract and return new value |
| `increment()` | `Integer^` | Add 1 and return new value |
| `decrement()` | `Integer^` | Subtract 1 and return new value |
| `compareAndSwap(expected, desired)` | `Bool^` | CAS operation |
| `exchange(newValue)` | `Integer^` | Set new value, return old |
| `isValid()` | `Bool^` | Check if atomic was created |

### Atomic Operations

```xxml
Instantiate Concurrent::Atomic^ As <counter> = Concurrent::Atomic::Constructor();

// Increment atomically
Instantiate Integer^ As <newVal> = counter.increment();
Run Console::printLine(newVal.toString());  // Prints: 1

// Add value
Run counter.add(Integer::Constructor(5));   // Now 6

// Get current value
Instantiate Integer^ As <current> = counter.get();

// Compare and swap
Instantiate Bool^ As <success> = counter.compareAndSwap(
    Integer::Constructor(6),   // expected
    Integer::Constructor(10)   // desired
);
// If counter was 6, it's now 10 and success is true

// Exchange
Instantiate Integer^ As <old> = counter.exchange(Integer::Constructor(20));
// old is previous value, counter is now 20
```

---

## ConditionVariable Class

The `ConditionVariable` class allows threads to wait for specific conditions.

### Creating a ConditionVariable

```xxml
Instantiate Concurrent::ConditionVariable^ As <cond> = Concurrent::ConditionVariable::Constructor();
```

### ConditionVariable Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `wait(mutex)` | `Bool^` | Wait on condition (must hold mutex) |
| `waitTimeout(mutex, ms)` | `Integer^` | Wait with timeout (0=signaled, 1=timeout, -1=error) |
| `signal()` | `Bool^` | Wake one waiting thread |
| `broadcast()` | `Bool^` | Wake all waiting threads |
| `isValid()` | `Bool^` | Check if created successfully |

### Producer-Consumer Pattern

```xxml
// Shared state
Instantiate Concurrent::Mutex^ As <mutex> = Concurrent::Mutex::Constructor();
Instantiate Concurrent::ConditionVariable^ As <cond> = Concurrent::ConditionVariable::Constructor();
Instantiate Concurrent::Atomic^ As <dataReady> = Concurrent::Atomic::Constructor();

// Consumer thread
Instantiate F(None^)()^ As <consumer> = [ Lambda [&mutex, &cond, &dataReady] Returns None^ Parameters () {
    Run mutex.lock();

    // Wait for data
    Instantiate Integer^ As <ready> = dataReady.get();
    While (ready.equals(Integer::Constructor(0))) -> {
        Run cond.wait(mutex);
        Set ready = dataReady.get();
    }

    Run Console::printLine(String::Constructor("Data received!"));
    Run mutex.unlock();
    Return None::Constructor();
}];

// Producer thread
Instantiate F(None^)()^ As <producer> = [ Lambda [&mutex, &cond, &dataReady] Returns None^ Parameters () {
    // Simulate producing data
    Run Concurrent::Thread::sleep(Integer::Constructor(100));

    Run mutex.lock();
    Run dataReady.set(Integer::Constructor(1));
    Run cond.signal();
    Run mutex.unlock();
    Return None::Constructor();
}];

Instantiate Concurrent::Thread^ As <t1> = Concurrent::Thread::spawn(consumer);
Instantiate Concurrent::Thread^ As <t2> = Concurrent::Thread::spawn(producer);

Run t1.join();
Run t2.join();
```

---

## Semaphore Class

The `Semaphore` class is a counting semaphore for resource limiting.

### Creating a Semaphore

```xxml
// Semaphore with initial count of 3 (3 resources available)
Instantiate Concurrent::Semaphore^ As <sem> = Concurrent::Semaphore::Constructor(Integer::Constructor(3));

// Default constructor starts at 0
Instantiate Concurrent::Semaphore^ As <sem2> = Concurrent::Semaphore::Constructor();
```

### Semaphore Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `acquire()` | `None` | Decrement count, block if 0 |
| `tryAcquire()` | `Bool^` | Try to acquire without blocking |
| `release()` | `None` | Increment count, wake waiters |
| `getCount()` | `Integer^` | Get current count (approximate) |

### Resource Pool Example

```xxml
// Pool of 2 resources
Instantiate Concurrent::Semaphore^ As <pool> = Concurrent::Semaphore::Constructor(Integer::Constructor(2));

// Worker acquires a resource
Run pool.acquire();
// ... use the resource ...
Run pool.release();
```

---

## ThreadLocal Class

The `ThreadLocal<T>` template class provides thread-local storage where each thread has its own independent copy of the value.

### Creating ThreadLocal Storage

```xxml
// Create thread-local storage for Integer values
Instantiate Concurrent::ThreadLocal<Integer>^ As <tls> = Concurrent::ThreadLocal@Integer::Constructor();
```

### ThreadLocal Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `get()` | `T^` | Get thread-local value |
| `set(value)` | `None` | Set thread-local value |
| `isSet()` | `Bool^` | Check if value has been set for this thread |
| `isValid()` | `Bool^` | Check if TLS key was created |

### ThreadLocal Example

```xxml
Instantiate Concurrent::ThreadLocal<Integer>^ As <threadId> = Concurrent::ThreadLocal@Integer::Constructor();

Instantiate F(None^)()^ As <worker> = [ Lambda [&threadId] Returns None^ Parameters () {
    // Set thread-local value
    Run threadId.set(Concurrent::Thread::currentId());

    // Get thread-local value (each thread sees its own value)
    Instantiate Integer^ As <myId> = threadId.get();
    Run Console::printLine(myId.toString());
    Return None::Constructor();
}];
```

---

## Best Practices

### 1. Use LockGuard for Exception Safety

```xxml
// Good: Lock is always released
{
    Instantiate Concurrent::LockGuard^ As <guard> = Concurrent::LockGuard::Constructor(mutex);
    // ... code that might fail ...
}

// Avoid: Manual lock/unlock can leave mutex locked on error
Run mutex.lock();
// ... if something fails here, mutex stays locked ...
Run mutex.unlock();
```

### 2. Prefer Atomics for Simple Counters

```xxml
// Good: Lock-free counter
Instantiate Concurrent::Atomic^ As <counter> = Concurrent::Atomic::Constructor();
Run counter.increment();  // Fast, no locking

// Slower: Mutex for simple counter
Run mutex.lock();
// ... increment ...
Run mutex.unlock();
```

### 3. Avoid Deadlocks

- Always acquire locks in the same order across all threads
- Use `tryLock()` when appropriate
- Keep critical sections short
- Use `LockGuard` for automatic release

### 4. Condition Variable Pattern

Always use condition variables with a loop to handle spurious wakeups:

```xxml
Run mutex.lock();
While (conditionNotMet) -> {
    Run cond.wait(mutex);
}
// Condition is now met
Run mutex.unlock();
```

### 5. Clean Up Resources

While destructors handle cleanup automatically, be mindful of resource lifetime:

```xxml
// Mutex destroyed automatically when out of scope
{
    Instantiate Concurrent::Mutex^ As <localMutex> = Concurrent::Mutex::Constructor();
    // ... use mutex ...
}  // Mutex destroyed here
```

---

## Examples

### Example 1: Parallel Counter

```xxml
#import Language::Core;
#import Language::Concurrent;

[ Entrypoint
    {
        // Shared atomic counter
        Instantiate Concurrent::Atomic^ As <counter> = Concurrent::Atomic::Constructor();

        // Create worker 1
        Instantiate F(None^)()^ As <worker1> = [ Lambda [&counter] Returns None^ Parameters () {
            Run Console::printLine(String::Constructor("Worker 1 starting"));
            Run counter.increment();
            Run counter.increment();
            Run counter.increment();
            Run Console::printLine(String::Constructor("Worker 1 done"));
            Return None::Constructor();
        }];

        // Create worker 2
        Instantiate F(None^)()^ As <worker2> = [ Lambda [&counter] Returns None^ Parameters () {
            Run Console::printLine(String::Constructor("Worker 2 starting"));
            Run counter.increment();
            Run counter.increment();
            Run counter.increment();
            Run Console::printLine(String::Constructor("Worker 2 done"));
            Return None::Constructor();
        }];

        // Spawn both threads
        Instantiate Concurrent::Thread^ As <t1> = Concurrent::Thread::spawn(worker1);
        Instantiate Concurrent::Thread^ As <t2> = Concurrent::Thread::spawn(worker2);

        // Wait for both
        Run t1.join();
        Run t2.join();

        // Final count should be 6
        Run Console::printLine(String::Constructor("Final count: "));
        Run Console::printLine(counter.get().toString());

        Exit(0);
    }
]
```

### Example 2: Protected Shared Data

```xxml
#import Language::Core;
#import Language::Concurrent;

[ Entrypoint
    {
        Instantiate Concurrent::Mutex^ As <mutex> = Concurrent::Mutex::Constructor();
        Instantiate Integer^ As <sharedData> = Integer::Constructor(0);

        Instantiate F(None^)()^ As <modifier> = [ Lambda [&mutex, &sharedData] Returns None^ Parameters () {
            // Use LockGuard for automatic unlock
            Instantiate Concurrent::LockGuard^ As <guard> = Concurrent::LockGuard::Constructor(mutex);

            // Safely modify shared data
            Set sharedData = sharedData.add(Integer::Constructor(10));
            Run Console::printLine(String::Constructor("Modified data"));

            Return None::Constructor();
        }];

        Instantiate Concurrent::Thread^ As <t1> = Concurrent::Thread::spawn(modifier);
        Instantiate Concurrent::Thread^ As <t2> = Concurrent::Thread::spawn(modifier);

        Run t1.join();
        Run t2.join();

        Run Console::printLine(String::Constructor("Final value: "));
        Run Console::printLine(sharedData.toString());  // Should be 20

        Exit(0);
    }
]
```

### Example 3: Thread Pool Pattern with Semaphore

```xxml
#import Language::Core;
#import Language::Concurrent;

[ Entrypoint
    {
        // Limit to 2 concurrent workers
        Instantiate Concurrent::Semaphore^ As <pool> = Concurrent::Semaphore::Constructor(Integer::Constructor(2));
        Instantiate Concurrent::Atomic^ As <completed> = Concurrent::Atomic::Constructor();

        // Create 5 tasks
        Instantiate F(None^)()^ As <task> = [ Lambda [&pool, &completed] Returns None^ Parameters () {
            Run pool.acquire();  // Wait for a slot

            Run Console::printLine(String::Constructor("Task running"));
            Run Concurrent::Thread::sleep(Integer::Constructor(50));
            Run Console::printLine(String::Constructor("Task done"));

            Run completed.increment();
            Run pool.release();  // Release the slot
            Return None::Constructor();
        }];

        // Spawn all 5 tasks (only 2 will run at a time)
        Instantiate Concurrent::Thread^ As <t1> = Concurrent::Thread::spawn(task);
        Instantiate Concurrent::Thread^ As <t2> = Concurrent::Thread::spawn(task);
        Instantiate Concurrent::Thread^ As <t3> = Concurrent::Thread::spawn(task);
        Instantiate Concurrent::Thread^ As <t4> = Concurrent::Thread::spawn(task);
        Instantiate Concurrent::Thread^ As <t5> = Concurrent::Thread::spawn(task);

        Run t1.join();
        Run t2.join();
        Run t3.join();
        Run t4.join();
        Run t5.join();

        Run Console::printLine(String::Constructor("All tasks completed: "));
        Run Console::printLine(completed.get().toString());

        Exit(0);
    }
]
```

---

## Class Reference

| Class | Description |
|-------|-------------|
| `Thread` | Execution thread with lambda support |
| `Mutex` | Mutual exclusion lock |
| `LockGuard` | RAII scoped lock |
| `Atomic` | Thread-safe atomic integer |
| `ConditionVariable` | Thread coordination/signaling |
| `Semaphore` | Counting semaphore |
| `ThreadLocal<T>` | Per-thread storage |

---

## Platform Notes

### Windows
- Uses `_beginthreadex` for thread creation
- `CRITICAL_SECTION` for mutexes (recursive by default)
- `CONDITION_VARIABLE` for condition variables
- `InterlockedAdd64`, `InterlockedCompareExchange64` for atomics
- `TlsAlloc`, `TlsGetValue`, `TlsSetValue` for TLS

### POSIX (Linux, macOS)
- Uses `pthread_create` for thread creation
- `pthread_mutex_t` for mutexes
- `pthread_cond_t` for condition variables
- GCC/Clang `__atomic_*` builtins for atomics
- `pthread_key_create`, `pthread_getspecific`, `pthread_setspecific` for TLS

---

## See Also

- [Language Specification](LANGUAGE_SPEC.md) - Complete language syntax
- [Lambdas](LANGUAGE_SPEC.md#lambdas-and-function-references) - Lambda expressions for thread functions
- [Advanced Features](ADVANCED_FEATURES.md) - Native types and syscalls
- [Limitations](LIMITATIONS.md) - Known concurrency limitations
