#ifndef XXML_FFI_RUNTIME_H
#define XXML_FFI_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// XXML Foreign Function Interface Runtime
// Provides DLL loading and symbol resolution
// ============================================

// Load a dynamic library
// Returns handle (opaque pointer), or NULL on failure
void* xxml_FFI_loadLibrary(const char* path);

// Get a symbol (function) from a loaded library
// Returns function pointer, or NULL on failure
void* xxml_FFI_getSymbol(void* handle, const char* name);

// Free a loaded library
void xxml_FFI_freeLibrary(void* handle);

// Get the last error message (for debugging)
const char* xxml_FFI_getError(void);

// Check if a library file exists
bool xxml_FFI_libraryExists(const char* path);

// ============================================
// Type Conversion Helpers
// ============================================

// Convert XXML String to C string (for FFI ptr params)
const char* xxml_FFI_stringToCString(void* xxmlString);

// Convert C string to XXML String (for FFI ptr returns)
void* xxml_FFI_cstringToString(const char* cstr);

// Convert XXML Integer to int64 (for FFI int params)
int64_t xxml_FFI_integerToInt64(void* xxmlInteger);

// Convert int64 to XXML Integer (for FFI int returns)
void* xxml_FFI_int64ToInteger(int64_t value);

// Convert XXML Float to C float
float xxml_FFI_floatToC(void* xxmlFloat);

// Convert C float to XXML Float
void* xxml_FFI_cToFloat(float value);

// Convert XXML Double to C double
double xxml_FFI_doubleToC(void* xxmlDouble);

// Convert C double to XXML Double
void* xxml_FFI_cToDouble(double value);

// Convert XXML Bool to C bool
bool xxml_FFI_boolToC(void* xxmlBool);

// Convert C bool to XXML Bool
void* xxml_FFI_cToBool(bool value);

#ifdef __cplusplus
}
#endif

#endif // XXML_FFI_RUNTIME_H
