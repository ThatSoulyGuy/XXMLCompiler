#ifndef XXML_LLVM_RUNTIME_H
#define XXML_LLVM_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// XXML LLVM Runtime Library
// Minimal C runtime for LLVM IR generated code
// ============================================

// ============================================
// Memory Management
// ============================================

void* xxml_malloc(size_t size);
void xxml_free(void* ptr);
void* xxml_memcpy(void* dest, const void* src, size_t n);
void* xxml_memset(void* ptr, int value, size_t n);

// Pointer operations (for manual memory manipulation)
void* xxml_ptr_read(void** ptr);
void xxml_ptr_write(void** ptr, void* value);
uint8_t xxml_read_byte(void* ptr);
void xxml_write_byte(void* ptr, uint8_t value);
int64_t xxml_int64_read(void* ptr);
void xxml_int64_write(void* ptr, int64_t value);

// ============================================
// Integer Operations
// ============================================

// Integer constructor (returns pointer to allocated Integer)
void* Integer_Constructor(int64_t value);
int64_t Integer_getValue(void* self);
void* Integer_add(void* self, void* other);
void* Integer_sub(void* self, void* other);
void* Integer_mul(void* self, void* other);
void* Integer_div(void* self, void* other);
void* Integer_negate(void* self);
void* Integer_mod(void* self, void* other);
bool Integer_eq(void* self, void* other);
bool Integer_ne(void* self, void* other);
bool Integer_lt(void* self, void* other);
bool Integer_le(void* self, void* other);
bool Integer_gt(void* self, void* other);
bool Integer_ge(void* self, void* other);
int64_t Integer_toInt64(void* self);
void* Integer_toString(void* self);
void* Integer_addAssign(void* self, void* other);
void* Integer_subtractAssign(void* self, void* other);
void* Integer_multiplyAssign(void* self, void* other);
void* Integer_divideAssign(void* self, void* other);
void* Integer_moduloAssign(void* self, void* other);

// ============================================
// Float Operations
// ============================================

void* Float_Constructor(float value);
float Float_getValue(void* self);
void* Float_toString(void* self);
char* xxml_float_to_string(float value);
void* Float_addAssign(void* self, void* other);
void* Float_subtractAssign(void* self, void* other);
void* Float_multiplyAssign(void* self, void* other);
void* Float_divideAssign(void* self, void* other);

// ============================================
// Double Operations
// ============================================

void* Double_Constructor(double value);
double Double_getValue(void* self);
void* Double_toString(void* self);
char* xxml_double_to_string(double value);
void* Double_addAssign(void* self, void* other);
void* Double_subtractAssign(void* self, void* other);
void* Double_multiplyAssign(void* self, void* other);
void* Double_divideAssign(void* self, void* other);

// ============================================
// String Operations
// ============================================

// String constructor (returns pointer to allocated String)
void* String_Constructor(const char* cstr);
void* String_FromCString(const char* cstr);
const char* String_toCString(void* self);
void* String_length(void* self);
bool String_isEmpty(void* self);
void* String_concat(void* self, void* other);
void* String_append(void* self, void* other);
bool String_equals(void* self, void* other);
void String_destroy(void* self);
void* String_copy(void* self);
void* String_charAt(void* self, void* indexObj);
void String_setCharAt(void* self, void* indexObj, void* charStr);
int64_t xxml_string_hash(void* self);

// ============================================
// Bool Operations
// ============================================

// Bool constructor (returns pointer to allocated Bool)
void* Bool_Constructor(bool value);
bool Bool_getValue(void* self);
void* Bool_and(void* self, void* other);
void* Bool_or(void* self, void* other);
void* Bool_not(void* self);
void* Bool_xor(void* self, void* other);
void* Bool_toInteger(void* self);

// ============================================
// None Operations (for void returns)
// ============================================

void* None_Constructor();

// ============================================
// List Operations
// ============================================

void* List_Constructor();
void List_add(void* self, void* item);
void* List_get(void* self, int64_t index);
size_t List_size(void* self);

// ============================================
// Console I/O
// ============================================

void Console_print(void* str);
void Console_printLine(void* str);
void Console_printInt(int64_t value);
void Console_printBool(bool value);

