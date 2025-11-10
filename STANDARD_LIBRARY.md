# XXML Standard Library Documentation

## Overview

The XXML Standard Library provides a comprehensive set of classes and functions for common programming tasks. All types are actual XXML classes (not C++ primitives), ensuring type safety and consistency.

## Core Types (`Language::Core`)

### String Class

Comprehensive string manipulation and utilities.

#### Constructor
```xxml
String::Constructor()           // Empty string
String::Constructor("text")      // From literal
```

#### Query Operations
- `length() -> Integer` - Get string length
- `isEmpty() -> Bool` - Check if string is empty
- `charAt(index: Integer) -> String` - Get character at index
- `indexOf(searchStr: String) -> Integer` - Find first occurrence (-1 if not found)
- `lastIndexOf(searchStr: String) -> Integer` - Find last occurrence
- `contains(searchStr: String) -> Bool` - Check if contains substring
- `startsWith(prefix: String) -> Bool` - Check if starts with prefix
- `endsWith(suffix: String) -> Bool` - Check if ends with suffix

#### Manipulation Operations
- `copy() -> String` - Create a copy
- `append(other: String) -> String` - Append string
- `prepend(other: String) -> String` - Prepend string
- `substring(start: Integer, end: Integer) -> String` - Extract substring

#### Comparison Operations
- `equals(other: String) -> Bool` - Case-sensitive equality
- `equalsIgnoreCase(other: String) -> Bool` - Case-insensitive equality
- `compareTo(other: String) -> Integer` - Lexicographic comparison

#### Transformation Operations
- `toUpperCase() -> String` - Convert to uppercase
- `toLowerCase() -> String` - Convert to lowercase
- `trim() -> String` - Remove leading/trailing whitespace
- `replace(oldStr: String, newStr: String) -> String` - Replace first occurrence
- `replaceAll(oldStr: String, newStr: String) -> String` - Replace all occurrences
- `reverse() -> String` - Reverse the string
- `repeat(count: Integer) -> String` - Repeat string n times

#### Conversion Operations
- `toInteger() -> Integer` - Parse string to integer
- `toBool() -> Bool` - Parse string to boolean

#### Static Methods
- `String::Convert(value: Integer) -> String` - Convert integer to string
- `String::Convert(value: Bool) -> String` - Convert boolean to string

#### Example Usage
```xxml
Instantiate String As <text> = String::Constructor("Hello, XXML!");
Instantiate String As <upper> = text.toUpperCase();
Instantiate Integer As <pos> = text.indexOf(String::Constructor("XXML"));
Instantiate Bool As <hasHello> = text.contains(String::Constructor("Hello"));
Instantiate String As <reversed> = text.reverse();
```

### Integer Class

64-bit signed integer with arithmetic operations.

#### Constructor
```xxml
Integer::Constructor()          // Zero
Integer::Constructor(42)        // From literal
```

#### Arithmetic Operations
- `add(other: Integer) -> Integer`
- `subtract(other: Integer) -> Integer`
- `multiply(other: Integer) -> Integer`
- `divide(other: Integer) -> Integer`
- `modulo(other: Integer) -> Integer`

#### Operators
- `+`, `-`, `*`, `/`, `%` - Arithmetic operators
- `<`, `<=`, `>`, `>=`, `==`, `!=` - Comparison operators
- `++` - Increment operator

#### Conversion
- `toString() -> String` - Convert to string

### Bool Class

Boolean type with logical operations.

#### Constructor
```xxml
Bool::Constructor()             // False
Bool::Constructor(true)         // From literal
```

#### Operations
- `andOp(other: Bool) -> Bool` - Logical AND
- `orOp(other: Bool) -> Bool` - Logical OR
- `notOp() -> Bool` - Logical NOT

#### Conversion
- `toString() -> String` - Convert to string ("true" or "false")

## Math Library (`Language::Math`)

Comprehensive mathematical functions.

### Basic Operations
- `Math::abs(value: Integer) -> Integer` - Absolute value
- `Math::min(a: Integer, b: Integer) -> Integer` - Minimum of two values
- `Math::max(a: Integer, b: Integer) -> Integer` - Maximum of two values
- `Math::clamp(value: Integer, min: Integer, max: Integer) -> Integer` - Clamp value to range

### Power and Roots
- `Math::pow(base: Integer, exponent: Integer) -> Integer` - Exponentiation
- `Math::sqrt(value: Integer) -> Integer` - Square root (integer)

