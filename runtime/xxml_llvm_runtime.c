#include "xxml_llvm_runtime.h"
#include "xxml_reflection_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Platform-specific includes for threading
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <process.h>
#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sched.h>
    #include <time.h>
    #include <errno.h>
#endif

// ============================================
// Memory Management
// ============================================

void* xxml_malloc(size_t size) {
    return malloc(size);
}

void xxml_free(void* ptr) {
    free(ptr);
}

void* xxml_memcpy(void* dest, const void* src, size_t n) {
    return memcpy(dest, src, n);
}

void* xxml_memset(void* ptr, int value, size_t n) {
    return memset(ptr, value, n);
}

// Pointer operations
void* xxml_ptr_read(void** ptr) {
    return *ptr;
}

void xxml_ptr_write(void** ptr, void* value) {
    *ptr = value;
}

uint8_t xxml_read_byte(void* ptr) {
    return *(uint8_t*)ptr;
}

void xxml_write_byte(void* ptr, uint8_t value) {
    *(uint8_t*)ptr = value;
}

// Int64 operations for reading/writing int64 from heap memory
int64_t xxml_int64_read(void* ptr) {
    return *(int64_t*)ptr;
}

void xxml_int64_write(void* ptr, int64_t value) {
    *(int64_t*)ptr = value;
}

// ============================================
// Forward declarations for cross-references
// ============================================
void* Bool_Constructor(bool value);

// ============================================
// Integer Operations
// ============================================

// Internal structure for Integer objects
typedef struct {
    int64_t value;
} Integer;

void* Integer_Constructor(int64_t value) {
    Integer* obj = (Integer*)xxml_malloc(sizeof(Integer));
    if (obj) {
        obj->value = value;
    }
    return obj;
}

int64_t Integer_getValue(void* self) {
    if (!self) return 0;
    return ((Integer*)self)->value;
}

void* Integer_add(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t result = ((Integer*)self)->value + ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_sub(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t result = ((Integer*)self)->value - ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_mul(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t result = ((Integer*)self)->value * ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_div(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t otherVal = ((Integer*)other)->value;
    if (otherVal == 0) {
        fprintf(stderr, "Error: Division by zero\n");
        exit(1);
    }
    int64_t result = ((Integer*)self)->value / otherVal;
    return Integer_Constructor(result);
}

void* Integer_negate(void* self) {
    if (!self) return NULL;
    int64_t result = -((Integer*)self)->value;
    return Integer_Constructor(result);
}

void* Integer_mod(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t otherVal = ((Integer*)other)->value;
    if (otherVal == 0) {
        fprintf(stderr, "Error: Modulo by zero\n");
        exit(1);
    }
    int64_t result = ((Integer*)self)->value % otherVal;
    return Integer_Constructor(result);
}

// Assignment operations (modify in place)
void* Integer_addAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Integer*)self)->value += ((Integer*)other)->value;
    return self;
}

void* Integer_subtractAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Integer*)self)->value -= ((Integer*)other)->value;
    return self;
}

void* Integer_multiplyAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Integer*)self)->value *= ((Integer*)other)->value;
    return self;
}

void* Integer_divideAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t otherVal = ((Integer*)other)->value;
    if (otherVal == 0) {
        fprintf(stderr, "Error: Division by zero\n");
        exit(1);
    }
    ((Integer*)self)->value /= otherVal;
    return self;
}

void* Integer_moduloAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    int64_t otherVal = ((Integer*)other)->value;
    if (otherVal == 0) {
        fprintf(stderr, "Error: Modulo by zero\n");
        exit(1);
    }
    ((Integer*)self)->value %= otherVal;
    return self;
}

bool Integer_eq(void* self, void* other) {
    if (!self || !other) return false;
    return ((Integer*)self)->value == ((Integer*)other)->value;
}

bool Integer_ne(void* self, void* other) {
    if (!self || !other) return true;
    return ((Integer*)self)->value != ((Integer*)other)->value;
}

bool Integer_lt(void* self, void* other) {
    if (!self || !other) return false;
    return ((Integer*)self)->value < ((Integer*)other)->value;
}

bool Integer_le(void* self, void* other) {
    if (!self || !other) return false;
    return ((Integer*)self)->value <= ((Integer*)other)->value;
}

bool Integer_gt(void* self, void* other) {
    if (!self || !other) return false;
    return ((Integer*)self)->value > ((Integer*)other)->value;
}

bool Integer_ge(void* self, void* other) {
    if (!self || !other) return false;
    return ((Integer*)self)->value >= ((Integer*)other)->value;
}

// Long-name aliases for comparison operations (called from codegen)
// These return Bool* objects instead of raw bool to match XXML semantics
void* Integer_equals(void* self, void* other) {
    return Bool_Constructor(Integer_eq(self, other));
}

void* Integer_notEquals(void* self, void* other) {
    return Bool_Constructor(Integer_ne(self, other));
}

void* Integer_lessThan(void* self, void* other) {
    return Bool_Constructor(Integer_lt(self, other));
}

void* Integer_greaterThan(void* self, void* other) {
    return Bool_Constructor(Integer_gt(self, other));
}

void* Integer_lessOrEqual(void* self, void* other) {
    return Bool_Constructor(Integer_le(self, other));
}

void* Integer_greaterOrEqual(void* self, void* other) {
    return Bool_Constructor(Integer_ge(self, other));
}

// Logical/bitwise operations
void* Integer_not(void* self) {
    if (!self) return Bool_Constructor(true);
    int64_t val = ((Integer*)self)->value;
    return Bool_Constructor(val == 0);
}