// ============================================
// Reflection Syscalls
// ============================================

// Type reflection syscalls
const char* Syscall_reflection_type_getName(void* typeInfo);
const char* Syscall_reflection_type_getFullName(void* typeInfo);
const char* Syscall_reflection_type_getNamespace(void* typeInfo);
int64_t Syscall_reflection_type_isTemplate(void* typeInfo);
int64_t Syscall_reflection_type_getTemplateParamCount(void* typeInfo);
int64_t Syscall_reflection_type_getPropertyCount(void* typeInfo);
void* Syscall_reflection_type_getProperty(void* typeInfo, int64_t index);
void* Syscall_reflection_type_getPropertyByName(void* typeInfo, const char* name);
int64_t Syscall_reflection_type_getMethodCount(void* typeInfo);
void* Syscall_reflection_type_getMethod(void* typeInfo, int64_t index);
void* Syscall_reflection_type_getMethodByName(void* typeInfo, const char* name);
void* Syscall_reflection_getTypeByName(const char* typeName);
int64_t Syscall_reflection_type_getInstanceSize(void* typeInfo);
const char* Syscall_reflection_type_getBaseClassName(void* typeInfo);
int64_t Syscall_reflection_type_hasBaseClass(void* typeInfo);
int64_t Syscall_reflection_type_getConstructorCount(void* typeInfo);
void* Syscall_reflection_type_getConstructor(void* typeInfo, int64_t index);

// Concurrency marker trait syscalls
// isSendable: returns 1 if type can be safely moved across thread boundaries
// A type is Sendable if all fields are Sendable and no reference (&) fields exist
int64_t Syscall_reflection_type_isSendable(void* typeInfo);
// isSharable: returns 1 if type can be safely shared across threads
// A type is Sharable if all fields are immutable/Sharable types
int64_t Syscall_reflection_type_isSharable(void* typeInfo);

// Method reflection syscalls
const char* Syscall_reflection_method_getName(void* methodInfo);
const char* Syscall_reflection_method_getReturnType(void* methodInfo);
int64_t Syscall_reflection_method_getReturnOwnership(void* methodInfo);
int64_t Syscall_reflection_method_getParameterCount(void* methodInfo);
void* Syscall_reflection_method_getParameter(void* methodInfo, int64_t index);
int64_t Syscall_reflection_method_isStatic(void* methodInfo);
int64_t Syscall_reflection_method_isConstructor(void* methodInfo);

// Property reflection syscalls
const char* Syscall_reflection_property_getName(void* propInfo);
const char* Syscall_reflection_property_getTypeName(void* propInfo);
int64_t Syscall_reflection_property_getOwnership(void* propInfo);
int64_t Syscall_reflection_property_getOffset(void* propInfo);

// Dynamic field access syscalls (ownership-aware)
// Gets pointer to the field value within an instance
void* Syscall_reflection_property_getValuePtr(void* propInfo, void* instance);
// Sets the pointer value at the field within an instance
void Syscall_reflection_property_setValuePtr(void* propInfo, void* instance, void* value);

// Parameter reflection syscalls
const char* Syscall_reflection_parameter_getName(void* paramInfo);
const char* Syscall_reflection_parameter_getTypeName(void* paramInfo);
int64_t Syscall_reflection_parameter_getOwnership(void* paramInfo);

// ============================================
// System Functions
// ============================================

void xxml_exit(int32_t code);

// ============================================
// Threading Functions
// ============================================

// Thread creation and management
// Creates a new thread that executes the given function with the given argument
// Returns thread handle (opaque pointer), or NULL on failure
void* xxml_Thread_create(void* (*func)(void*), void* arg);

// Wait for a thread to complete
// Returns 0 on success, non-zero on error
int64_t xxml_Thread_join(void* thread_handle);

// Detach a thread (let it run independently)
// Returns 0 on success, non-zero on error
int64_t xxml_Thread_detach(void* thread_handle);

// Check if thread is joinable (still running and not detached)
bool xxml_Thread_isJoinable(void* thread_handle);