### Random Numbers
- `Math::random() -> Integer` - Random integer
- `Math::randomRange(min: Integer, max: Integer) -> Integer` - Random integer in range

### Number Theory
- `Math::gcd(a: Integer, b: Integer) -> Integer` - Greatest common divisor
- `Math::lcm(a: Integer, b: Integer) -> Integer` - Least common multiple
- `Math::isPrime(value: Integer) -> Bool` - Check if prime
- `Math::factorial(n: Integer) -> Integer` - Factorial (n!)
- `Math::fibonacci(n: Integer) -> Integer` - Nth Fibonacci number

### Example Usage
```xxml
Instantiate Integer As <absVal> = Math::abs(Integer::Constructor(-42));
Instantiate Integer As <power> = Math::pow(Integer::Constructor(2), Integer::Constructor(10));
Instantiate Integer As <gcdVal> = Math::gcd(Integer::Constructor(48), Integer::Constructor(18));
Instantiate Bool As <isPrime> = Math::isPrime(Integer::Constructor(17));
Instantiate Integer As <fib10> = Math::fibonacci(Integer::Constructor(10));
```

## Collections Library (`Language::Collections`)

Dynamic data structures for storing multiple values.

### IntegerArray Class

Fixed-size array of integers.

#### Methods
- `get(index: Integer) -> Integer` - Get element at index
- `set(index: Integer, value: Integer)` - Set element at index
- `size() -> Integer` - Get array size
- `isEmpty() -> Bool` - Check if empty
- `contains(value: Integer) -> Bool` - Check if contains value
- `indexOf(value: Integer) -> Integer` - Find index of value
- `sum() -> Integer` - Sum of all elements
- `min() -> Integer` - Minimum element
- `max() -> Integer` - Maximum element
- `average() -> Integer` - Average of elements
- `sort()` - Sort in ascending order
- `reverse()` - Reverse order
- `fill(value: Integer)` - Fill with value
- `clear()` - Clear all elements

### IntegerList Class

Dynamic-size list of integers.

#### Methods
- `add(value: Integer)` - Add element to end
- `insert(index: Integer, value: Integer)` - Insert at index
- `remove(value: Integer) -> Bool` - Remove first occurrence
- `removeAt(index: Integer)` - Remove at index
- `get(index: Integer) -> Integer` - Get element
- `set(index: Integer, value: Integer)` - Set element
- `size() -> Integer` - Get list size
- `isEmpty() -> Bool` - Check if empty
- `contains(value: Integer) -> Bool` - Check if contains
- `clear()` - Remove all elements
- `sort()` - Sort in ascending order
- `reverse()` - Reverse order

### StringArray Class

Fixed-size array of strings.

#### Methods
- `get(index: Integer) -> String` - Get element
- `set(index: Integer, value: String)` - Set element
- `size() -> Integer` - Get array size
- `contains(value: String) -> Bool` - Check if contains
- `join(delimiter: String) -> String` - Join all elements

### StringList Class

Dynamic-size list of strings.

#### Methods
- `add(value: String)` - Add element
- `get(index: Integer) -> String` - Get element
- `size() -> Integer` - Get list size
- `join(delimiter: String) -> String` - Join all elements with delimiter
- `sort()` - Sort alphabetically
- `clear()` - Remove all elements

## System Library

### Console Class (`System::Console`)

Console input/output operations.

#### Output Operations
- `System::Print(message: String)` - Print without newline
- `System::PrintLine(message: String)` - Print with newline
- `System::printError(message: String)` - Print to error stream

#### Input Operations
- `System::ReadLine() -> String` - Read line from input
- `readChar() -> String` - Read single character
- `readInt() -> Integer` - Read integer from input

#### Time Operations
- `System::GetTime() -> Integer` - Get current Unix timestamp
- `getTimeMillis() -> Integer` - Get time in milliseconds
- `sleep(milliseconds: Integer)` - Sleep for duration

#### Environment
- `getEnv(varName: String) -> String` - Get environment variable
- `setEnv(varName: String, value: String) -> Bool` - Set environment variable

#### Program Control
- `exit(exitCode: Integer)` - Exit program

## File I/O Library (`Language::IO`)

### File Class

File reading and writing operations.

#### File Opening
- `open(filePath: String) -> Bool` - Open file
- `close()` - Close file

#### Reading
- `readAll() -> String` - Read entire file
- `readLine() -> String` - Read single line
- `hasMoreLines() -> Bool` - Check if more lines available