void* Integer_bitwiseAnd(void* self, void* other) {
    if (!self || !other) return Integer_Constructor(0);
    int64_t result = ((Integer*)self)->value & ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_bitwiseOr(void* self, void* other) {
    if (!self || !other) return Integer_Constructor(0);
    int64_t result = ((Integer*)self)->value | ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_bitwiseXor(void* self, void* other) {
    if (!self || !other) return Integer_Constructor(0);
    int64_t result = ((Integer*)self)->value ^ ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_bitwiseNot(void* self) {
    if (!self) return Integer_Constructor(~0LL);
    int64_t result = ~((Integer*)self)->value;
    return Integer_Constructor(result);
}

void* Integer_shiftLeft(void* self, void* other) {
    if (!self || !other) return Integer_Constructor(0);
    int64_t result = ((Integer*)self)->value << ((Integer*)other)->value;
    return Integer_Constructor(result);
}

void* Integer_shiftRight(void* self, void* other) {
    if (!self || !other) return Integer_Constructor(0);
    int64_t result = ((Integer*)self)->value >> ((Integer*)other)->value;
    return Integer_Constructor(result);
}

// Long-name aliases for arithmetic operations
void* Integer_subtract(void* self, void* other) {
    return Integer_sub(self, other);
}

void* Integer_multiply(void* self, void* other) {
    return Integer_mul(self, other);
}

void* Integer_divide(void* self, void* other) {
    return Integer_div(self, other);
}

void* Integer_modulo(void* self, void* other) {
    return Integer_mod(self, other);
}

void* Integer_abs(void* self) {
    if (!self) return Integer_Constructor(0);
    int64_t val = ((Integer*)self)->value;
    return Integer_Constructor(val < 0 ? -val : val);
}

int64_t Integer_toInt64(void* self) {
    return Integer_getValue(self);
}

// ============================================
// String Operations
// ============================================

// Internal structure for String objects
typedef struct {
    char* data;
    size_t length;
} String;

void* String_Constructor(const char* cstr) {
    String* obj = (String*)xxml_malloc(sizeof(String));
    if (!obj) return NULL;

    if (cstr) {
        obj->length = strlen(cstr);
        obj->data = (char*)xxml_malloc(obj->length + 1);
        if (obj->data) {
            strcpy(obj->data, cstr);
        } else {
            xxml_free(obj);
            return NULL;
        }
    } else {
        obj->length = 0;
        obj->data = (char*)xxml_malloc(1);
        if (obj->data) {
            obj->data[0] = '\0';
        } else {
            xxml_free(obj);
            return NULL;
        }
    }

    return obj;
}

void* String_FromCString(const char* cstr) {
    return String_Constructor(cstr);
}

const char* String_toCString(void* self) {
    if (!self) return "";
    return ((String*)self)->data;
}

void* String_length(void* self) {
    if (!self) return Integer_Constructor(0);
    return Integer_Constructor((int64_t)((String*)self)->length);
}

bool String_isEmpty(void* self) {
    if (!self) return true;
    return ((String*)self)->length == 0;
}

void* String_concat(void* self, void* other) {
    if (!self || !other) return NULL;

    String* str1 = (String*)self;
    String* str2 = (String*)other;

    size_t newLength = str1->length + str2->length;
    String* result = (String*)xxml_malloc(sizeof(String));
    if (!result) return NULL;

    result->length = newLength;
    result->data = (char*)xxml_malloc(newLength + 1);
    if (!result->data) {
        xxml_free(result);
        return NULL;
    }

    strcpy(result->data, str1->data);
    strcat(result->data, str2->data);

    return result;
}

void* String_append(void* self, void* other) {
    // String_append is the same as String_concat
    return String_concat(self, other);
}

bool String_equals(void* self, void* other) {
    if (!self || !other) return false;

    String* str1 = (String*)self;
    String* str2 = (String*)other;

    if (str1->length != str2->length) return false;

    return strcmp(str1->data, str2->data) == 0;
}

void String_destroy(void* self) {
    if (!self) return;

    String* str = (String*)self;
    if (str->data) {
        xxml_free(str->data);
    }
    xxml_free(str);
}

// Copy a string (return new String object)
void* String_copy(void* self) {
    if (!self) return String_Constructor("");
    String* str = (String*)self;
    return String_Constructor(str->data ? str->data : "");
}

// Get character at index as a new single-character string
// Takes Integer pointer for index
void* String_charAt(void* self, void* indexObj) {
    if (!self || !indexObj) return String_Constructor("");
    String* str = (String*)self;
    int64_t index = Integer_getValue(indexObj);
    if (index < 0 || (size_t)index >= str->length) return String_Constructor("");

    char buf[2];
    buf[0] = str->data[index];
    buf[1] = '\0';
    return String_Constructor(buf);
}

// Set character at index from another string (uses first character)
// Takes Integer pointer for index, String pointer for character
void String_setCharAt(void* self, void* indexObj, void* charStr) {
    if (!self || !indexObj || !charStr) return;
    String* str = (String*)self;
    String* ch = (String*)charStr;
    int64_t index = Integer_getValue(indexObj);
    if (index < 0 || (size_t)index >= str->length) return;
    if (ch->length == 0) return;

    str->data[index] = ch->data[0];
}

// djb2 hash algorithm for strings
int64_t xxml_string_hash(void* self) {
    if (!self) return 0;

    String* str = (String*)self;
    if (!str->data) return 0;

    const char* s = str->data;
    uint64_t hash = 5381;
    int c;

    while ((c = (unsigned char)*s++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    return (int64_t)hash;
}

// String replace - replaces all occurrences of pattern with replacement
// xxml_string_replace is the syscall version (called by XXML String.replace)
void* xxml_string_replace(void* self, void* pattern, void* replacement);

void* String_replace(void* self, void* pattern, void* replacement) {
    if (!self || !pattern || !replacement) return String_copy(self);

    String* str = (String*)self;
    String* pat = (String*)pattern;
    String* rep = (String*)replacement;

    if (!str->data || !pat->data || pat->length == 0) {
        return String_copy(self);
    }

    // Count occurrences to calculate result size
    size_t count = 0;
    const char* pos = str->data;
    while ((pos = strstr(pos, pat->data)) != NULL) {
        count++;
        pos += pat->length;
    }

    if (count == 0) {
        return String_copy(self);
    }

    // Calculate new length
    size_t repLen = rep->data ? rep->length : 0;
    size_t newLen = str->length + count * (repLen - pat->length);

    char* result = (char*)xxml_malloc(newLen + 1);
    if (!result) return String_copy(self);

    // Build result string
    char* dst = result;
    const char* src = str->data;
    while (*src) {
        if (strncmp(src, pat->data, pat->length) == 0) {
            if (rep->data && repLen > 0) {
                memcpy(dst, rep->data, repLen);
                dst += repLen;
            }
            src += pat->length;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    void* newStr = String_Constructor(result);
    xxml_free(result);
    return newStr;
}

// Syscall wrapper for String_replace (called by XXML String.replace method)
void* xxml_string_replace(void* self, void* pattern, void* replacement) {
    return String_replace(self, pattern, replacement);
}

// Wrap a String value as a StringLiteral AST JSON for Quote splice substitution
// Returns: {"kind":"StringLiteral","value":"escaped_string"}
void* xxml_splice_wrap_string(void* str) {
    if (!str) {
        return String_Constructor("{\"kind\":\"StringLiteral\",\"value\":\"\"}");
    }

    String* s = (String*)str;
    const char* data = s->data ? s->data : "";
    size_t dataLen = s->length;

    // Estimate escaped length (worst case: all chars need escaping)
    size_t bufSize = 64 + dataLen * 6;  // prefix + escaped content + suffix
    char* buffer = (char*)xxml_malloc(bufSize);
    if (!buffer) {
        return String_Constructor("{\"kind\":\"StringLiteral\",\"value\":\"\"}");
    }

    char* dst = buffer;

    // Write prefix
    const char* prefix = "{\"kind\":\"StringLiteral\",\"value\":\"";
    size_t prefixLen = strlen(prefix);
    memcpy(dst, prefix, prefixLen);
    dst += prefixLen;

    // Write escaped string value
    for (size_t i = 0; i < dataLen; i++) {
        char c = data[i];
        switch (c) {
            case '\"': *dst++ = '\\'; *dst++ = '\"'; break;
            case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
            case '\n': *dst++ = '\\'; *dst++ = 'n'; break;
            case '\r': *dst++ = '\\'; *dst++ = 'r'; break;
            case '\t': *dst++ = '\\'; *dst++ = 't'; break;
            default:
                if ((unsigned char)c < 32) {
                    // Control character - use \uXXXX
                    dst += sprintf(dst, "\\u%04x", (unsigned char)c);
                } else {
                    *dst++ = c;
                }
                break;
        }
    }

    // Write suffix
    const char* suffix = "\"}";
    memcpy(dst, suffix, 3);  // includes null terminator

    void* result = String_Constructor(buffer);
    xxml_free(buffer);
    return result;
}

// ============================================
// Bool Operations
// ============================================

// Internal structure for Bool objects
typedef struct {
    bool value;
} Bool;

void* Bool_Constructor(bool value) {
    Bool* obj = (Bool*)xxml_malloc(sizeof(Bool));
    if (obj) {
        obj->value = value;
    }
    return obj;
}

bool Bool_getValue(void* self) {
    if (!self) return false;
    return ((Bool*)self)->value;
}

void* Bool_and(void* self, void* other) {
    if (!self || !other) return Bool_Constructor(false);
    bool result = ((Bool*)self)->value && ((Bool*)other)->value;
    return Bool_Constructor(result);
}

void* Bool_or(void* self, void* other) {
    if (!self || !other) return Bool_Constructor(false);
    bool result = ((Bool*)self)->value || ((Bool*)other)->value;
    return Bool_Constructor(result);
}

void* Bool_not(void* self) {
    if (!self) return Bool_Constructor(true);
    bool result = !((Bool*)self)->value;
    return Bool_Constructor(result);
}

void* Bool_xor(void* self, void* other) {
    if (!self || !other) return Bool_Constructor(false);
    bool result = ((Bool*)self)->value != ((Bool*)other)->value;
    return Bool_Constructor(result);
}

void* Bool_toInteger(void* self) {
    if (!self) return Integer_Constructor(0);
    int64_t result = ((Bool*)self)->value ? 1 : 0;
    return Integer_Constructor(result);
}

// ============================================
// None Operations (for void returns)
// ============================================

void* None_Constructor() {
    // None represents "no value" / void
    // Just return null pointer
    return NULL;
}

// ============================================
// Console I/O
// ============================================

void Console_print(void* str) {
    if (!str) return;
    const char* cstr = String_toCString(str);
    printf("%s", cstr);
    fflush(stdout);
}

void Console_printLine(void* str) {
    if (!str) return;
    const char* cstr = String_toCString(str);
    printf("%s\n", cstr);
    fflush(stdout);
}

void Console_printInt(int64_t value) {
    printf("%lld", (long long)value);
    fflush(stdout);
}

void Console_printBool(bool value) {
    printf("%s", value ? "true" : "false");
    fflush(stdout);
}

// ============================================
// System Functions
// ============================================

void xxml_exit(int32_t code) {
    exit(code);
}

// ============================================
// Additional Integer Functions
// ============================================

void* Integer_toString(void* self) {
    Integer* obj = (Integer*)self;
    // Allocate buffer for string representation (max 20 chars for int64)
    char* buffer = (char*)malloc(21);
    snprintf(buffer, 21, "%lld", (long long)obj->value);
    return String_Constructor(buffer);
}

// ============================================
// Float Operations
// ============================================

// Internal structure for Float objects
typedef struct {
    float value;
} Float;

void* Float_Constructor(float value) {
    Float* obj = (Float*)xxml_malloc(sizeof(Float));
    if (obj) {
        obj->value = value;
    }
    return obj;
}

float Float_getValue(void* self) {
    if (!self) return 0.0f;
    return ((Float*)self)->value;
}

void* Float_toString(void* self) {
    Float* obj = (Float*)self;
    // Allocate buffer for string representation (enough for a float with precision)
    char* buffer = (char*)malloc(32);
    snprintf(buffer, 32, "%g", obj->value);
    return String_Constructor(buffer);
}

// Assignment operations (modify in place)
void* Float_addAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Float*)self)->value += ((Float*)other)->value;
    return self;
}

void* Float_subtractAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Float*)self)->value -= ((Float*)other)->value;
    return self;
}

void* Float_multiplyAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Float*)self)->value *= ((Float*)other)->value;
    return self;
}

