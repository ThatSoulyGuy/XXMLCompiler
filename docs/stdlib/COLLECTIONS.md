# Collections

The `Language::Collections` module provides generic data structures.

```xxml
#import Language::Core;
#import Language::Collections;
```

---

## List\<T\>

Dynamic resizable array.

```xxml
Instantiate Collections::List<Integer>^ As <numbers> = Collections::List@Integer::Constructor();
Run numbers.add(Integer::Constructor(1));
Run numbers.add(Integer::Constructor(2));
Run numbers.add(Integer::Constructor(3));

Instantiate Integer^ As <first> = numbers.get(Integer::Constructor(0));  // 1
Run Console::printLine(numbers.size().toString());  // "3"
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty list with initial capacity |

### Element Access

| Method | Returns | Description |
|--------|---------|-------------|
| `add(value: T^)` | `None` | Append element to end |
| `get(index: Integer&)` | `T^` | Get element at index |
| `set(index: Integer&, value: T^)` | `None` | Set element at index |
| `remove(index: Integer&)` | `None` | Remove element at index |

### Size Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Number of elements |
| `isEmpty()` | `Bool^` | True if size is 0 |
| `clear()` | `None` | Remove all elements |

### Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `begin()` | `ListIterator<T>^` | Iterator at first element |
| `end()` | `ListIterator<T>^` | Iterator past last element |

### Example: Iteration

```xxml
Instantiate Collections::List<String>^ As <names> = Collections::List@String::Constructor();
Run names.add(String::Constructor("Alice"));
Run names.add(String::Constructor("Bob"));

Instantiate Collections::ListIterator<String>^ As <iter> = names.begin();
While (iter.hasNext()) -> {
    Instantiate String^ As <name> = iter.next();
    Run Console::printLine(name);
}
```

---

## HashMap\<K, V\>

Hash table mapping keys to values. K must implement `Hashable` and `Equatable`.

```xxml
Instantiate Collections::HashMap<String, Integer>^ As <ages> =
    Collections::HashMap@String, Integer::Constructor();

Run ages.put(String::Constructor("Alice"), Integer::Constructor(30));
Run ages.put(String::Constructor("Bob"), Integer::Constructor(25));

Instantiate Integer^ As <aliceAge> = ages.get(String::Constructor("Alice"));  // 30
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty map with 16 buckets |

### Element Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `put(key: K^, value: V^)` | `None` | Insert or update entry |
| `get(key: K^)` | `V&` | Get value for key (null if not found) |
| `containsKey(key: K^)` | `Bool^` | Check if key exists |
| `remove(key: K^)` | `Bool^` | Remove entry, returns true if existed |

### Size Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Number of entries |
| `isEmpty()` | `Bool^` | True if size is 0 |
| `clear()` | `None` | Remove all entries |

### Bulk Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `keys()` | `List<K>^` | List of all keys |
| `values()` | `List<V>^` | List of all values |
| `begin()` | `HashMapIterator<K,V>^` | Iterator over entries |

### Example: Iteration

```xxml
Instantiate Collections::HashMapIterator<String, Integer>^ As <iter> = ages.begin();
While (iter.hasNext()) -> {
    Instantiate Collections::KeyValuePair<String, Integer>^ As <entry> = iter.next();
    Run Console::printLine(entry.key().append(String::Constructor(": ")).append(entry.value().toString()));
}
```

---

## Set\<T\>

Collection of unique elements. T must implement `Hashable` and `Equatable`.

```xxml
Instantiate Collections::Set<String>^ As <tags> = Collections::Set@String::Constructor();
Run tags.add(String::Constructor("important"));
Run tags.add(String::Constructor("urgent"));
Run tags.add(String::Constructor("important"));  // Duplicate, not added

Run Console::printLine(tags.size().toString());  // "2"
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty set with 16 buckets |

### Element Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `add(value: T^)` | `Bool^` | Add element, returns true if new |
| `contains(value: T&)` | `Bool^` | Check if element exists |
| `remove(value: T&)` | `Bool^` | Remove element, returns true if existed |

### Size Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Number of elements |
| `isEmpty()` | `Bool^` | True if size is 0 |
| `clear()` | `None` | Remove all elements |

### Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `begin()` | `SetIterator<T>^` | Iterator over elements |

---

## Array\<T, N\>

Fixed-size array where N is a compile-time constant.

```xxml
// Array of 10 integers
Instantiate Collections::Array<Integer, 10>^ As <arr> =
    Collections::Array@Integer, 10::Constructor();

Run arr.set(Integer::Constructor(0), Integer::Constructor(100));
Instantiate Integer^ As <val> = arr.get(Integer::Constructor(0));  // 100
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates array with N default-initialized elements |

### Element Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `get(index: Integer&)` | `T^` | Get element at index |
| `set(index: Integer&, value: T^)` | `None` | Set element at index |
| `fill(value: T^)` | `None` | Set all elements to value |

### Size Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Returns N |
| `isValidIndex(index: Integer&)` | `Bool^` | Check if index in bounds |

---

## Stack\<T\>

Last-in-first-out (LIFO) collection.

```xxml
Instantiate Collections::Stack<Integer>^ As <stack> = Collections::Stack@Integer::Constructor();
Run stack.push(Integer::Constructor(1));
Run stack.push(Integer::Constructor(2));
Run stack.push(Integer::Constructor(3));

Instantiate Integer^ As <top> = stack.pop();  // 3
Instantiate Integer^ As <next> = stack.peek();  // 2 (doesn't remove)
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty stack |

### Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `push(value: T^)` | `None` | Add to top |
| `pop()` | `T^` | Remove and return top |
| `peek()` | `T^` | Return top without removing |
| `size()` | `Integer^` | Number of elements |
| `isEmpty()` | `Bool^` | True if empty |
| `clear()` | `None` | Remove all elements |

---

## Queue\<T\>

First-in-first-out (FIFO) collection implemented as a circular buffer.

```xxml
Instantiate Collections::Queue<String>^ As <queue> = Collections::Queue@String::Constructor();
Run queue.enqueue(String::Constructor("first"));
Run queue.enqueue(String::Constructor("second"));

Instantiate String^ As <item> = queue.dequeue();  // "first"
```

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty queue |

### Operations

| Method | Returns | Description |
|--------|---------|-------------|
| `enqueue(value: T^)` | `None` | Add to back |
| `dequeue()` | `T^` | Remove and return front |
| `peek()` | `T^` | Return front without removing |
| `size()` | `Integer^` | Number of elements |
| `isEmpty()` | `Bool^` | True if empty |
| `clear()` | `None` | Remove all elements |

---

## Type Requirements

### Hashable Constraint

Types used as HashMap keys or Set elements must provide:

```xxml
Method <hash> Returns NativeType<"int64">^ Parameters ()
```

### Equatable Constraint

Types used as HashMap keys or Set elements must provide:

```xxml
Method <equals> Returns Bool^ Parameters (Parameter <other> Types T&)
```

The built-in `Integer` and `String` types implement both constraints.

---

## Memory Management

All collections automatically manage their internal memory:
- Constructors allocate backing storage
- Destructors free all memory via RAII
- Adding elements may trigger reallocation (except Array)

You don't need to manually free collection memory - just let owned references go out of scope.

---

## See Also

- [Iterators](ITERATORS.md) - Iterator types and protocols
- [Core Types](CORE.md) - Integer, String (used as elements)
- [Templates](../language/TEMPLATES.md) - Generic type syntax