// Sleep current thread for specified milliseconds
void xxml_Thread_sleep(int64_t milliseconds);

// Yield current thread's time slice
void xxml_Thread_yield(void);

// Get current thread ID (for debugging)
int64_t xxml_Thread_currentId(void);

// Spawn a thread running an XXML lambda
// The lambda closure must be a struct with function pointer as first field:
//   { void* (*fn)(void*), captured_vars... }
// Returns thread handle (opaque pointer), or NULL on failure
void* xxml_Thread_spawn_lambda(void* lambda_closure);

// ============================================
// Mutex Functions
// ============================================

// Create a new mutex
// Returns mutex handle (opaque pointer), or NULL on failure
void* xxml_Mutex_create(void);

// Destroy a mutex
void xxml_Mutex_destroy(void* mutex_handle);

// Lock mutex (blocking)
// Returns 0 on success, non-zero on error
int64_t xxml_Mutex_lock(void* mutex_handle);

// Unlock mutex
// Returns 0 on success, non-zero on error
int64_t xxml_Mutex_unlock(void* mutex_handle);

// Try to lock mutex (non-blocking)
// Returns true if lock acquired, false otherwise
bool xxml_Mutex_tryLock(void* mutex_handle);

// ============================================
// Condition Variable Functions
// ============================================

// Create a condition variable
void* xxml_CondVar_create(void);

// Destroy a condition variable
void xxml_CondVar_destroy(void* cond_handle);

// Wait on condition variable (must hold mutex)
int64_t xxml_CondVar_wait(void* cond_handle, void* mutex_handle);

// Wait on condition variable with timeout (milliseconds)
// Returns 0 if signaled, 1 if timed out, -1 on error
int64_t xxml_CondVar_waitTimeout(void* cond_handle, void* mutex_handle, int64_t timeout_ms);

// Signal one waiting thread
int64_t xxml_CondVar_signal(void* cond_handle);

// Signal all waiting threads
int64_t xxml_CondVar_broadcast(void* cond_handle);

// ============================================
// Atomic Integer Functions
// ============================================

// Create an atomic integer with initial value
void* xxml_Atomic_create(int64_t initial_value);

// Destroy an atomic integer
void xxml_Atomic_destroy(void* atomic_handle);

// Load value atomically
int64_t xxml_Atomic_load(void* atomic_handle);

// Store value atomically
void xxml_Atomic_store(void* atomic_handle, int64_t value);

// Add and return new value
int64_t xxml_Atomic_add(void* atomic_handle, int64_t value);

// Subtract and return new value
int64_t xxml_Atomic_sub(void* atomic_handle, int64_t value);

// Compare and swap: if current == expected, set to desired and return true
bool xxml_Atomic_compareAndSwap(void* atomic_handle, int64_t expected, int64_t desired);

// Exchange: set new value and return old value
int64_t xxml_Atomic_exchange(void* atomic_handle, int64_t new_value);

// ============================================
// Thread-Local Storage
// ============================================

// Create a thread-local storage key
void* xxml_TLS_create(void);

// Destroy a TLS key
void xxml_TLS_destroy(void* tls_key);

// Get thread-local value
void* xxml_TLS_get(void* tls_key);

// Set thread-local value
void xxml_TLS_set(void* tls_key, void* value);

// ============================================
// File I/O Functions
// ============================================

// File open modes (match standard C modes)
// "r"  - read only (file must exist)
// "w"  - write only (creates/truncates)
// "a"  - append (creates if doesn't exist)
// "r+" - read/write (file must exist)
// "w+" - read/write (creates/truncates)
// "a+" - read/append (creates if doesn't exist)
// "rb", "wb", "ab", etc. for binary mode

// Open a file, returns file handle or NULL on error
void* xxml_File_open(const char* path, const char* mode);

// Close a file
void xxml_File_close(void* file_handle);

// Read bytes from file, returns number of bytes read
int64_t xxml_File_read(void* file_handle, void* buffer, int64_t size);

// Write bytes to file, returns number of bytes written
int64_t xxml_File_write(void* file_handle, const void* buffer, int64_t size);

// Read a line from file (up to newline or EOF), returns String or NULL
void* xxml_File_readLine(void* file_handle);