#### Writing
- `write(content: String) -> Bool` - Write to file (overwrite)
- `writeLine(content: String) -> Bool` - Write line to file
- `append(content: String) -> Bool` - Append to file

#### File Information
- `exists() -> Bool` - Check if file exists
- `size() -> Integer` - Get file size in bytes

#### Static Methods
- `File::fileExists(filePath: String) -> Bool` - Check if file exists
- `File::deleteFile(filePath: String) -> Bool` - Delete file
- `File::copyFile(source: String, dest: String) -> Bool` - Copy file
- `File::moveFile(source: String, dest: String) -> Bool` - Move file

### Directory Class

Directory operations.

#### Methods
- `create(dirPath: String) -> Bool` - Create directory
- `delete(dirPath: String) -> Bool` - Delete directory
- `exists(dirPath: String) -> Bool` - Check if exists
- `getWorkingDirectory() -> String` - Get current directory
- `setWorkingDirectory(dirPath: String) -> Bool` - Change directory

## Complete Example

```xxml
[ Entrypoint
	{
		// String manipulation
		Instantiate String As <name> = String::Constructor("XXML");
		Instantiate String As <greeting> = String::Constructor("Hello, ");
		Run greeting = greeting.append(name);
		Run System::Print(greeting);
		Run System::Print(String::Constructor("\n"));

		// Math operations
		Instantiate Integer As <result> = Math::pow(Integer::Constructor(2), Integer::Constructor(8));
		Run System::Print(String::Constructor("2^8 = "));
		Run System::Print(String::Convert(result));
		Run System::Print(String::Constructor("\n"));

		// Number theory
		Instantiate Integer As <fib> = Math::fibonacci(Integer::Constructor(10));
		Run System::Print(String::Constructor("Fibonacci(10) = "));
		Run System::Print(String::Convert(fib));
		Run System::Print(String::Constructor("\n"));

		Exit(0);
	}
]
```

## Notes

- All types are **actual XXML classes**, not C++ primitives
- String literals automatically create `String` objects
- Integer literals automatically create `Integer` objects
- Boolean literals automatically create `Bool` objects
- Arithmetic and comparison operators work through C++ operator overloading
- The runtime library is implemented in C++ for performance but exposed as XXML classes

## Advanced Collections (`Language::Collections`)

### HashMap Classes

Key-value storage using hash map data structure.

**IntegerHashMap** - Integer key-value pairs
- `put(key: Integer, value: Integer)` - Add/update key-value pair
- `get(key: Integer) -> Integer` - Get value for key
- `remove(key: Integer)` - Remove key-value pair
- `containsKey(key: Integer) -> Bool` - Check if key exists
- `containsValue(value: Integer) -> Bool` - Check if value exists
- `size() -> Integer` - Get number of entries
- `isEmpty() -> Bool` - Check if empty
- `clear()` - Remove all entries

**StringHashMap** - String key-value pairs
- Same methods as IntegerHashMap but for String types

### Set Classes

Collection of unique values with fast membership testing.

**IntegerSet** - Set of unique integers
- `add(value: Integer) -> Bool` - Add value (returns false if already exists)
- `remove(value: Integer) -> Bool` - Remove value
- `contains(value: Integer) -> Bool` - Check membership
- `size() -> Integer` - Get number of elements
- `isEmpty() -> Bool` - Check if empty
- `clear()` - Remove all elements
- `union(other: IntegerSet) -> IntegerSet` - Union of two sets
- `intersection(other: IntegerSet) -> IntegerSet` - Intersection
- `difference(other: IntegerSet) -> IntegerSet` - Set difference

**StringSet** - Set of unique strings
- Same methods as IntegerSet but for String types

### Queue Classes

FIFO (First In First Out) data structures.

**IntegerQueue** - Integer queue
- `enqueue(value: Integer)` - Add element to back
- `dequeue() -> Integer` - Remove and return front element
- `peek() -> Integer` - View front element without removing
- `size() -> Integer` - Get number of elements
- `isEmpty() -> Bool` - Check if empty
- `clear()` - Remove all elements

**StringQueue** - String queue
- Same methods as IntegerQueue but for String types

### Stack Classes

LIFO (Last In First Out) data structures.

**IntegerStack** - Integer stack
- `push(value: Integer)` - Add element to top
- `pop() -> Integer` - Remove and return top element
- `peek() -> Integer` - View top element without removing
- `size() -> Integer` - Get number of elements
- `isEmpty() -> Bool` - Check if empty
- `clear()` - Remove all elements