void* Float_divideAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    float otherVal = ((Float*)other)->value;
    if (otherVal == 0.0f) {
        fprintf(stderr, "Error: Division by zero (float)\n");
        exit(1);
    }
    ((Float*)self)->value /= otherVal;
    return self;
}

// Syscall function for float to string conversion
char* xxml_float_to_string(float value) {
    char* buffer = (char*)malloc(32);
    snprintf(buffer, 32, "%g", value);
    return buffer;
}

// ============================================
// Double Operations
// ============================================

// Internal structure for Double objects
typedef struct {
    double value;
} Double;

void* Double_Constructor(double value) {
    Double* obj = (Double*)xxml_malloc(sizeof(Double));
    if (obj) {
        obj->value = value;
    }
    return obj;
}

double Double_getValue(void* self) {
    if (!self) return 0.0;
    return ((Double*)self)->value;
}

// Assignment operations (modify in place)
void* Double_addAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Double*)self)->value += ((Double*)other)->value;
    return self;
}

void* Double_subtractAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Double*)self)->value -= ((Double*)other)->value;
    return self;
}

void* Double_multiplyAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    ((Double*)self)->value *= ((Double*)other)->value;
    return self;
}

void* Double_divideAssign(void* self, void* other) {
    if (!self || !other) return NULL;
    double otherVal = ((Double*)other)->value;
    if (otherVal == 0.0) {
        fprintf(stderr, "Error: Division by zero (double)\n");
        exit(1);
    }
    ((Double*)self)->value /= otherVal;
    return self;
}

void* Double_toString(void* self) {
    Double* obj = (Double*)self;
    // Allocate buffer for string representation (enough for a double with precision)
    char* buffer = (char*)malloc(64);
    snprintf(buffer, 64, "%.15g", obj->value);
    return String_Constructor(buffer);
}

// Syscall function for double to string conversion
char* xxml_double_to_string(double value) {
    char* buffer = (char*)malloc(64);
    snprintf(buffer, 64, "%.15g", value);
    return buffer;
}

// ============================================
// List Functions (Simple Dynamic Array)
// ============================================

typedef struct {
    void** items;
    size_t count;
    size_t capacity;
} List;

void* List_Constructor() {
    List* list = (List*)malloc(sizeof(List));
    list->capacity = 16;
    list->count = 0;
    list->items = (void**)malloc(sizeof(void*) * list->capacity);
    return list;
}

void List_add(void* self, void* item) {
    List* list = (List*)self;
    if (list->count >= list->capacity) {
        // Grow the list using malloc + memcpy instead of realloc
        size_t newCapacity = list->capacity * 2;
        void** newItems = (void**)malloc(sizeof(void*) * newCapacity);
        memcpy(newItems, list->items, sizeof(void*) * list->count);
        free(list->items);
        list->items = newItems;
        list->capacity = newCapacity;
    }
    list->items[list->count++] = item;
}

void* List_get(void* self, int64_t index) {
    List* list = (List*)self;
    if (index < 0 || (size_t)index >= list->count) {
        fprintf(stderr, "List index out of bounds: %lld\n", (long long)index);
        exit(1);
    }
    return list->items[index];
}

size_t List_size(void* self) {
    List* list = (List*)self;
    return list->count;
}

// ============================================
// Reflection Syscalls Implementation
// ============================================

// Type reflection syscalls
const char* Syscall_reflection_type_getName(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->name : "";
}

const char* Syscall_reflection_type_getFullName(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->fullName : "";
}

const char* Syscall_reflection_type_getNamespace(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->namespaceName : "";
}

int64_t Syscall_reflection_type_isTemplate(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (info->isTemplate ? 1 : 0) : 0;
}

int64_t Syscall_reflection_type_getTemplateParamCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->templateParamCount : 0;
}

int64_t Syscall_reflection_type_getPropertyCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->propertyCount : 0;
}

void* Syscall_reflection_type_getProperty(void* typeInfo, int64_t index) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || index < 0 || index >= info->propertyCount) {
        return NULL;
    }
    return &info->properties[index];
}

void* Syscall_reflection_type_getPropertyByName(void* typeInfo, const char* name) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || !name) {
        return NULL;
    }
    
    for (int32_t i = 0; i < info->propertyCount; i++) {
        if (strcmp(info->properties[i].name, name) == 0) {
            return &info->properties[i];
        }
    }
    
    return NULL;
}

int64_t Syscall_reflection_type_getMethodCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->methodCount : 0;
}

void* Syscall_reflection_type_getMethod(void* typeInfo, int64_t index) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || index < 0 || index >= info->methodCount) {
        return NULL;
    }
    return &info->methods[index];
}

void* Syscall_reflection_type_getMethodByName(void* typeInfo, const char* name) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || !name) {
        return NULL;
    }
    
    for (int32_t i = 0; i < info->methodCount; i++) {
        if (strcmp(info->methods[i].name, name) == 0) {
            return &info->methods[i];
        }
    }
    
    return NULL;
}

void* Syscall_reflection_getTypeByName(const char* typeName) {
    return Reflection_getTypeInfo(typeName);
}

int64_t Syscall_reflection_type_getInstanceSize(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (int64_t)info->instanceSize : 0;
}

// Constructor reflection syscalls
int64_t Syscall_reflection_type_getConstructorCount(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? info->constructorCount : 0;
}

void* Syscall_reflection_type_getConstructor(void* typeInfo, int64_t index) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info || index < 0 || index >= info->constructorCount) {
        return NULL;
    }
    return &info->constructors[index];
}

const char* Syscall_reflection_type_getBaseClassName(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info ? (info->baseClassName ? info->baseClassName : "") : "";
}

int64_t Syscall_reflection_type_hasBaseClass(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    return info && info->baseClassName && info->baseClassName[0] != '\0' ? 1 : 0;
}

// Helper function to check if a type name is a known Sendable primitive
static int is_sendable_primitive(const char* typeName) {
    if (!typeName) return 0;
    // Check common primitive types that are always Sendable
    if (strcmp(typeName, "Integer") == 0 ||
        strcmp(typeName, "Bool") == 0 ||
        strcmp(typeName, "Float") == 0 ||
        strcmp(typeName, "Double") == 0 ||
        strcmp(typeName, "String") == 0 ||
        strcmp(typeName, "Char") == 0 ||
        strcmp(typeName, "Language::Core::Integer") == 0 ||
        strcmp(typeName, "Language::Core::Bool") == 0 ||
        strcmp(typeName, "Language::Core::Float") == 0 ||
        strcmp(typeName, "Language::Core::Double") == 0 ||
        strcmp(typeName, "Language::Core::String") == 0 ||
        strcmp(typeName, "Language::Core::Char") == 0) {
        return 1;
    }
    // NativeType is assumed Sendable (user responsibility for FFI)
    if (strncmp(typeName, "NativeType", 10) == 0) {
        return 1;
    }
    // Atomic<T> is always Sendable
    if (strncmp(typeName, "Atomic<", 7) == 0 ||
        strncmp(typeName, "Language::Concurrent::Atomic<", 29) == 0) {
        return 1;
    }
    return 0;
}

// Helper function to check if a type name is a known Sharable type
static int is_sharable_primitive(const char* typeName) {
    if (!typeName) return 0;
    // Immutable primitives are Sharable
    if (strcmp(typeName, "Integer") == 0 ||
        strcmp(typeName, "Bool") == 0 ||
        strcmp(typeName, "Float") == 0 ||
        strcmp(typeName, "Double") == 0 ||
        strcmp(typeName, "String") == 0 ||
        strcmp(typeName, "Char") == 0 ||
        strcmp(typeName, "Language::Core::Integer") == 0 ||
        strcmp(typeName, "Language::Core::Bool") == 0 ||
        strcmp(typeName, "Language::Core::Float") == 0 ||
        strcmp(typeName, "Language::Core::Double") == 0 ||
        strcmp(typeName, "Language::Core::String") == 0 ||
        strcmp(typeName, "Language::Core::Char") == 0) {
        return 1;
    }
    // Atomic<T> is thread-safe
    if (strncmp(typeName, "Atomic<", 7) == 0 ||
        strncmp(typeName, "Language::Concurrent::Atomic<", 29) == 0) {
        return 1;
    }
    // Sync primitives are Sharable
    if (strcmp(typeName, "Mutex") == 0 ||
        strcmp(typeName, "LockGuard") == 0 ||
        strcmp(typeName, "ConditionVariable") == 0 ||
        strcmp(typeName, "Semaphore") == 0 ||
        strcmp(typeName, "Language::Concurrent::Mutex") == 0 ||
        strcmp(typeName, "Language::Concurrent::LockGuard") == 0 ||
        strcmp(typeName, "Language::Concurrent::ConditionVariable") == 0 ||
        strcmp(typeName, "Language::Concurrent::Semaphore") == 0) {
        return 1;
    }
    return 0;
}