// Write a string to file, returns bytes written
int64_t xxml_File_writeString(void* file_handle, const char* str);

// Write a line to file (appends newline), returns bytes written
int64_t xxml_File_writeLine(void* file_handle, const char* str);

// Read entire file contents as a String, returns String or NULL
void* xxml_File_readAll(void* file_handle);

// Seek position in file
// whence: 0=SEEK_SET (beginning), 1=SEEK_CUR (current), 2=SEEK_END (end)
// Returns 0 on success, -1 on error
int64_t xxml_File_seek(void* file_handle, int64_t offset, int64_t whence);

// Get current position in file
int64_t xxml_File_tell(void* file_handle);

// Get file size in bytes
int64_t xxml_File_size(void* file_handle);

// Check if at end of file
bool xxml_File_eof(void* file_handle);

// Flush file buffers
int64_t xxml_File_flush(void* file_handle);

// Check if file exists
bool xxml_File_exists(const char* path);

// Delete a file, returns true on success
bool xxml_File_delete(const char* path);

// Rename/move a file, returns true on success
bool xxml_File_rename(const char* old_path, const char* new_path);

// Copy a file, returns true on success
bool xxml_File_copy(const char* src_path, const char* dst_path);

// Get file size by path (without opening)
int64_t xxml_File_sizeByPath(const char* path);

// Read entire file content by path (returns String object)
void* xxml_File_readAllByPath(const char* path);

// ============================================
// Directory Functions
// ============================================

// Create a directory, returns true on success
bool xxml_Dir_create(const char* path);

// Check if directory exists
bool xxml_Dir_exists(const char* path);

// Delete an empty directory, returns true on success
bool xxml_Dir_delete(const char* path);

// Get current working directory
void* xxml_Dir_getCurrent(void);

// Set current working directory, returns true on success
bool xxml_Dir_setCurrent(const char* path);

// ============================================
// Path Utilities
// ============================================

// Join two path components
void* xxml_Path_join(const char* path1, const char* path2);

// Get the file name from a path
void* xxml_Path_getFileName(const char* path);

// Get the directory from a path
void* xxml_Path_getDirectory(const char* path);

// Get the file extension
void* xxml_Path_getExtension(const char* path);

// Check if path is absolute
bool xxml_Path_isAbsolute(const char* path);

// Get absolute path
void* xxml_Path_getAbsolute(const char* path);

// ============================================
// Utility Functions for Syscall Namespace
// ============================================

// Create a new String from a C string literal
void* xxml_string_create(const char* cstr);

// Concatenate two XXML String objects and return a new String
void* xxml_string_concat(void* str1, void* str2);

// Get the C string pointer from an XXML String
const char* xxml_string_cstr(void* str);

// Get the length of an XXML String
int64_t xxml_string_length(void* str);

// Copy an XXML String
void* xxml_string_copy(void* str);

// Check if two XXML Strings are equal
int64_t xxml_string_equals(void* str1, void* str2);

// Get character at index as a new single-character string
const char* xxml_string_charAt(void* str, int64_t index);

// Set character at index from another string (uses first character)
void xxml_string_setCharAt(void* str, int64_t index, void* charStr);

// Destroy an XXML String
void xxml_string_destroy(void* str);

// Check if a pointer is null (returns 1 if null, 0 otherwise)
int64_t xxml_ptr_is_null(void* ptr);

// Get a null pointer
void* xxml_ptr_null(void);

// ============================================
// Dynamic Value Methods (for __DynamicValue type)
// Used by processor code when target type is unknown at compile time
// ============================================

void* __DynamicValue_toString(void* self);
bool __DynamicValue_greaterThan(void* self, void* other);
bool __DynamicValue_lessThan(void* self, void* other);
bool __DynamicValue_equals(void* self, void* other);
void* __DynamicValue_add(void* self, void* other);
void* __DynamicValue_sub(void* self, void* other);
void* __DynamicValue_mul(void* self, void* other);
void* __DynamicValue_div(void* self, void* other);

#ifdef __cplusplus
}
#endif

#endif // XXML_LLVM_RUNTIME_H
