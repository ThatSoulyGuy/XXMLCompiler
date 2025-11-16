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
bool Integer_eq(void* self, void* other);
bool Integer_ne(void* self, void* other);
bool Integer_lt(void* self, void* other);
bool Integer_le(void* self, void* other);
bool Integer_gt(void* self, void* other);
bool Integer_ge(void* self, void* other);
int64_t Integer_toInt64(void* self);
void* Integer_toString(void* self);

// ============================================
// String Operations
// ============================================

// String constructor (returns pointer to allocated String)
void* String_Constructor(const char* cstr);
void* String_FromCString(const char* cstr);
const char* String_toCString(void* self);
size_t String_length(void* self);
void* String_concat(void* self, void* other);
void* String_append(void* self, void* other);
bool String_equals(void* self, void* other);
void String_destroy(void* self);

// ============================================
// Bool Operations
// ============================================

// Bool constructor (returns pointer to allocated Bool)
void* Bool_Constructor(bool value);
bool Bool_getValue(void* self);
void* Bool_and(void* self, void* other);
void* Bool_or(void* self, void* other);
void* Bool_not(void* self);

// ============================================
// None Operations (for void returns)
// ============================================

void* None_Constructor();

// ============================================
// Console I/O
// ============================================

void Console_print(void* str);
void Console_printLine(void* str);
void Console_printInt(int64_t value);
void Console_printBool(bool value);

// ============================================
// System Functions
// ============================================

void xxml_exit(int32_t code);

#ifdef __cplusplus
}
#endif

#endif // XXML_LLVM_RUNTIME_H
