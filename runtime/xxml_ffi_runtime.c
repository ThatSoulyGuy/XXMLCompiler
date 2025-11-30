#include "xxml_ffi_runtime.h"
#include "xxml_llvm_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#endif

// ============================================
// Thread-local error message storage
// ============================================
#ifdef _WIN32
static __declspec(thread) char ffi_error_message[1024] = {0};
#else
static __thread char ffi_error_message[1024] = {0};
#endif

// ============================================
// DLL Loading Functions
// ============================================

void* xxml_FFI_loadLibrary(const char* path) {
    if (!path) {
        snprintf(ffi_error_message, sizeof(ffi_error_message), "FFI Error: NULL library path");
        return NULL;
    }

#ifdef _WIN32
    // On Windows, use LoadLibrary
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        DWORD error = GetLastError();
        snprintf(ffi_error_message, sizeof(ffi_error_message),
                 "FFI Error: Failed to load library '%s' (error code: %lu)", path, error);
        return NULL;
    }
    return (void*)handle;
#else
    // On Unix-like systems, use dlopen
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        snprintf(ffi_error_message, sizeof(ffi_error_message),
                 "FFI Error: Failed to load library '%s': %s", path, dlerror());
        return NULL;
    }
    return handle;
#endif
}

void* xxml_FFI_getSymbol(void* handle, const char* name) {
    if (!handle) {
        snprintf(ffi_error_message, sizeof(ffi_error_message), "FFI Error: NULL library handle");
        return NULL;
    }
    if (!name) {
        snprintf(ffi_error_message, sizeof(ffi_error_message), "FFI Error: NULL symbol name");
        return NULL;
    }

#ifdef _WIN32
    FARPROC proc = GetProcAddress((HMODULE)handle, name);
    if (!proc) {
        DWORD error = GetLastError();
        snprintf(ffi_error_message, sizeof(ffi_error_message),
                 "FFI Error: Failed to get symbol '%s' (error code: %lu)", name, error);
        return NULL;
    }
    return (void*)proc;
#else
    void* sym = dlsym(handle, name);
    if (!sym) {
        snprintf(ffi_error_message, sizeof(ffi_error_message),
                 "FFI Error: Failed to get symbol '%s': %s", name, dlerror());
        return NULL;
    }
    return sym;
#endif
}

void xxml_FFI_freeLibrary(void* handle) {
    if (!handle) return;

#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

const char* xxml_FFI_getError(void) {
    return ffi_error_message;
}

bool xxml_FFI_libraryExists(const char* path) {
    if (!path) return false;

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
#endif
}

// ============================================
// Type Conversion Functions
// ============================================

const char* xxml_FFI_stringToCString(void* xxmlString) {
    if (!xxmlString) return NULL;
    return String_toCString(xxmlString);
}

void* xxml_FFI_cstringToString(const char* cstr) {
    if (!cstr) return String_Constructor("");
    return String_Constructor(cstr);
}

int64_t xxml_FFI_integerToInt64(void* xxmlInteger) {
    if (!xxmlInteger) return 0;
    return Integer_getValue(xxmlInteger);
}

void* xxml_FFI_int64ToInteger(int64_t value) {
    return Integer_Constructor(value);
}

float xxml_FFI_floatToC(void* xxmlFloat) {
    if (!xxmlFloat) return 0.0f;
    return Float_getValue(xxmlFloat);
}

void* xxml_FFI_cToFloat(float value) {
    return Float_Constructor(value);
}

double xxml_FFI_doubleToC(void* xxmlDouble) {
    if (!xxmlDouble) return 0.0;
    return Double_getValue(xxmlDouble);
}

void* xxml_FFI_cToDouble(double value) {
    return Double_Constructor(value);
}

bool xxml_FFI_boolToC(void* xxmlBool) {
    if (!xxmlBool) return false;
    return Bool_getValue(xxmlBool);
}

void* xxml_FFI_cToBool(bool value) {
    return Bool_Constructor(value);
}
