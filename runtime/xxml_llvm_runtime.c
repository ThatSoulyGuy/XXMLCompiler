#include "xxml_llvm_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

size_t String_length(void* self) {
    if (!self) return 0;
    return ((String*)self)->length;
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