// Concurrency marker trait: isSendable
// A type is Sendable if all fields are Sendable and no reference (&) fields exist
int64_t Syscall_reflection_type_isSendable(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info) return 0;

    // Check if this type itself is a known Sendable primitive
    if (is_sendable_primitive(info->fullName) || is_sendable_primitive(info->name)) {
        return 1;
    }

    // Check all properties
    for (int32_t i = 0; i < info->propertyCount; i++) {
        ReflectionPropertyInfo* prop = &info->properties[i];

        // Reference (&) fields are NOT Sendable - they could become dangling
        // Ownership values: 0=None, 1=Owned(^), 2=Reference(&), 3=Copy(%)
        if (prop->ownership == 2) {  // Reference
            return 0;
        }

        // Check if the property's type is Sendable
        if (!is_sendable_primitive(prop->typeName)) {
            // Try to look up the type and check recursively
            ReflectionTypeInfo* propType = Reflection_getTypeInfo(prop->typeName);
            if (propType) {
                if (!Syscall_reflection_type_isSendable(propType)) {
                    return 0;
                }
            }
            // If type not found in registry, assume it might be Sendable (be permissive)
        }
    }

    return 1;
}

// Concurrency marker trait: isSharable
// A type is Sharable if all fields are immutable/Sharable types
int64_t Syscall_reflection_type_isSharable(void* typeInfo) {
    ReflectionTypeInfo* info = (ReflectionTypeInfo*)typeInfo;
    if (!info) return 0;

    // Check if this type itself is a known Sharable type
    if (is_sharable_primitive(info->fullName) || is_sharable_primitive(info->name)) {
        return 1;
    }

    // Check all properties - all must be Sharable types
    for (int32_t i = 0; i < info->propertyCount; i++) {
        ReflectionPropertyInfo* prop = &info->properties[i];

        // Check if the property's type is Sharable
        if (!is_sharable_primitive(prop->typeName)) {
            // Try to look up the type and check recursively
            ReflectionTypeInfo* propType = Reflection_getTypeInfo(prop->typeName);
            if (propType) {
                if (!Syscall_reflection_type_isSharable(propType)) {
                    return 0;
                }
            }
            // If type not found in registry, assume it might be Sharable (be permissive)
        }
    }

    return 1;
}

// Method reflection syscalls
const char* Syscall_reflection_method_getName(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->name : "";
}

const char* Syscall_reflection_method_getReturnType(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->returnType : "";
}

int64_t Syscall_reflection_method_getReturnOwnership(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->returnOwnership : 0;
}

int64_t Syscall_reflection_method_getParameterCount(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? info->parameterCount : 0;
}

void* Syscall_reflection_method_getParameter(void* methodInfo, int64_t index) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    if (!info || index < 0 || index >= info->parameterCount) {
        return NULL;
    }
    return &info->parameters[index];
}

int64_t Syscall_reflection_method_isStatic(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? (info->isStatic ? 1 : 0) : 0;
}

int64_t Syscall_reflection_method_isConstructor(void* methodInfo) {
    ReflectionMethodInfo* info = (ReflectionMethodInfo*)methodInfo;
    return info ? (info->isConstructor ? 1 : 0) : 0;
}

// Property reflection syscalls
const char* Syscall_reflection_property_getName(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->name : "";
}

const char* Syscall_reflection_property_getTypeName(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->typeName : "";
}

int64_t Syscall_reflection_property_getOwnership(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? info->ownership : 0;
}

int64_t Syscall_reflection_property_getOffset(void* propInfo) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    return info ? (int64_t)info->offset : 0;
}

// Dynamic field access syscalls
void* Syscall_reflection_property_getValuePtr(void* propInfo, void* instance) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    if (!info || !instance) {
        return NULL;
    }
    // Calculate field address: instance base + field offset
    // The field stores a pointer (void*), so we read and return that pointer
    void** fieldPtr = (void**)((char*)instance + info->offset);
    return *fieldPtr;
}

void Syscall_reflection_property_setValuePtr(void* propInfo, void* instance, void* value) {
    ReflectionPropertyInfo* info = (ReflectionPropertyInfo*)propInfo;
    if (!info || !instance) {
        return;
    }
    // Calculate field address: instance base + field offset
    // Write the new pointer value to the field
    void** fieldPtr = (void**)((char*)instance + info->offset);
    *fieldPtr = value;
}

// Parameter reflection syscalls
const char* Syscall_reflection_parameter_getName(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->name : "";
}

const char* Syscall_reflection_parameter_getTypeName(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->typeName : "";
}

int64_t Syscall_reflection_parameter_getOwnership(void* paramInfo) {
    ReflectionParameterInfo* info = (ReflectionParameterInfo*)paramInfo;
    return info ? info->ownership : 0;
}

// ============================================
// Language::Reflection Module Implementation
// These are the XXML class methods compiled to C
// ============================================

// Internal Type structure (mirrors XXML Language::Reflection::Type)
typedef struct {
    void* typeInfoPtr;
} Language_Reflection_Type;

// Language::Reflection::Type::Constructor
void* Language_Reflection_Type_Constructor(void* infoPtr) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)xxml_malloc(sizeof(Language_Reflection_Type));
    if (type) {
        type->typeInfoPtr = infoPtr;
    }
    return type;
}

// Language::Reflection::Type::forName (static method)
void* Language_Reflection_Type_forName(void* nameStr) {
    // nameStr is a String* object, get the C string
    const char* cstr = String_toCString(nameStr);
    void* typeInfo = xxml_reflection_getTypeByName(cstr);
    if (!typeInfo) {
        return NULL;  // None
    }
    return Language_Reflection_Type_Constructor(typeInfo);
}

// Language::Reflection::Type::getName
void* Language_Reflection_Type_getName(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return String_Constructor("");
    const char* name = xxml_reflection_type_getName(type->typeInfoPtr);
    return String_Constructor(name);
}

// Language::Reflection::Type::getFullName
void* Language_Reflection_Type_getFullName(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return String_Constructor("");
    const char* fullName = xxml_reflection_type_getFullName(type->typeInfoPtr);
    return String_Constructor(fullName);
}

// Language::Reflection::Type::getNamespace
void* Language_Reflection_Type_getNamespace(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return String_Constructor("");
    const char* ns = xxml_reflection_type_getNamespace(type->typeInfoPtr);
    return String_Constructor(ns);
}

// Language::Reflection::Type::isTemplate
void* Language_Reflection_Type_isTemplate(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Bool_Constructor(false);
    int64_t isTemplate = xxml_reflection_type_isTemplate(type->typeInfoPtr);
    return Bool_Constructor(isTemplate != 0);
}

// Language::Reflection::Type::getPropertyCount
void* Language_Reflection_Type_getPropertyCount(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Integer_Constructor(0);
    int64_t count = xxml_reflection_type_getPropertyCount(type->typeInfoPtr);
    return Integer_Constructor(count);
}

// Forward declaration for PropertyInfo constructor
void* Language_Reflection_PropertyInfo_Constructor(void* infoPtr);

// Language::Reflection::Type::getProperty (by name)
void* Language_Reflection_Type_getProperty(void* self, void* nameStr) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type || !nameStr) return NULL;
    const char* cstr = String_toCString(nameStr);
    void* propInfo = Syscall_reflection_type_getPropertyByName(type->typeInfoPtr, cstr);
    if (!propInfo) return NULL;
    return Language_Reflection_PropertyInfo_Constructor(propInfo);
}

// Language::Reflection::Type::getPropertyAt (by index)
void* Language_Reflection_Type_getPropertyAt(void* self, void* indexObj) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type || !indexObj) return NULL;
    int64_t index = Integer_getValue(indexObj);
    void* propInfo = Syscall_reflection_type_getProperty(type->typeInfoPtr, index);
    if (!propInfo) return NULL;
    return Language_Reflection_PropertyInfo_Constructor(propInfo);
}

// Language::Reflection::Type::getMethodCount
void* Language_Reflection_Type_getMethodCount(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Integer_Constructor(0);
    int64_t count = xxml_reflection_type_getMethodCount(type->typeInfoPtr);
    return Integer_Constructor(count);
}

// Language::Reflection::Type::getInstanceSize
void* Language_Reflection_Type_getInstanceSize(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Integer_Constructor(0);
    int64_t size = xxml_reflection_type_getInstanceSize(type->typeInfoPtr);
    return Integer_Constructor(size);
}

// Language::Reflection::Type::hasBaseType
void* Language_Reflection_Type_hasBaseType(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Bool_Constructor(false);
    int64_t hasBase = Syscall_reflection_type_hasBaseClass(type->typeInfoPtr);
    return Bool_Constructor(hasBase != 0);
}

// Language::Reflection::Type::getBaseTypeName
void* Language_Reflection_Type_getBaseTypeName(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return String_Constructor("");
    const char* baseName = Syscall_reflection_type_getBaseClassName(type->typeInfoPtr);
    return String_Constructor(baseName ? baseName : "");
}

