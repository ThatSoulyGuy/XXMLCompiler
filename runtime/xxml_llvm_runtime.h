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

// Parameter reflection syscalls
const char* Syscall_reflection_parameter_getName(void* paramInfo);
const char* Syscall_reflection_parameter_getTypeName(void* paramInfo);
int64_t Syscall_reflection_parameter_getOwnership(void* paramInfo);

// ============================================
// System Functions
// ============================================

void xxml_exit(int32_t code);

#ifdef __cplusplus
}
#endif

#endif // XXML_LLVM_RUNTIME_H
