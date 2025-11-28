# XXML Threading and Concurrency

XXML provides comprehensive multithreading support through low-level syscalls that map directly to platform-native threading primitives. On Windows, these use `_beginthreadex`, `CRITICAL_SECTION`, and `CONDITION_VARIABLE`. On POSIX systems (Linux, macOS), they use pthreads.

## Table of Contents

1. [Thread Utilities](#thread-utilities)
2. [Atomic Operations](#atomic-operations)
3. [Mutexes](#mutexes)
4. [Condition Variables](#condition-variables)
5. [Thread-Local Storage](#thread-local-storage)
6. [Thread Creation](#thread-creation)
7. [Best Practices](#best-practices)
8. [Examples](#examples)

---

## Thread Utilities

Basic thread control functions available via syscalls.

### Sleep

Suspends the current thread for a specified number of milliseconds.

```xxml
// Sleep for 100 milliseconds
Instantiate NativeType<"int64">^ As <ms> = 100;
Run Syscall::Thread_sleep(ms);
```

### Yield

Voluntarily yields the current thread's time slice to allow other threads to run.

```xxml
Run Syscall::Thread_yield();
```

### Get Current Thread ID

Returns the OS-level thread identifier for the current thread.

```xxml
Instantiate NativeType<"int64">^ As <threadId> = Syscall::Thread_currentId();
Run Console::printLine(Integer::Constructor(threadId).toString());
```

---

## Atomic Operations

Atomic operations provide lock-free synchronization for simple integer values. These operations are guaranteed to be indivisible and are useful for counters, flags, and simple synchronization patterns.

### Creating and Destroying Atomics

```xxml
// Create an atomic with initial value 0
Instantiate NativeType<"int64">^ As <initVal> = 0;
Instantiate NativeType<"ptr">^ As <counter> = Syscall::Atomic_create(initVal);

// ... use the atomic ...

// Clean up when done
Run Syscall::Atomic_destroy(counter);
```

### Load and Store

```xxml
// Atomically read the current value
Instantiate NativeType<"int64">^ As <value> = Syscall::Atomic_load(counter);

// Atomically write a new value
Instantiate NativeType<"int64">^ As <newVal> = 42;
Run Syscall::Atomic_store(counter, newVal);
```

### Add and Subtract

These operations atomically modify the value and return the new value.

```xxml
// Add 5 to the counter, returns new value (47)
Instantiate NativeType<"int64">^ As <addAmount> = 5;
Instantiate NativeType<"int64">^ As <afterAdd> = Syscall::Atomic_add(counter, addAmount);

// Subtract 3 from the counter, returns new value (44)
Instantiate NativeType<"int64">^ As <subAmount> = 3;
Instantiate NativeType<"int64">^ As <afterSub> = Syscall::Atomic_sub(counter, subAmount);
```

### Exchange

Atomically sets a new value and returns the old value.

```xxml
Instantiate NativeType<"int64">^ As <newVal> = 100;
Instantiate NativeType<"int64">^ As <oldVal> = Syscall::Atomic_exchange(counter, newVal);
// oldVal contains the previous value, counter now holds 100
```

### Compare and Swap (CAS)

The fundamental building block for lock-free algorithms. Atomically compares the current value with `expected`, and if they match, sets the value to `desired`.

```xxml
Instantiate NativeType<"int64">^ As <expected> = 100;
Instantiate NativeType<"int64">^ As <desired> = 200;
Instantiate NativeType<"i1">^ As <success> = Syscall::Atomic_compareAndSwap(counter, expected, desired);

// success is 1 (true) if the swap occurred, 0 (false) if current != expected
```

---

## Mutexes

Mutexes (mutual exclusion locks) provide exclusive access to shared resources.

### Creating and Destroying Mutexes

```xxml
// Create a mutex
Instantiate NativeType<"ptr">^ As <mutex> = Syscall::Mutex_create();

// ... use the mutex ...

// Clean up when done
Run Syscall::Mutex_destroy(mutex);
```

### Lock and Unlock

```xxml
// Acquire the lock (blocks if already held by another thread)
Instantiate NativeType<"int64">^ As <lockResult> = Syscall::Mutex_lock(mutex);
// lockResult is 0 on success

// Critical section - only one thread can execute this at a time
// ... protected code ...

// Release the lock
Instantiate NativeType<"int64">^ As <unlockResult> = Syscall::Mutex_unlock(mutex);
// unlockResult is 0 on success
```

### Try Lock (Non-blocking)

Attempts to acquire the lock without blocking. Useful for avoiding deadlocks or implementing spin-wait patterns.

```xxml
Instantiate NativeType<"i1">^ As <acquired> = Syscall::Mutex_tryLock(mutex);
If (Bool::Constructor(acquired)) -> {
    // Got the lock, do work
    // ...
    Run Syscall::Mutex_unlock(mutex);
} Else -> {
    // Lock was held by another thread
    Run Console::printLine(String::Constructor("Could not acquire lock"));
}
```

> **Note**: On Windows, `CRITICAL_SECTION` is recursive, meaning the same thread can lock the same mutex multiple times (and must unlock the same number of times). On POSIX systems with default settings, this may cause a deadlock.

---

## Condition Variables

Condition variables allow threads to wait for specific conditions to become true. They must always be used with a mutex.

### Creating and Destroying Condition Variables

```xxml
Instantiate NativeType<"ptr">^ As <condVar> = Syscall::CondVar_create();

// ... use the condition variable ...

Run Syscall::CondVar_destroy(condVar);
```

### Wait

Atomically releases the mutex and waits for a signal. When signaled, re-acquires the mutex before returning.

```xxml
// Must hold the mutex before calling wait
Run Syscall::Mutex_lock(mutex);

// Wait for condition (releases mutex while waiting, reacquires on return)
Instantiate NativeType<"int64">^ As <waitResult> = Syscall::CondVar_wait(condVar, mutex);

// At this point we hold the mutex again
Run Syscall::Mutex_unlock(mutex);
```

### Wait with Timeout

Same as wait, but returns after a timeout even if not signaled.

```xxml
Instantiate NativeType<"int64">^ As <timeoutMs> = 5000;  // 5 seconds
Instantiate NativeType<"int64">^ As <result> = Syscall::CondVar_waitTimeout(condVar, mutex, timeoutMs);
// result: 0 = signaled, 1 = timed out, -1 = error
```

### Signal and Broadcast

```xxml
// Wake up ONE waiting thread
Instantiate NativeType<"int64">^ As <signalResult> = Syscall::CondVar_signal(condVar);

// Wake up ALL waiting threads
Instantiate NativeType<"int64">^ As <broadcastResult> = Syscall::CondVar_broadcast(condVar);
```

---

## Thread-Local Storage

Thread-local storage (TLS) allows each thread to have its own copy of a variable.

### Creating and Destroying TLS Keys

```xxml
// Create a TLS key
Instantiate NativeType<"ptr">^ As <tlsKey> = Syscall::TLS_create();

// ... use TLS ...

// Clean up
Run Syscall::TLS_destroy(tlsKey);
```

### Get and Set Values

Each thread sees its own independent value for the same key.

```xxml
// Store a value (typically a pointer to thread-specific data)
Instantiate Integer^ As <myThreadData> = Integer::Constructor(42);
Run Syscall::TLS_set(tlsKey, myThreadData);

// Retrieve the value (in the same or different code path, same thread)
Instantiate NativeType<"ptr">^ As <retrieved> = Syscall::TLS_get(tlsKey);
```

---

## Thread Creation

Thread creation allows spawning new threads of execution with XXML lambdas.

### Spawning Threads with Lambdas

The preferred way to spawn threads is using `Thread_spawn_lambda` with an XXML lambda:

```xxml
// Create a lambda that will run in the new thread
Instantiate F(None^)()^ As <threadFunc> = [ Lambda [] Returns None^ Parameters () {
    Run Console::printLine(String::Constructor("Hello from worker thread!"));
    Return None::Constructor();
}];

// Spawn the thread
Instantiate NativeType<"ptr">^ As <threadHandle> = Syscall::Thread_spawn_lambda(threadFunc);

// Wait for thread completion
Instantiate NativeType<"int64">^ As <joinResult> = Syscall::Thread_join(threadHandle);
```

### Capturing Variables in Thread Lambdas

Lambdas can capture variables from the enclosing scope:

```xxml
// Create shared atomic counter
Instantiate NativeType<"ptr">^ As <counter> = Syscall::Atomic_create(0);

// Lambda captures counter by reference
Instantiate F(None^)()^ As <workerFunc> = [ Lambda [&counter] Returns None^ Parameters () {
    Instantiate NativeType<"int64">^ As <one> = 1;
    Run Syscall::Atomic_add(counter, one);  // Safe: atomic operation
    Return None::Constructor();
}];

Instantiate NativeType<"ptr">^ As <thread> = Syscall::Thread_spawn_lambda(workerFunc);
Run Syscall::Thread_join(thread);
```

### Thread Management

```xxml
// Wait for thread completion (blocks until thread finishes)
Instantiate NativeType<"int64">^ As <joinResult> = Syscall::Thread_join(threadHandle);

// Detach a thread (let it run independently, cannot join later)
Instantiate NativeType<"int64">^ As <detachResult> = Syscall::Thread_detach(threadHandle);

// Check if thread is joinable (still running and not detached)
Instantiate NativeType<"i1">^ As <isJoinable> = Syscall::Thread_isJoinable(threadHandle);
```

### Low-Level Thread API

For advanced use cases, you can use the low-level `Thread_create`:

```xxml
// Create a thread with raw function pointer and argument
// thread_handle = Syscall::Thread_create(function_ptr, arg);
```

---

## Best Practices

### 1. Always Clean Up Resources

```xxml
// Always destroy threading primitives when done
Run Syscall::Mutex_destroy(mutex);
Run Syscall::Atomic_destroy(atomic);
Run Syscall::CondVar_destroy(condVar);
Run Syscall::TLS_destroy(tlsKey);
```

### 2. Use Mutexes for Shared Data

```xxml
// Protect shared data with a mutex
Run Syscall::Mutex_lock(mutex);
// ... modify shared data ...
Run Syscall::Mutex_unlock(mutex);
```

### 3. Prefer Atomics for Simple Counters

```xxml
// Use atomics for simple increment/decrement operations
Instantiate NativeType<"int64">^ As <one> = 1;
Run Syscall::Atomic_add(counter, one);  // Lock-free!
```

### 4. Avoid Deadlocks

- Always acquire locks in the same order across all threads
- Use `tryLock` when appropriate
- Keep critical sections short

### 5. Condition Variable Pattern

```xxml
// Producer pattern
Run Syscall::Mutex_lock(mutex);
// ... produce data ...
Run Syscall::CondVar_signal(condVar);
Run Syscall::Mutex_unlock(mutex);

// Consumer pattern
Run Syscall::Mutex_lock(mutex);
// Wait in a loop to handle spurious wakeups
// while (!condition) CondVar_wait(...)
Run Syscall::CondVar_wait(condVar, mutex);
// ... consume data ...
Run Syscall::Mutex_unlock(mutex);
```

---

## Examples

### Example 1: Atomic Counter

```xxml
#import Language::Core;

[ Entrypoint
    {
        // Create atomic counter starting at 0
        Instantiate NativeType<"int64">^ As <zero> = 0;
        Instantiate NativeType<"ptr">^ As <counter> = Syscall::Atomic_create(zero);

        // Increment multiple times
        Instantiate NativeType<"int64">^ As <one> = 1;
        Run Syscall::Atomic_add(counter, one);
        Run Syscall::Atomic_add(counter, one);
        Run Syscall::Atomic_add(counter, one);

        // Read final value
        Instantiate NativeType<"int64">^ As <final> = Syscall::Atomic_load(counter);
        Run Console::printLine(String::Constructor("Counter value: "));
        Run Console::printLine(Integer::Constructor(final).toString());
        // Output: 3

        Run Syscall::Atomic_destroy(counter);
        Exit(0);
    }
]
```

### Example 2: Protected Resource with Mutex

```xxml
#import Language::Core;

[ Entrypoint
    {
        Instantiate NativeType<"ptr">^ As <mutex> = Syscall::Mutex_create();

        // Simulate protected access
        Run Syscall::Mutex_lock(mutex);
        Run Console::printLine(String::Constructor("Entered critical section"));

        // ... do work with shared resource ...
        Instantiate NativeType<"int64">^ As <delay> = 100;
        Run Syscall::Thread_sleep(delay);

        Run Console::printLine(String::Constructor("Leaving critical section"));
        Run Syscall::Mutex_unlock(mutex);

        Run Syscall::Mutex_destroy(mutex);
        Exit(0);
    }
]
```

### Example 3: Thread Utilities Demo

```xxml
#import Language::Core;

[ Entrypoint
    {
        // Get thread ID
        Instantiate NativeType<"int64">^ As <tid> = Syscall::Thread_currentId();
        Run Console::printLine(String::Constructor("Main thread ID: "));
        Run Console::printLine(Integer::Constructor(tid).toString());

        // Yield to other threads
        Run Console::printLine(String::Constructor("Yielding..."));
        Run Syscall::Thread_yield();

        // Sleep
        Run Console::printLine(String::Constructor("Sleeping for 500ms..."));
        Instantiate NativeType<"int64">^ As <ms> = 500;
        Run Syscall::Thread_sleep(ms);
        Run Console::printLine(String::Constructor("Awake!"));

        Exit(0);
    }
]
```

### Example 4: Multi-Threaded Counter

```xxml
#import Language::Core;

[ Entrypoint
    {
        // Shared atomic counter
        Instantiate NativeType<"int64">^ As <zero> = 0;
        Instantiate NativeType<"ptr">^ As <counter> = Syscall::Atomic_create(zero);

        // Create worker thread 1
        Instantiate F(None^)()^ As <worker1> = [ Lambda [&counter] Returns None^ Parameters () {
            Run Console::printLine(String::Constructor("Worker 1 starting"));
            Instantiate NativeType<"int64">^ As <one> = 1;
            Run Syscall::Atomic_add(counter, one);
            Run Syscall::Atomic_add(counter, one);
            Run Syscall::Atomic_add(counter, one);
            Run Console::printLine(String::Constructor("Worker 1 done"));
            Return None::Constructor();
        }];

        // Create worker thread 2
        Instantiate F(None^)()^ As <worker2> = [ Lambda [&counter] Returns None^ Parameters () {
            Run Console::printLine(String::Constructor("Worker 2 starting"));
            Instantiate NativeType<"int64">^ As <one> = 1;
            Run Syscall::Atomic_add(counter, one);
            Run Syscall::Atomic_add(counter, one);
            Run Syscall::Atomic_add(counter, one);
            Run Console::printLine(String::Constructor("Worker 2 done"));
            Return None::Constructor();
        }];

        // Spawn both threads
        Instantiate NativeType<"ptr">^ As <t1> = Syscall::Thread_spawn_lambda(worker1);
        Instantiate NativeType<"ptr">^ As <t2> = Syscall::Thread_spawn_lambda(worker2);

        // Wait for both to complete
        Run Syscall::Thread_join(t1);
        Run Syscall::Thread_join(t2);

        // Final count should be 6 (3 + 3)
        Instantiate NativeType<"int64">^ As <final> = Syscall::Atomic_load(counter);
        Run Console::printLine(String::Constructor("Final count: "));
        Run Console::printLine(Integer::Constructor(final).toString());

        Run Syscall::Atomic_destroy(counter);
        Exit(0);
    }
]
```

---

## Runtime Functions Reference

| Function | Signature | Description |
|----------|-----------|-------------|
| `Thread_create` | `(ptr, ptr) -> ptr` | Create a new thread |
| `Thread_join` | `(ptr) -> i64` | Wait for thread completion |
| `Thread_detach` | `(ptr) -> i64` | Detach a thread |
| `Thread_isJoinable` | `(ptr) -> i1` | Check if thread is joinable |
| `Thread_sleep` | `(i64) -> void` | Sleep for milliseconds |
| `Thread_yield` | `() -> void` | Yield time slice |
| `Thread_currentId` | `() -> i64` | Get current thread ID |
| `Thread_spawn_lambda` | `(ptr) -> ptr` | Spawn thread with lambda |
| `Mutex_create` | `() -> ptr` | Create a mutex |
| `Mutex_destroy` | `(ptr) -> void` | Destroy a mutex |
| `Mutex_lock` | `(ptr) -> i64` | Lock (blocking) |
| `Mutex_unlock` | `(ptr) -> i64` | Unlock |
| `Mutex_tryLock` | `(ptr) -> i1` | Try to lock (non-blocking) |
| `CondVar_create` | `() -> ptr` | Create condition variable |
| `CondVar_destroy` | `(ptr) -> void` | Destroy condition variable |
| `CondVar_wait` | `(ptr, ptr) -> i64` | Wait on condition |
| `CondVar_waitTimeout` | `(ptr, ptr, i64) -> i64` | Wait with timeout |
| `CondVar_signal` | `(ptr) -> i64` | Signal one waiter |
| `CondVar_broadcast` | `(ptr) -> i64` | Signal all waiters |
| `Atomic_create` | `(i64) -> ptr` | Create atomic integer |
| `Atomic_destroy` | `(ptr) -> void` | Destroy atomic |
| `Atomic_load` | `(ptr) -> i64` | Load value |
| `Atomic_store` | `(ptr, i64) -> void` | Store value |
| `Atomic_add` | `(ptr, i64) -> i64` | Add and return new value |
| `Atomic_sub` | `(ptr, i64) -> i64` | Subtract and return new value |
| `Atomic_compareAndSwap` | `(ptr, i64, i64) -> i1` | CAS operation |
| `Atomic_exchange` | `(ptr, i64) -> i64` | Exchange values |
| `TLS_create` | `() -> ptr` | Create TLS key |
| `TLS_destroy` | `(ptr) -> void` | Destroy TLS key |
| `TLS_get` | `(ptr) -> ptr` | Get thread-local value |
| `TLS_set` | `(ptr, ptr) -> void` | Set thread-local value |

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