// Language::Reflection::Type::getBaseType
void* Language_Reflection_Type_getBaseType(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return NULL;

    // Check if there's a base class
    int64_t hasBase = Syscall_reflection_type_hasBaseClass(type->typeInfoPtr);
    if (!hasBase) return NULL;

    // Get base class name and look it up
    const char* baseName = Syscall_reflection_type_getBaseClassName(type->typeInfoPtr);
    if (!baseName || baseName[0] == '\0') return NULL;

    void* baseTypeInfo = Reflection_getTypeInfo(baseName);
    if (!baseTypeInfo) return NULL;

    return Language_Reflection_Type_Constructor(baseTypeInfo);
}

// Language::Reflection::Type::getConstructorCount
void* Language_Reflection_Type_getConstructorCount(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Integer_Constructor(0);
    int64_t count = Syscall_reflection_type_getConstructorCount(type->typeInfoPtr);
    return Integer_Constructor(count);
}

// Forward declaration for MethodInfo constructor
void* Language_Reflection_MethodInfo_Constructor(void* infoPtr);

// Language::Reflection::Type::getConstructorAt
void* Language_Reflection_Type_getConstructorAt(void* self, void* indexObj) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type || !indexObj) return NULL;
    int64_t index = Integer_getValue(indexObj);
    void* ctorInfo = Syscall_reflection_type_getConstructor(type->typeInfoPtr, index);
    if (!ctorInfo) return NULL;
    return Language_Reflection_MethodInfo_Constructor(ctorInfo);
}

// Language::Reflection::Type::isSendable
void* Language_Reflection_Type_isSendable(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Bool_Constructor(false);
    int64_t result = Syscall_reflection_type_isSendable(type->typeInfoPtr);
    return Bool_Constructor(result != 0);
}

// Language::Reflection::Type::isSharable
void* Language_Reflection_Type_isSharable(void* self) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return Bool_Constructor(false);
    int64_t result = Syscall_reflection_type_isSharable(type->typeInfoPtr);
    return Bool_Constructor(result != 0);
}

// Internal PropertyInfo structure
typedef struct {
    void* propInfoPtr;
} Language_Reflection_PropertyInfo;

// Language::Reflection::PropertyInfo::Constructor
void* Language_Reflection_PropertyInfo_Constructor(void* infoPtr) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)xxml_malloc(sizeof(Language_Reflection_PropertyInfo));
    if (info) {
        info->propInfoPtr = infoPtr;
    }
    return info;
}

// Language::Reflection::PropertyInfo::getName
void* Language_Reflection_PropertyInfo_getName(void* self) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info) return String_Constructor("");
    const char* name = xxml_reflection_property_getName(info->propInfoPtr);
    return String_Constructor(name);
}

// Language::Reflection::PropertyInfo::getTypeName
void* Language_Reflection_PropertyInfo_getTypeName(void* self) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info) return String_Constructor("");
    const char* typeName = xxml_reflection_property_getTypeName(info->propInfoPtr);
    return String_Constructor(typeName);
}

// Language::Reflection::PropertyInfo::getOwnership
void* Language_Reflection_PropertyInfo_getOwnership(void* self) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info) return Integer_Constructor(0);
    int64_t ownership = Syscall_reflection_property_getOwnership(info->propInfoPtr);
    return Integer_Constructor(ownership);
}

// Language::Reflection::PropertyInfo::getOffset
void* Language_Reflection_PropertyInfo_getOffset(void* self) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info) return Integer_Constructor(0);
    int64_t offset = Syscall_reflection_property_getOffset(info->propInfoPtr);
    return Integer_Constructor(offset);
}

// Language::Reflection::PropertyInfo::getValuePtr
// Returns the pointer stored at this property's location in the instance
void* Language_Reflection_PropertyInfo_getValuePtr(void* self, void* instance) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info || !instance) return NULL;
    return Syscall_reflection_property_getValuePtr(info->propInfoPtr, instance);
}

// Language::Reflection::PropertyInfo::setValuePtr
// Sets the pointer value at this property's location in the instance
void Language_Reflection_PropertyInfo_setValuePtr(void* self, void* instance, void* value) {
    Language_Reflection_PropertyInfo* info = (Language_Reflection_PropertyInfo*)self;
    if (!info || !instance) return;
    Syscall_reflection_property_setValuePtr(info->propInfoPtr, instance, value);
}

// Internal MethodInfo structure
typedef struct {
    void* methodInfoPtr;
} Language_Reflection_MethodInfo;

// Language::Reflection::MethodInfo::Constructor
void* Language_Reflection_MethodInfo_Constructor(void* infoPtr) {
    Language_Reflection_MethodInfo* info = (Language_Reflection_MethodInfo*)xxml_malloc(sizeof(Language_Reflection_MethodInfo));
    if (info) {
        info->methodInfoPtr = infoPtr;
    }
    return info;
}

// Language::Reflection::MethodInfo::getName
void* Language_Reflection_MethodInfo_getName(void* self) {
    Language_Reflection_MethodInfo* info = (Language_Reflection_MethodInfo*)self;
    if (!info) return String_Constructor("");
    const char* name = xxml_reflection_method_getName(info->methodInfoPtr);
    return String_Constructor(name);
}

// Language::Reflection::MethodInfo::getReturnType
void* Language_Reflection_MethodInfo_getReturnType(void* self) {
    Language_Reflection_MethodInfo* info = (Language_Reflection_MethodInfo*)self;
    if (!info) return String_Constructor("");
    const char* retType = xxml_reflection_method_getReturnType(info->methodInfoPtr);
    return String_Constructor(retType);
}

// Language::Reflection::MethodInfo::getParameterCount
void* Language_Reflection_MethodInfo_getParameterCount(void* self) {
    Language_Reflection_MethodInfo* info = (Language_Reflection_MethodInfo*)self;
    if (!info) return Integer_Constructor(0);
    int64_t count = xxml_reflection_method_getParameterCount(info->methodInfoPtr);
    return Integer_Constructor(count);
}

// Language::Reflection::MethodInfo::isConstructor
void* Language_Reflection_MethodInfo_isConstructor(void* self) {
    Language_Reflection_MethodInfo* info = (Language_Reflection_MethodInfo*)self;
    if (!info) return Bool_Constructor(false);
    int64_t result = Syscall_reflection_method_isConstructor(info->methodInfoPtr);
    return Bool_Constructor(result != 0);
}

// Language::Reflection::Type::getMethodAt
void* Language_Reflection_Type_getMethodAt(void* self, void* index) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return NULL;
    int64_t idx = Integer_getValue(index);
    void* methodPtr = xxml_reflection_type_getMethod(type->typeInfoPtr, idx);
    if (!methodPtr) return NULL;
    return Language_Reflection_MethodInfo_Constructor(methodPtr);
}

// NOTE: Language::Reflection::GetType<T> template implementations are NOT included here
// because the compiler generates them. We only provide the base Type/PropertyInfo/MethodInfo
// class implementations which the compiler cannot generate due to AST corruption issues.

// ============================================
// Dynamic Value Methods (for processor __DynamicValue type)
// These provide runtime type dispatch for values whose types are unknown at compile time
// ============================================

// __DynamicValue_toString - calls toString on an XXML object
// For now, assumes the object is Integer-compatible (has toString method)
void* __DynamicValue_toString(void* self) {
    if (!self) return String_Constructor("null");
    // For simplicity, delegate to Integer_toString which works for Integer objects
    // A more complete implementation would check the runtime type
    return Integer_toString(self);
}

// __DynamicValue_greaterThan - compares two XXML objects
bool __DynamicValue_greaterThan(void* self, void* other) {
    if (!self || !other) return false;
    // Delegate to Integer_gt for integer comparison
    return Integer_gt(self, other);
}

// __DynamicValue_lessThan - compares two XXML objects
bool __DynamicValue_lessThan(void* self, void* other) {
    if (!self || !other) return false;
    return Integer_lt(self, other);
}

// __DynamicValue_equals - compares two XXML objects for equality
bool __DynamicValue_equals(void* self, void* other) {
    if (!self || !other) return self == other;
    return Integer_eq(self, other);
}

// __DynamicValue_add - adds two XXML objects
void* __DynamicValue_add(void* self, void* other) {
    if (!self || !other) return NULL;
    return Integer_add(self, other);
}

// __DynamicValue_sub - subtracts two XXML objects
void* __DynamicValue_sub(void* self, void* other) {
    if (!self || !other) return NULL;
    return Integer_sub(self, other);
}

// __DynamicValue_mul - multiplies two XXML objects
void* __DynamicValue_mul(void* self, void* other) {
    if (!self || !other) return NULL;
    return Integer_mul(self, other);
}

// __DynamicValue_div - divides two XXML objects
void* __DynamicValue_div(void* self, void* other) {
    if (!self || !other) return NULL;
    return Integer_div(self, other);
}

// ============================================
// Threading Implementation
// ============================================

#ifdef _WIN32

// Windows thread wrapper structure
typedef struct {
    void* (*func)(void*);
    void* arg;
    void* result;
    HANDLE handle;
    bool joinable;
} ThreadInfo;

// Windows thread entry point
static unsigned __stdcall win_thread_entry(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
    info->result = info->func(info->arg);
    return 0;
}