**StringStack** - String stack
- Same methods as IntegerStack but for String types

## JSON Support (`Language::Format`)

### JSONObject Class

JSON object manipulation.

- `JSONObject::parse(jsonString: String) -> JSONObject` - Parse JSON string
- `stringify() -> String` - Convert to JSON string
- `get(key: String) -> String` - Get value for key
- `set(key: String, value: String)` - Set key-value pair
- `has(key: String) -> Bool` - Check if key exists
- `remove(key: String)` - Remove key
- `size() -> Integer` - Get number of properties
- `getInteger(key: String) -> Integer` - Get integer value
- `getBool(key: String) -> Bool` - Get boolean value

### JSONArray Class

JSON array manipulation.

- `JSONArray::parse(jsonString: String) -> JSONArray` - Parse JSON array
- `stringify() -> String` - Convert to JSON string
- `get(index: Integer) -> String` - Get element at index
- `add(value: String)` - Add element to end
- `remove(index: Integer)` - Remove element at index
- `size() -> Integer` - Get number of elements

## Regular Expressions (`Language::Text`)

### Pattern Class

Regular expression pattern matching.

- `Pattern::compile(patternString: String) -> Pattern` - Compile regex pattern
- `isValid() -> Bool` - Check if pattern compiled successfully
- `matches(text: String) -> Bool` - Check if entire text matches
- `find(text: String) -> Bool` - Check if pattern found in text
- `replace(text: String, replacement: String) -> String` - Replace all matches
- `replaceFirst(text: String, replacement: String) -> String` - Replace first match
- `split(text: String) -> StringArray` - Split text by pattern

### RegexUtil Class

Common regex validation utilities.

- `RegexUtil::isEmail(text: String) -> Bool` - Validate email format
- `RegexUtil::isURL(text: String) -> Bool` - Validate URL format
- `RegexUtil::isNumeric(text: String) -> Bool` - Check if only digits
- `RegexUtil::isAlphabetic(text: String) -> Bool` - Check if only letters

## Network Support (`Language::Network`)

### HTTPResponse Class

HTTP response data.

**Properties:**
- `statusCode: Integer` - HTTP status code
- `body: String` - Response body

**Methods:**
- `getStatusCode() -> Integer` - Get status code
- `getBody() -> String` - Get response body
- `isSuccess() -> Bool` - Check if 2xx status
- `isError() -> Bool` - Check if 4xx/5xx status

### HTTPClient Class

HTTP client for web requests.

**Static Methods:**
- `HTTPClient::get(url: String) -> HTTPResponse` - Perform GET request
- `HTTPClient::post(url: String, data: String) -> HTTPResponse` - Perform POST request
- `HTTPClient::put(url: String, data: String) -> HTTPResponse` - Perform PUT request
- `HTTPClient::delete(url: String) -> HTTPResponse` - Perform DELETE request

**Instance Methods:**
- `setHeader(key: String, value: String)` - Set HTTP header
- `setTimeout(milliseconds: Integer)` - Set request timeout
- `performGet(url: String) -> HTTPResponse` - GET with configured settings
- `performPost(url: String, data: String) -> HTTPResponse` - POST with configured settings

### HTTPServer Class

HTTP server functionality (placeholder for future implementation).

- `listen(port: Integer)` - Start server on port
- `stop()` - Stop server
- `isRunning() -> Bool` - Check if server is running

## Concurrent Programming (`Language::Concurrent`)

### Thread Class

Thread management.

- `start()` - Start thread execution
- `join()` - Wait for thread completion
- `isRunning() -> Bool` - Check if thread is running

**Static Methods:**
- `Thread::sleep(milliseconds: Integer)` - Sleep current thread
- `Thread::yield()` - Yield to other threads
- `Thread::getCurrentId() -> Integer` - Get current thread ID

### Mutex Class

Mutual exclusion lock.

- `lock()` - Acquire lock (blocks if locked)
- `unlock()` - Release lock
- `tryLock() -> Bool` - Try to acquire lock without blocking

### Lock Class

RAII-style lock guard.

- `Lock::create(mutex: Mutex) -> Lock` - Create lock and acquire mutex
- `acquire()` - Manually acquire lock
- `release()` - Manually release lock

### Atomic Class

Atomic integer operations.

