#include "xxml_llvm_runtime.h"
#include "xxml_reflection_runtime.h"
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

// Language::Reflection::Type::getPropertyAt
void* Language_Reflection_Type_getPropertyAt(void* self, void* index) {
    Language_Reflection_Type* type = (Language_Reflection_Type*)self;
    if (!type) return NULL;
    int64_t idx = Integer_getValue(index);
    void* propPtr = xxml_reflection_type_getProperty(type->typeInfoPtr, idx);
    if (!propPtr) return NULL;
    return Language_Reflection_PropertyInfo_Constructor(propPtr);
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