void* xxml_Thread_create(void* (*func)(void*), void* arg) {
    ThreadInfo* info = (ThreadInfo*)xxml_malloc(sizeof(ThreadInfo));
    if (!info) return NULL;

    info->func = func;
    info->arg = arg;
    info->result = NULL;
    info->joinable = true;

    info->handle = (HANDLE)_beginthreadex(NULL, 0, win_thread_entry, info, 0, NULL);
    if (info->handle == 0) {
        xxml_free(info);
        return NULL;
    }

    return info;
}

int64_t xxml_Thread_join(void* thread_handle) {
    if (!thread_handle) return -1;
    ThreadInfo* info = (ThreadInfo*)thread_handle;

    if (!info->joinable) return -1;

    DWORD result = WaitForSingleObject(info->handle, INFINITE);
    if (result != WAIT_OBJECT_0) return -1;

    CloseHandle(info->handle);
    info->joinable = false;
    xxml_free(info);
    return 0;
}

int64_t xxml_Thread_detach(void* thread_handle) {
    if (!thread_handle) return -1;
    ThreadInfo* info = (ThreadInfo*)thread_handle;

    if (!info->joinable) return -1;

    CloseHandle(info->handle);
    info->joinable = false;
    // Note: memory will leak, but detached threads are fire-and-forget
    return 0;
}

bool xxml_Thread_isJoinable(void* thread_handle) {
    if (!thread_handle) return false;
    ThreadInfo* info = (ThreadInfo*)thread_handle;
    return info->joinable;
}

void xxml_Thread_sleep(int64_t milliseconds) {
    Sleep((DWORD)milliseconds);
}

void xxml_Thread_yield(void) {
    SwitchToThread();
}

int64_t xxml_Thread_currentId(void) {
    return (int64_t)GetCurrentThreadId();
}

// Lambda trampoline for thread spawning
// XXML lambda closures have the function pointer as their first field
typedef void* (*LambdaFn)(void*);

static void* lambda_trampoline(void* closure) {
    // Extract function pointer from first field of closure struct
    LambdaFn fn = *(LambdaFn*)closure;
    // Call the lambda with its closure
    return fn(closure);
}

void* xxml_Thread_spawn_lambda(void* lambda_closure) {
    if (!lambda_closure) return NULL;
    return xxml_Thread_create(lambda_trampoline, lambda_closure);
}

// Windows Mutex implementation
void* xxml_Mutex_create(void) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)xxml_malloc(sizeof(CRITICAL_SECTION));
    if (!cs) return NULL;
    InitializeCriticalSection(cs);
    return cs;
}

void xxml_Mutex_destroy(void* mutex_handle) {
    if (!mutex_handle) return;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;
    DeleteCriticalSection(cs);
    xxml_free(cs);
}

int64_t xxml_Mutex_lock(void* mutex_handle) {
    if (!mutex_handle) return -1;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;
    EnterCriticalSection(cs);
    return 0;
}

int64_t xxml_Mutex_unlock(void* mutex_handle) {
    if (!mutex_handle) return -1;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;
    LeaveCriticalSection(cs);
    return 0;
}

bool xxml_Mutex_tryLock(void* mutex_handle) {
    if (!mutex_handle) return false;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;
    return TryEnterCriticalSection(cs) != 0;
}

// Windows Condition Variable implementation
void* xxml_CondVar_create(void) {
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)xxml_malloc(sizeof(CONDITION_VARIABLE));
    if (!cv) return NULL;
    InitializeConditionVariable(cv);
    return cv;
}

void xxml_CondVar_destroy(void* cond_handle) {
    if (!cond_handle) return;
    // Windows condition variables don't need explicit destruction
    xxml_free(cond_handle);
}

int64_t xxml_CondVar_wait(void* cond_handle, void* mutex_handle) {
    if (!cond_handle || !mutex_handle) return -1;
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)cond_handle;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;

    if (!SleepConditionVariableCS(cv, cs, INFINITE)) {
        return -1;
    }
    return 0;
}

int64_t xxml_CondVar_waitTimeout(void* cond_handle, void* mutex_handle, int64_t timeout_ms) {
    if (!cond_handle || !mutex_handle) return -1;
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)cond_handle;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex_handle;

    if (!SleepConditionVariableCS(cv, cs, (DWORD)timeout_ms)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return 1;  // Timeout
        }
        return -1;  // Error
    }
    return 0;  // Signaled
}

int64_t xxml_CondVar_signal(void* cond_handle) {
    if (!cond_handle) return -1;
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)cond_handle;
    WakeConditionVariable(cv);
    return 0;
}

int64_t xxml_CondVar_broadcast(void* cond_handle) {
    if (!cond_handle) return -1;
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)cond_handle;
    WakeAllConditionVariable(cv);
    return 0;
}

// Windows TLS implementation
void* xxml_TLS_create(void) {
    DWORD* key = (DWORD*)xxml_malloc(sizeof(DWORD));
    if (!key) return NULL;
    *key = TlsAlloc();
    if (*key == TLS_OUT_OF_INDEXES) {
        xxml_free(key);
        return NULL;
    }
    return key;
}

void xxml_TLS_destroy(void* tls_key) {
    if (!tls_key) return;
    DWORD* key = (DWORD*)tls_key;
    TlsFree(*key);
    xxml_free(key);
}

void* xxml_TLS_get(void* tls_key) {
    if (!tls_key) return NULL;
    DWORD* key = (DWORD*)tls_key;
    return TlsGetValue(*key);
}

void xxml_TLS_set(void* tls_key, void* value) {
    if (!tls_key) return;
    DWORD* key = (DWORD*)tls_key;
    TlsSetValue(*key, value);
}

#else  // POSIX implementation

// POSIX thread wrapper structure
typedef struct {
    pthread_t thread;
    bool joinable;
} ThreadInfo;

void* xxml_Thread_create(void* (*func)(void*), void* arg) {
    ThreadInfo* info = (ThreadInfo*)xxml_malloc(sizeof(ThreadInfo));
    if (!info) return NULL;

    info->joinable = true;

    int result = pthread_create(&info->thread, NULL, func, arg);
    if (result != 0) {
        xxml_free(info);
        return NULL;
    }

    return info;
}

int64_t xxml_Thread_join(void* thread_handle) {
    if (!thread_handle) return -1;
    ThreadInfo* info = (ThreadInfo*)thread_handle;

    if (!info->joinable) return -1;

    void* result;
    int ret = pthread_join(info->thread, &result);
    if (ret != 0) return -1;

    info->joinable = false;
    xxml_free(info);
    return 0;
}

int64_t xxml_Thread_detach(void* thread_handle) {
    if (!thread_handle) return -1;
    ThreadInfo* info = (ThreadInfo*)thread_handle;

    if (!info->joinable) return -1;

    int ret = pthread_detach(info->thread);
    if (ret != 0) return -1;

    info->joinable = false;
    return 0;
}

bool xxml_Thread_isJoinable(void* thread_handle) {
    if (!thread_handle) return false;
    ThreadInfo* info = (ThreadInfo*)thread_handle;
    return info->joinable;
}