- `get() -> Integer` - Atomically read value
- `set(newValue: Integer)` - Atomically write value
- `increment() -> Integer` - Atomic increment, return new value
- `decrement() -> Integer` - Atomic decrement, return new value
- `compareAndSwap(expected: Integer, newValue: Integer) -> Bool` - Atomic CAS

## Date and Time (`Language::Time`)

### DateTime Class

Date and time manipulation.

**Factory Methods:**
- `DateTime::now() -> DateTime` - Get current date/time
- `DateTime::fromMilliseconds(milliseconds: Integer) -> DateTime` - From epoch
- `DateTime::fromSeconds(seconds: Integer) -> DateTime` - From epoch

**Conversion Methods:**
- `toMilliseconds() -> Integer` - Get milliseconds since epoch
- `toSeconds() -> Integer` - Get seconds since epoch
- `toString() -> String` - Format as "YYYY-MM-DD HH:MM:SS"

**Arithmetic Methods:**
- `addMilliseconds(milliseconds: Integer) -> DateTime`
- `addSeconds(seconds: Integer) -> DateTime`
- `addMinutes(minutes: Integer) -> DateTime`
- `addHours(hours: Integer) -> DateTime`
- `addDays(days: Integer) -> DateTime`

**Comparison Methods:**
- `difference(other: DateTime) -> Integer` - Difference in milliseconds
- `isBefore(other: DateTime) -> Bool`
- `isAfter(other: DateTime) -> Bool`

### TimeSpan Class

Time duration.

**Factory Methods:**
- `TimeSpan::fromMilliseconds(milliseconds: Integer) -> TimeSpan`
- `TimeSpan::fromSeconds/Minutes/Hours/Days(...) -> TimeSpan`

**Conversion Methods:**
- `toMilliseconds/Seconds/Minutes/Hours/Days() -> Integer`
- `toString() -> String` - Format as "HH:MM:SS"

**Arithmetic Methods:**
- `add(other: TimeSpan) -> TimeSpan`
- `subtract(other: TimeSpan) -> TimeSpan`

### Timer Class

Elapsed time measurement.

- `start()` - Start timer
- `stop()` - Stop timer
- `reset()` - Reset to zero
- `elapsed() -> Integer` - Get elapsed milliseconds
- `isRunning() -> Bool` - Check if running

## File I/O (`Language::IO`)

### File Class

File reading and writing operations.

**File Operations:**
- `open(filePath: String) -> Bool` - Open file
- `close()` - Close file
- `readAll() -> String` - Read entire file
- `readLine() -> String` - Read one line
- `readChar() -> String` - Read one character
- `hasMoreLines() -> Bool` - Check for more content
- `write(content: String) -> Bool` - Write to file
- `writeLine(content: String) -> Bool` - Write line
- `append(content: String) -> Bool` - Append to file

**File Information:**
- `exists() -> Bool` - Check if file exists
- `size() -> Integer` - Get file size
- `getPath() -> String` - Get file path

**Static Utility Methods:**
- `fileExists(filePath: String) -> Bool` - Check file existence
- `deleteFile(filePath: String) -> Bool` - Delete file
- `copyFile(source: String, dest: String) -> Bool` - Copy file
- `moveFile(source: String, dest: String) -> Bool` - Move/rename file

### Directory Class

Directory operations.

- `create(dirPath: String) -> Bool` - Create directory
- `delete(dirPath: String) -> Bool` - Delete directory
- `exists(dirPath: String) -> Bool` - Check if exists
- `listFiles(dirPath: String) -> StringList` - List files in directory
- `getWorkingDirectory() -> String` - Get current working directory
- `setWorkingDirectory(dirPath: String) -> Bool` - Change working directory

## Environment and System (`Environment` namespace in runtime)

### Environment Functions

- `Environment::getEnv(name: String) -> String` - Get environment variable
- `Environment::setEnv(name: String, value: String) -> Bool` - Set environment variable
- `Environment::sleep(milliseconds: Integer)` - Sleep for specified time
- `Environment::exit(code: Integer)` - Exit program with code

## Implementation Notes

- All collections are backed by C++ STL containers for performance
- File I/O uses C++ std::filesystem (C++17 required)
- Regex uses C++ std::regex
- Threading uses C++ std::thread and std::mutex
- DateTime uses C++ std::chrono
- Network HTTP is placeholder (requires libcurl or similar for full implementation)
- All XXML library files define interfaces; C++ runtime provides implementations