void xxml_Thread_sleep(int64_t milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void xxml_Thread_yield(void) {
    sched_yield();
}

int64_t xxml_Thread_currentId(void) {
    return (int64_t)pthread_self();
}

// Lambda trampoline for thread spawning (POSIX)
typedef void* (*LambdaFnPosix)(void*);

static void* lambda_trampoline_posix(void* closure) {
    // Extract function pointer from first field of closure struct
    LambdaFnPosix fn = *(LambdaFnPosix*)closure;
    // Call the lambda with its closure
    return fn(closure);
}

void* xxml_Thread_spawn_lambda(void* lambda_closure) {
    if (!lambda_closure) return NULL;
    return xxml_Thread_create(lambda_trampoline_posix, lambda_closure);
}

// POSIX Mutex implementation
void* xxml_Mutex_create(void) {
    pthread_mutex_t* mutex = (pthread_mutex_t*)xxml_malloc(sizeof(pthread_mutex_t));
    if (!mutex) return NULL;

    if (pthread_mutex_init(mutex, NULL) != 0) {
        xxml_free(mutex);
        return NULL;
    }
    return mutex;
}

void xxml_Mutex_destroy(void* mutex_handle) {
    if (!mutex_handle) return;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;
    pthread_mutex_destroy(mutex);
    xxml_free(mutex);
}

int64_t xxml_Mutex_lock(void* mutex_handle) {
    if (!mutex_handle) return -1;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;
    return pthread_mutex_lock(mutex) == 0 ? 0 : -1;
}

int64_t xxml_Mutex_unlock(void* mutex_handle) {
    if (!mutex_handle) return -1;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;
    return pthread_mutex_unlock(mutex) == 0 ? 0 : -1;
}

bool xxml_Mutex_tryLock(void* mutex_handle) {
    if (!mutex_handle) return false;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;
    return pthread_mutex_trylock(mutex) == 0;
}

// POSIX Condition Variable implementation
void* xxml_CondVar_create(void) {
    pthread_cond_t* cond = (pthread_cond_t*)xxml_malloc(sizeof(pthread_cond_t));
    if (!cond) return NULL;

    if (pthread_cond_init(cond, NULL) != 0) {
        xxml_free(cond);
        return NULL;
    }
    return cond;
}

void xxml_CondVar_destroy(void* cond_handle) {
    if (!cond_handle) return;
    pthread_cond_t* cond = (pthread_cond_t*)cond_handle;
    pthread_cond_destroy(cond);
    xxml_free(cond);
}

int64_t xxml_CondVar_wait(void* cond_handle, void* mutex_handle) {
    if (!cond_handle || !mutex_handle) return -1;
    pthread_cond_t* cond = (pthread_cond_t*)cond_handle;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;

    return pthread_cond_wait(cond, mutex) == 0 ? 0 : -1;
}

int64_t xxml_CondVar_waitTimeout(void* cond_handle, void* mutex_handle, int64_t timeout_ms) {
    if (!cond_handle || !mutex_handle) return -1;
    pthread_cond_t* cond = (pthread_cond_t*)cond_handle;
    pthread_mutex_t* mutex = (pthread_mutex_t*)mutex_handle;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    int ret = pthread_cond_timedwait(cond, mutex, &ts);
    if (ret == 0) return 0;  // Signaled
    if (ret == ETIMEDOUT) return 1;  // Timeout
    return -1;  // Error
}

int64_t xxml_CondVar_signal(void* cond_handle) {
    if (!cond_handle) return -1;
    pthread_cond_t* cond = (pthread_cond_t*)cond_handle;
    return pthread_cond_signal(cond) == 0 ? 0 : -1;
}

int64_t xxml_CondVar_broadcast(void* cond_handle) {
    if (!cond_handle) return -1;
    pthread_cond_t* cond = (pthread_cond_t*)cond_handle;
    return pthread_cond_broadcast(cond) == 0 ? 0 : -1;
}

// POSIX TLS implementation
void* xxml_TLS_create(void) {
    pthread_key_t* key = (pthread_key_t*)xxml_malloc(sizeof(pthread_key_t));
    if (!key) return NULL;

    if (pthread_key_create(key, NULL) != 0) {
        xxml_free(key);
        return NULL;
    }
    return key;
}

void xxml_TLS_destroy(void* tls_key) {
    if (!tls_key) return;
    pthread_key_t* key = (pthread_key_t*)tls_key;
    pthread_key_delete(*key);
    xxml_free(key);
}

void* xxml_TLS_get(void* tls_key) {
    if (!tls_key) return NULL;
    pthread_key_t* key = (pthread_key_t*)tls_key;
    return pthread_getspecific(*key);
}

void xxml_TLS_set(void* tls_key, void* value) {
    if (!tls_key) return;
    pthread_key_t* key = (pthread_key_t*)tls_key;
    pthread_setspecific(*key, value);
}

#endif  // _WIN32 / POSIX

// ============================================
// Atomic Integer Implementation (Cross-platform using intrinsics)
// ============================================

#ifdef _WIN32
#include <intrin.h>
#endif

typedef struct {
    volatile int64_t value;
} AtomicInt64;

void* xxml_Atomic_create(int64_t initial_value) {
    AtomicInt64* atomic = (AtomicInt64*)xxml_malloc(sizeof(AtomicInt64));
    if (!atomic) return NULL;
    atomic->value = initial_value;
    return atomic;
}

void xxml_Atomic_destroy(void* atomic_handle) {
    if (atomic_handle) {
        xxml_free(atomic_handle);
    }
}

int64_t xxml_Atomic_load(void* atomic_handle) {
    if (!atomic_handle) return 0;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    return InterlockedCompareExchange64(&atomic->value, 0, 0);
#else
    return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
#endif
}

void xxml_Atomic_store(void* atomic_handle, int64_t value) {
    if (!atomic_handle) return;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    InterlockedExchange64(&atomic->value, value);
#else
    __atomic_store_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t xxml_Atomic_add(void* atomic_handle, int64_t value) {
    if (!atomic_handle) return 0;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    return InterlockedAdd64(&atomic->value, value);
#else
    return __atomic_add_fetch(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t xxml_Atomic_sub(void* atomic_handle, int64_t value) {
    if (!atomic_handle) return 0;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    return InterlockedAdd64(&atomic->value, -value);
#else
    return __atomic_sub_fetch(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

bool xxml_Atomic_compareAndSwap(void* atomic_handle, int64_t expected, int64_t desired) {
    if (!atomic_handle) return false;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    return InterlockedCompareExchange64(&atomic->value, desired, expected) == expected;
#else
    return __atomic_compare_exchange_n(&atomic->value, &expected, desired,
                                        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
}

int64_t xxml_Atomic_exchange(void* atomic_handle, int64_t new_value) {
    if (!atomic_handle) return 0;
    AtomicInt64* atomic = (AtomicInt64*)atomic_handle;
#ifdef _WIN32
    return InterlockedExchange64(&atomic->value, new_value);
#else
    return __atomic_exchange_n(&atomic->value, new_value, __ATOMIC_SEQ_CST);
#endif
}

// ============================================
// File I/O Implementation
// ============================================

#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
#endif

void* xxml_File_open(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    return (void*)fopen(path, mode);
}

void xxml_File_close(void* file_handle) {
    if (file_handle) {
        fclose((FILE*)file_handle);
    }
}

int64_t xxml_File_read(void* file_handle, void* buffer, int64_t size) {
    if (!file_handle || !buffer || size <= 0) return 0;
    return (int64_t)fread(buffer, 1, (size_t)size, (FILE*)file_handle);
}

int64_t xxml_File_write(void* file_handle, const void* buffer, int64_t size) {
    if (!file_handle || !buffer || size <= 0) return 0;
    return (int64_t)fwrite(buffer, 1, (size_t)size, (FILE*)file_handle);
}

void* xxml_File_readLine(void* file_handle) {
    if (!file_handle) return NULL;

    FILE* fp = (FILE*)file_handle;

    // Dynamic buffer for reading line
    size_t capacity = 256;
    size_t length = 0;
    char* buffer = (char*)xxml_malloc(capacity);
    if (!buffer) return NULL;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        // Grow buffer if needed
        if (length + 2 >= capacity) {
            capacity *= 2;
            char* new_buffer = (char*)xxml_malloc(capacity);
            if (!new_buffer) {
                xxml_free(buffer);
                return NULL;
            }
            memcpy(new_buffer, buffer, length);
            xxml_free(buffer);
            buffer = new_buffer;
        }

        if (c == '\n') {
            break;  // Don't include newline in result
        }
        if (c == '\r') {
            // Handle Windows line endings (\r\n)
            int next = fgetc(fp);
            if (next != '\n' && next != EOF) {
                ungetc(next, fp);
            }
            break;
        }

        buffer[length++] = (char)c;
    }

    if (length == 0 && c == EOF) {
        xxml_free(buffer);
        return NULL;  // EOF reached with no data
    }

    buffer[length] = '\0';
    void* result = String_Constructor(buffer);
    xxml_free(buffer);
    return result;
}

int64_t xxml_File_writeString(void* file_handle, const char* str) {
    if (!file_handle || !str) return 0;
    size_t len = strlen(str);
    return (int64_t)fwrite(str, 1, len, (FILE*)file_handle);
}

int64_t xxml_File_writeLine(void* file_handle, const char* str) {
    if (!file_handle) return 0;
    int64_t written = 0;
    if (str) {
        written = xxml_File_writeString(file_handle, str);
    }
    written += (int64_t)fwrite("\n", 1, 1, (FILE*)file_handle);
    return written;
}

void* xxml_File_readAll(void* file_handle) {
    if (!file_handle) return NULL;

    FILE* fp = (FILE*)file_handle;

    // Get file size
    long current_pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, current_pos, SEEK_SET);

    if (file_size <= 0) {
        return String_Constructor("");
    }

    // Read remaining content
    long remaining = file_size - current_pos;
    char* buffer = (char*)xxml_malloc((size_t)remaining + 1);
    if (!buffer) return NULL;

    size_t bytes_read = fread(buffer, 1, (size_t)remaining, fp);
    buffer[bytes_read] = '\0';

    void* result = String_Constructor(buffer);
    xxml_free(buffer);
    return result;
}

int64_t xxml_File_seek(void* file_handle, int64_t offset, int64_t whence) {
    if (!file_handle) return -1;
    int seek_whence;
    switch (whence) {
        case 0: seek_whence = SEEK_SET; break;
        case 1: seek_whence = SEEK_CUR; break;
        case 2: seek_whence = SEEK_END; break;
        default: return -1;
    }
    return (int64_t)fseek((FILE*)file_handle, (long)offset, seek_whence);
}

int64_t xxml_File_tell(void* file_handle) {
    if (!file_handle) return -1;
    return (int64_t)ftell((FILE*)file_handle);
}

int64_t xxml_File_size(void* file_handle) {
    if (!file_handle) return -1;

    FILE* fp = (FILE*)file_handle;
    long current_pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, current_pos, SEEK_SET);

    return (int64_t)size;
}

bool xxml_File_eof(void* file_handle) {
    if (!file_handle) return true;
    return feof((FILE*)file_handle) != 0;
}

int64_t xxml_File_flush(void* file_handle) {
    if (!file_handle) return -1;
    return (int64_t)fflush((FILE*)file_handle);
}

bool xxml_File_exists(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

bool xxml_File_delete(const char* path) {
    if (!path) return false;
    return remove(path) == 0;
}

bool xxml_File_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return false;
    return rename(old_path, new_path) == 0;
}

bool xxml_File_copy(const char* src_path, const char* dst_path) {
    if (!src_path || !dst_path) return false;

    FILE* src = fopen(src_path, "rb");
    if (!src) return false;

    FILE* dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buffer[8192];
    size_t bytes_read;
    bool success = true;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            success = false;
            break;
        }
    }

    fclose(src);
    fclose(dst);

    if (!success) {
        remove(dst_path);  // Clean up partial file
    }

    return success;
}

int64_t xxml_File_sizeByPath(const char* path) {
    if (!path) return -1;

    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);

    return (int64_t)size;
}

void* xxml_File_readAllByPath(const char* path) {
    if (!path) return String_Constructor("");

    FILE* fp = fopen(path, "rb");
    if (!fp) return String_Constructor("");

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return String_Constructor("");
    }

    // Read entire content
    char* buffer = (char*)xxml_malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(fp);
        return String_Constructor("");
    }

    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);

    void* result = String_Constructor(buffer);
    xxml_free(buffer);
    return result;
}

// ============================================
// Directory Implementation
// ============================================

bool xxml_Dir_create(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

bool xxml_Dir_exists(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

bool xxml_Dir_delete(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _rmdir(path) == 0;
#else
    return rmdir(path) == 0;
#endif
}

void* xxml_Dir_getCurrent(void) {
    char buffer[4096];
#ifdef _WIN32
    if (_getcwd(buffer, sizeof(buffer)) != NULL) {
        return String_Constructor(buffer);
    }
#else
    if (getcwd(buffer, sizeof(buffer)) != NULL) {
        return String_Constructor(buffer);
    }
#endif
    return String_Constructor("");
}

bool xxml_Dir_setCurrent(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    return _chdir(path) == 0;
#else
    return chdir(path) == 0;
#endif
}

// ============================================
// Path Utilities Implementation
// ============================================

void* xxml_Path_join(const char* path1, const char* path2) {
    if (!path1 && !path2) return String_Constructor("");
    if (!path1) return String_Constructor(path2);
    if (!path2) return String_Constructor(path1);

    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);

    // Check if we need to add separator
    bool need_sep = (len1 > 0 && path1[len1-1] != PATH_SEPARATOR &&
                     path1[len1-1] != '/' && path1[len1-1] != '\\');

    size_t total_len = len1 + len2 + (need_sep ? 1 : 0) + 1;
    char* result = (char*)xxml_malloc(total_len);
    if (!result) return String_Constructor("");

    strcpy(result, path1);
    if (need_sep) {
        strcat(result, PATH_SEPARATOR_STR);
    }
    strcat(result, path2);

    void* str = String_Constructor(result);
    xxml_free(result);
    return str;
}

void* xxml_Path_getFileName(const char* path) {
    if (!path) return String_Constructor("");

    const char* last_sep = strrchr(path, PATH_SEPARATOR);
    const char* last_fwd = strrchr(path, '/');
    const char* last_back = strrchr(path, '\\');

    // Find the rightmost separator
    const char* sep = last_sep;
    if (last_fwd && (!sep || last_fwd > sep)) sep = last_fwd;
    if (last_back && (!sep || last_back > sep)) sep = last_back;

    if (sep) {
        return String_Constructor(sep + 1);
    }
    return String_Constructor(path);
}

void* xxml_Path_getDirectory(const char* path) {
    if (!path) return String_Constructor("");

    const char* last_sep = strrchr(path, PATH_SEPARATOR);
    const char* last_fwd = strrchr(path, '/');
    const char* last_back = strrchr(path, '\\');

    // Find the rightmost separator
    const char* sep = last_sep;
    if (last_fwd && (!sep || last_fwd > sep)) sep = last_fwd;
    if (last_back && (!sep || last_back > sep)) sep = last_back;

    if (sep) {
        size_t len = sep - path;
        char* result = (char*)xxml_malloc(len + 1);
        if (!result) return String_Constructor("");
        strncpy(result, path, len);
        result[len] = '\0';
        void* str = String_Constructor(result);
        xxml_free(result);
        return str;
    }
    return String_Constructor("");
}

void* xxml_Path_getExtension(const char* path) {
    if (!path) return String_Constructor("");

    // First get just the filename
    const char* filename = path;
    const char* sep = strrchr(path, PATH_SEPARATOR);
    const char* fwd = strrchr(path, '/');
    const char* back = strrchr(path, '\\');

    if (sep && sep > filename) filename = sep + 1;
    if (fwd && fwd > filename) filename = fwd + 1;
    if (back && back > filename) filename = back + 1;

    // Find the last dot in the filename
    const char* dot = strrchr(filename, '.');
    if (dot && dot != filename) {
        return String_Constructor(dot);  // Include the dot
    }
    return String_Constructor("");
}

bool xxml_Path_isAbsolute(const char* path) {
    if (!path || !*path) return false;

#ifdef _WIN32
    // Windows: starts with drive letter (C:\) or UNC path (\\)
    if (path[0] == '\\' || path[0] == '/') return true;
    if (strlen(path) >= 2 && path[1] == ':') return true;
    return false;
#else
    // Unix: starts with /
    return path[0] == '/';
#endif
}

void* xxml_Path_getAbsolute(const char* path) {
    if (!path) return String_Constructor("");

    char buffer[4096];
#ifdef _WIN32
    if (_fullpath(buffer, path, sizeof(buffer)) != NULL) {
        return String_Constructor(buffer);
    }
#else
    if (realpath(path, buffer) != NULL) {
        return String_Constructor(buffer);
    }
#endif
    // If resolution fails, return the original path
    return String_Constructor(path);
}

// ============================================
// Utility Functions for Syscall Namespace
// ============================================

// Create a new String from a C string literal
void* xxml_string_create(const char* cstr) {
    return String_Constructor(cstr ? cstr : "");
}

// Concatenate two XXML String objects and return a new String
void* xxml_string_concat(void* str1, void* str2) {
    const char* cstr1 = String_toCString(str1);
    const char* cstr2 = String_toCString(str2);
    if (!cstr1) cstr1 = "";
    if (!cstr2) cstr2 = "";

    size_t len1 = strlen(cstr1);
    size_t len2 = strlen(cstr2);
    char* result = (char*)xxml_malloc(len1 + len2 + 1);
    if (!result) return String_Constructor("");

    strcpy(result, cstr1);
    strcat(result, cstr2);

    void* newStr = String_Constructor(result);
    xxml_free(result);
    return newStr;
}

// Get the C string pointer from an XXML String
const char* xxml_string_cstr(void* str) {
    return String_toCString(str);
}

// Get the length of an XXML String
int64_t xxml_string_length(void* str) {
    if (!str) return 0;
    return (int64_t)((String*)str)->length;
}

// Copy an XXML String
void* xxml_string_copy(void* str) {
    const char* cstr = String_toCString(str);
    return String_Constructor(cstr ? cstr : "");
}

// Check if two XXML Strings are equal
int64_t xxml_string_equals(void* str1, void* str2) {
    const char* cstr1 = String_toCString(str1);
    const char* cstr2 = String_toCString(str2);
    if (!cstr1 && !cstr2) return 1;
    if (!cstr1 || !cstr2) return 0;
    return strcmp(cstr1, cstr2) == 0 ? 1 : 0;
}

// Check if string starts with prefix
int64_t xxml_string_startsWith(void* str, void* prefix) {
    const char* cstr = String_toCString(str);
    const char* prefixCstr = String_toCString(prefix);
    if (!cstr || !prefixCstr) return 0;
    size_t prefixLen = strlen(prefixCstr);
    if (prefixLen == 0) return 1;  // Empty prefix always matches
    return strncmp(cstr, prefixCstr, prefixLen) == 0 ? 1 : 0;
}

// Get character at index as a new single-character string
const char* xxml_string_charAt(void* str, int64_t index) {
    if (!str) return "";
    String* s = (String*)str;
    if (index < 0 || (size_t)index >= s->length) return "";

    // Return pointer to position in string (single char + null)
    static char result[2];
    result[0] = s->data[index];
    result[1] = '\0';
    return result;
}

// Set character at index from another string (uses first character)
void xxml_string_setCharAt(void* str, int64_t index, void* charStr) {
    if (!str || !charStr) return;
    String* s = (String*)str;
    String* ch = (String*)charStr;
    if (index < 0 || (size_t)index >= s->length) return;
    if (ch->length == 0) return;

    s->data[index] = ch->data[0];
}

// Destroy an XXML String
void xxml_string_destroy(void* str) {
    String_destroy(str);
}

// Check if a pointer is null
int64_t xxml_ptr_is_null(void* ptr) {
    return ptr == NULL ? 1 : 0;
}

// Get a null pointer
void* xxml_ptr_null(void) {
    return NULL;
}
