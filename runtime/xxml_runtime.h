#pragma once

// XXML Runtime Library
// This file provides C++ implementations of XXML built-in types and functions

#include <string>
#include <iostream>
#include <sstream>
#include <memory>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <fstream>
#include <vector>
#include <filesystem>
#include <regex>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <map>

namespace XXML {
namespace Runtime {

// Forward declarations
class String;
class Integer;
class Bool;

// ============================================
// String Class - Core XXML String Type
// ============================================
class String {
private:
    std::string data;

public:
    // Constructors
    String() : data("") {}
    String(const char* str) : data(str) {}
    String(const std::string& str) : data(str) {}
    String(const String& other) : data(other.data) {}

    // Copy assignment
    String& operator=(const String& other) {
        data = other.data;
        return *this;
    }

    // Get internal data
    const std::string& getData() const { return data; }
    std::string& getData() { return data; }

    // String methods
    String copy() const {
        return String(data);
    }

    String& append(const String& other) {
        data.append(other.data);
        return *this;
    }

    String prepend(const String& other) const {
        return String(other.data + data);
    }

    int64_t length() const {
        return static_cast<int64_t>(data.length());
    }

    Bool isEmpty() const;

    String charAt(int64_t index) const {
        if (index >= 0 && index < static_cast<int64_t>(data.length())) {
            return String(data.substr(static_cast<size_t>(index), 1));
        }
        return String("");
    }

    String substring(int64_t start, int64_t end) const {
        if (start < 0 || end > static_cast<int64_t>(data.length()) || start > end) {
            return String("");
        }
        return String(data.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
    }

    Integer indexOf(const String& searchStr) const;
    Integer lastIndexOf(const String& searchStr) const;
    Bool contains(const String& searchStr) const;
    Bool startsWith(const String& prefix) const;
    Bool endsWith(const String& suffix) const;

    bool equals(const String& other) const {
        return data == other.data;
    }

    Bool equalsIgnoreCase(const String& other) const;
    Integer compareTo(const String& other) const;

    String toUpperCase() const {
        std::string result = data;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return String(result);
    }

    String toLowerCase() const {
        std::string result = data;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return String(result);
    }

    String trim() const {
        size_t start = data.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return String("");
        size_t end = data.find_last_not_of(" \t\n\r");
        return String(data.substr(start, end - start + 1));
    }

    String replace(const String& oldStr, const String& newStr) const {
        std::string result = data;
        size_t pos = result.find(oldStr.data);
        if (pos != std::string::npos) {
            result.replace(pos, oldStr.data.length(), newStr.data);
        }
        return String(result);
    }

    String replaceAll(const String& oldStr, const String& newStr) const {
        std::string result = data;
        size_t pos = 0;
        while ((pos = result.find(oldStr.data, pos)) != std::string::npos) {
            result.replace(pos, oldStr.data.length(), newStr.data);
            pos += newStr.data.length();
        }
        return String(result);
    }

    String reverse() const {
        std::string result = data;
        std::reverse(result.begin(), result.end());
        return String(result);
    }

    String repeat(const Integer& count) const;

    Integer toInteger() const;
    Bool toBool() const;

    // C string conversion
    const char* toCString() const {
        return data.c_str();
    }

    // Conversion operators for C++ interop
    operator const char*() const { return data.c_str(); }
    operator std::string() const { return data; }

    // Static utility methods (XXML String:: namespace functions)
    static String Convert(const Integer& value);
    static String Convert(const Bool& value);
    static String Constructor() { return String(); }
    static String Constructor(const char* str) { return String(str); }
    static String FromCString(const char* str) {
        return str ? String(str) : String();
    }
};

// ============================================
// Integer Class - Core XXML Integer Type
// ============================================
class Integer {
private:
    int64_t value;

public:
    // Constructors
    Integer() : value(0) {}
    Integer(int64_t val) : value(val) {}
    Integer(const Integer& other) : value(other.value) {}

    // Copy assignment
    Integer& operator=(const Integer& other) {
        value = other.value;
        return *this;
    }

    // Get internal value
    int64_t getValue() const { return value; }
    void setValue(int64_t val) { value = val; }

    // Convert to string
    String toString() const {
        return String(std::to_string(value));
    }

    // Arithmetic operations
    Integer add(const Integer& other) const {
        return Integer(value + other.value);
    }

    Integer subtract(const Integer& other) const {
        return Integer(value - other.value);
    }

    Integer multiply(const Integer& other) const {
        return Integer(value * other.value);
    }

    Integer divide(const Integer& other) const {
        return Integer(value / other.value);
    }

    Integer modulo(const Integer& other) const {
        return Integer(value % other.value);
    }

    // Comparison operators
    bool operator<(const Integer& other) const { return value < other.value; }
    bool operator<=(const Integer& other) const { return value <= other.value; }
    bool operator>(const Integer& other) const { return value > other.value; }
    bool operator>=(const Integer& other) const { return value >= other.value; }
    bool operator==(const Integer& other) const { return value == other.value; }
    bool operator!=(const Integer& other) const { return value != other.value; }

    // Arithmetic operator overloads for C++ interop
    Integer operator+(const Integer& other) const { return Integer(value + other.value); }
    Integer operator-(const Integer& other) const { return Integer(value - other.value); }
    Integer operator*(const Integer& other) const { return Integer(value * other.value); }
    Integer operator/(const Integer& other) const { return Integer(value / other.value); }
    Integer operator%(const Integer& other) const { return Integer(value % other.value); }

    // Increment/decrement
    Integer& operator++() { value++; return *this; }
    Integer operator++(int) { Integer temp(*this); value++; return temp; }

    // Unary operators
    Integer operator-() const { return Integer(-value); }
    Integer operator+() const { return *this; }

    // Conversion operators
    operator int64_t() const { return value; }
    operator int() const { return static_cast<int>(value); }

    // Static utility methods (XXML Integer:: namespace functions)
    static String ToString(const Integer& val) { return val.toString(); }
    static Integer Constructor() { return Integer(); }
    static Integer Constructor(int64_t val) { return Integer(val); }
};

// ============================================
// Bool Class - Core XXML Boolean Type
// ============================================
class Bool {
private:
    bool value;

public:
    // Constructors
    Bool() : value(false) {}
    Bool(bool val) : value(val) {}
    Bool(const Bool& other) : value(other.value) {}

    // Copy assignment
    Bool& operator=(const Bool& other) {
        value = other.value;
        return *this;
    }

    // Get internal value
    bool getValue() const { return value; }
    void setValue(bool val) { value = val; }

    // Convert to string
    String toString() const {
        return String(value ? "true" : "false");
    }

    // Logical operations
    Bool andOp(const Bool& other) const {
        return Bool(value && other.value);
    }

    Bool orOp(const Bool& other) const {
        return Bool(value || other.value);
    }

    Bool notOp() const {
        return Bool(!value);
    }

    // Conversion operators
    operator bool() const { return value; }

    // Static utility methods (XXML Bool:: namespace functions)
    static Bool Constructor() { return Bool(); }
    static Bool Constructor(bool val) { return Bool(val); }
};

// ============================================
// System Namespace - I/O and System Functions
// ============================================
namespace System {

    inline void Print(const String& message) {
        std::cout << message.getData();
    }

    inline void PrintLine(const String& message) {
        std::cout << message.getData() << std::endl;
    }

    inline String ReadLine() {
        std::string line;
        std::getline(std::cin, line);
        return String(line);
    }

    inline Integer GetTime() {
        return Integer(static_cast<int64_t>(std::time(nullptr)));
    }

} // namespace System

// ============================================
// Global String Extension Functions
// ============================================

inline String Copy(const String& str) {
    return str.copy();
}

inline String& Append(String& str, const String& other) {
    return str.append(other);
}

inline Integer Length(const String& str) {
    return Integer(str.length());
}

inline String CharAt(const String& str, const Integer& index) {
    return str.charAt(index.getValue());
}

inline String Substring(const String& str, const Integer& start, const Integer& end) {
    return str.substring(start.getValue(), end.getValue());
}

inline Bool Equals(const String& str1, const String& str2) {
    return Bool(str1.equals(str2));
}

// Implement String static methods (defined after Integer and Bool are complete)
inline String String::Convert(const Integer& value) {
    return value.toString();
}

inline String String::Convert(const Bool& value) {
    return value.toString();
}

// Implement String methods that depend on Integer and Bool
inline Bool String::isEmpty() const {
    return Bool(data.empty());
}

inline Integer String::indexOf(const String& searchStr) const {
    size_t pos = data.find(searchStr.data);
    return Integer(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
}

inline Integer String::lastIndexOf(const String& searchStr) const {
    size_t pos = data.rfind(searchStr.data);
    return Integer(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
}

inline Bool String::contains(const String& searchStr) const {
    return Bool(data.find(searchStr.data) != std::string::npos);
}

inline Bool String::startsWith(const String& prefix) const {
    return Bool(data.find(prefix.data) == 0);
}

inline Bool String::endsWith(const String& suffix) const {
    if (suffix.data.length() > data.length()) return Bool(false);
    return Bool(data.compare(data.length() - suffix.data.length(), suffix.data.length(), suffix.data) == 0);
}

inline Bool String::equalsIgnoreCase(const String& other) const {
    std::string s1 = data, s2 = other.data;
    std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
    std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    return Bool(s1 == s2);
}

inline Integer String::compareTo(const String& other) const {
    return Integer(data.compare(other.data));
}

inline Integer String::toInteger() const {
    try {
        return Integer(std::stoll(data));
    } catch (...) {
        return Integer(0);
    }
}

inline Bool String::toBool() const {
    return Bool(data == "true" || data == "True" || data == "1");
}

inline String String::repeat(const Integer& count) const {
    std::string result;
    for (int64_t i = 0; i < count.getValue(); ++i) {
        result += data;
    }
    return String(result);
}

// ============================================
// Math Namespace - Mathematical Functions
// ============================================
namespace Math {

    inline Integer abs(const Integer& value) {
        return Integer(std::abs(value.getValue()));
    }

    inline Integer min(const Integer& a, const Integer& b) {
        return Integer(std::min(a.getValue(), b.getValue()));
    }

    inline Integer max(const Integer& a, const Integer& b) {
        return Integer(std::max(a.getValue(), b.getValue()));
    }

    inline Integer clamp(const Integer& value, const Integer& minVal, const Integer& maxVal) {
        return Integer(std::min(std::max(value.getValue(), minVal.getValue()), maxVal.getValue()));
    }

    inline Integer pow(const Integer& base, const Integer& exponent) {
        return Integer(static_cast<int64_t>(std::pow(base.getValue(), exponent.getValue())));
    }

    inline Integer sqrt(const Integer& value) {
        return Integer(static_cast<int64_t>(std::sqrt(value.getValue())));
    }

    inline Integer floor(const Integer& value) {
        return value; // Already an integer
    }

    inline Integer ceil(const Integer& value) {
        return value; // Already an integer
    }

    inline Integer round(const Integer& value) {
        return value; // Already an integer
    }

    inline Integer random() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int64_t> dis(0, INT64_MAX);
        return Integer(dis(gen));
    }

    inline Integer randomRange(const Integer& minVal, const Integer& maxVal) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int64_t> dis(minVal.getValue(), maxVal.getValue());
        return Integer(dis(gen));
    }

    inline Integer gcd(const Integer& a, const Integer& b) {
        int64_t x = std::abs(a.getValue());
        int64_t y = std::abs(b.getValue());
        while (y != 0) {
            int64_t temp = y;
            y = x % y;
            x = temp;
        }
        return Integer(x);
    }

    inline Integer lcm(const Integer& a, const Integer& b) {
        return Integer(std::abs(a.getValue() * b.getValue()) / gcd(a, b).getValue());
    }

    inline Bool isPrime(const Integer& value) {
        int64_t n = value.getValue();
        if (n <= 1) return Bool(false);
        if (n <= 3) return Bool(true);
        if (n % 2 == 0 || n % 3 == 0) return Bool(false);
        for (int64_t i = 5; i * i <= n; i += 6) {
            if (n % i == 0 || n % (i + 2) == 0) return Bool(false);
        }
        return Bool(true);
    }

    inline Integer factorial(const Integer& n) {
        int64_t val = n.getValue();
        if (val <= 1) return Integer(1);
        int64_t result = 1;
        for (int64_t i = 2; i <= val; ++i) {
            result *= i;
        }
        return Integer(result);
    }

    inline Integer fibonacci(const Integer& n) {
        int64_t val = n.getValue();
        if (val <= 0) return Integer(0);
        if (val == 1) return Integer(1);
        int64_t a = 0, b = 1;
        for (int64_t i = 2; i <= val; ++i) {
            int64_t temp = a + b;
            a = b;
            b = temp;
        }
        return Integer(b);
    }

} // namespace Math

// ============================================
// Syscall - System Call Interface
// ============================================

// Syscall return type - can hold various primitive types
class SyscallResult {
public:
    enum Type { INTEGER, PTR, BOOL, NONE };

    Type type;
    int64_t intValue;
    void* ptrValue;
    bool boolValue;

    SyscallResult() : type(NONE), intValue(0), ptrValue(nullptr), boolValue(false) {}
    SyscallResult(int64_t val) : type(INTEGER), intValue(val), ptrValue(nullptr), boolValue(false) {}
    SyscallResult(void* ptr) : type(PTR), intValue(0), ptrValue(ptr), boolValue(false) {}
    SyscallResult(bool val) : type(BOOL), intValue(0), ptrValue(nullptr), boolValue(val) {}

    Integer toInteger() const { return Integer(intValue); }
    Bool toBool() const { return Bool(boolValue); }
    void* toPtr() const { return ptrValue; }
};

// Syscall dispatcher - provides low-level operations
namespace Syscall {

    // ============================================
    // Memory Operations
    // ============================================

    inline SyscallResult malloc(int64_t size) {
        void* ptr = std::malloc(static_cast<size_t>(size));
        return SyscallResult(ptr);
    }

    inline SyscallResult free(void* ptr) {
        std::free(ptr);
        return SyscallResult();
    }

    inline SyscallResult realloc(void* ptr, int64_t newSize) {
        void* newPtr = std::realloc(ptr, static_cast<size_t>(newSize));
        return SyscallResult(newPtr);
    }

    inline SyscallResult memcpy(void* dest, const void* src, int64_t size) {
        std::memcpy(dest, src, static_cast<size_t>(size));
        return SyscallResult(dest);
    }

    inline SyscallResult memset(void* dest, int value, int64_t size) {
        std::memset(dest, value, static_cast<size_t>(size));
        return SyscallResult(dest);
    }

    inline SyscallResult read_int64(const void* ptr) {
        return SyscallResult(*static_cast<const int64_t*>(ptr));
    }

    inline SyscallResult write_int64(void* ptr, int64_t value) {
        *static_cast<int64_t*>(ptr) = value;
        return SyscallResult();
    }

    // ============================================
    // File I/O Operations
    // ============================================

    inline SyscallResult fopen(const char* filename, const char* mode) {
        FILE* file = std::fopen(filename, mode);
        return SyscallResult(static_cast<void*>(file));
    }

    inline SyscallResult fclose(FILE* file) {
        int result = std::fclose(file);
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult fread(void* buffer, int64_t size, int64_t count, FILE* file) {
        size_t bytesRead = std::fread(buffer, static_cast<size_t>(size), static_cast<size_t>(count), file);
        return SyscallResult(static_cast<int64_t>(bytesRead));
    }

    inline SyscallResult fwrite(const void* buffer, int64_t size, int64_t count, FILE* file) {
        size_t bytesWritten = std::fwrite(buffer, static_cast<size_t>(size), static_cast<size_t>(count), file);
        return SyscallResult(static_cast<int64_t>(bytesWritten));
    }

    inline SyscallResult fseek(FILE* file, int64_t offset, int origin) {
        int result = std::fseek(file, static_cast<long>(offset), origin);
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult ftell(FILE* file) {
        long pos = std::ftell(file);
        return SyscallResult(static_cast<int64_t>(pos));
    }

    inline SyscallResult feof(FILE* file) {
        return SyscallResult(std::feof(file) != 0);
    }

    // ============================================
    // System Operations
    // ============================================

    inline SyscallResult getenv(const char* name) {
        const char* value = std::getenv(name);
        return SyscallResult(static_cast<void*>(const_cast<char*>(value)));
    }

    inline SyscallResult setenv_impl(const char* name, const char* value) {
#ifdef _WIN32
        int result = _putenv_s(name, value);
#else
        int result = setenv(name, value, 1);
#endif
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult time_now() {
        return SyscallResult(static_cast<int64_t>(std::time(nullptr)));
    }

    inline SyscallResult sleep_ms(int64_t milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return SyscallResult();
    }

    inline SyscallResult exit_program(int64_t code) {
        std::exit(static_cast<int>(code));
        return SyscallResult(); // Never reached
    }

    // ============================================
    // Network Operations (Socket API)
    // ============================================

    #ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    #else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #endif

    inline SyscallResult socket_create(int domain, int type, int protocol) {
        #ifdef _WIN32
        SOCKET sock = socket(domain, type, protocol);
        return SyscallResult(static_cast<int64_t>(sock));
        #else
        int sock = socket(domain, type, protocol);
        return SyscallResult(static_cast<int64_t>(sock));
        #endif
    }

    inline SyscallResult socket_connect(int64_t sockfd, const void* addr, int64_t addrlen) {
        #ifdef _WIN32
        int result = connect(static_cast<SOCKET>(sockfd), static_cast<const sockaddr*>(addr), static_cast<int>(addrlen));
        #else
        int result = connect(static_cast<int>(sockfd), static_cast<const sockaddr*>(addr), static_cast<socklen_t>(addrlen));
        #endif
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult socket_bind(int64_t sockfd, const void* addr, int64_t addrlen) {
        #ifdef _WIN32
        int result = bind(static_cast<SOCKET>(sockfd), static_cast<const sockaddr*>(addr), static_cast<int>(addrlen));
        #else
        int result = bind(static_cast<int>(sockfd), static_cast<const sockaddr*>(addr), static_cast<socklen_t>(addrlen));
        #endif
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult socket_listen(int64_t sockfd, int64_t backlog) {
        #ifdef _WIN32
        int result = listen(static_cast<SOCKET>(sockfd), static_cast<int>(backlog));
        #else
        int result = listen(static_cast<int>(sockfd), static_cast<int>(backlog));
        #endif
        return SyscallResult(static_cast<int64_t>(result));
    }

    inline SyscallResult socket_accept(int64_t sockfd, void* addr, int64_t* addrlen) {
        #ifdef _WIN32
        int addrlen_int = static_cast<int>(*addrlen);
        SOCKET clientSock = accept(static_cast<SOCKET>(sockfd), static_cast<sockaddr*>(addr), &addrlen_int);
        *addrlen = static_cast<int64_t>(addrlen_int);
        return SyscallResult(static_cast<int64_t>(clientSock));
        #else
        socklen_t addrlen_socklen = static_cast<socklen_t>(*addrlen);
        int clientSock = accept(static_cast<int>(sockfd), static_cast<sockaddr*>(addr), &addrlen_socklen);
        *addrlen = static_cast<int64_t>(addrlen_socklen);
        return SyscallResult(static_cast<int64_t>(clientSock));
        #endif
    }

    inline SyscallResult socket_send(int64_t sockfd, const void* buf, int64_t len, int64_t flags) {
        #ifdef _WIN32
        int bytesSent = send(static_cast<SOCKET>(sockfd), static_cast<const char*>(buf), static_cast<int>(len), static_cast<int>(flags));
        #else
        ssize_t bytesSent = send(static_cast<int>(sockfd), buf, static_cast<size_t>(len), static_cast<int>(flags));
        #endif
        return SyscallResult(static_cast<int64_t>(bytesSent));
    }

    inline SyscallResult socket_recv(int64_t sockfd, void* buf, int64_t len, int64_t flags) {
        #ifdef _WIN32
        int bytesRecv = recv(static_cast<SOCKET>(sockfd), static_cast<char*>(buf), static_cast<int>(len), static_cast<int>(flags));
        #else
        ssize_t bytesRecv = recv(static_cast<int>(sockfd), buf, static_cast<size_t>(len), static_cast<int>(flags));
        #endif
        return SyscallResult(static_cast<int64_t>(bytesRecv));
    }

    inline SyscallResult socket_close(int64_t sockfd) {
        #ifdef _WIN32
        int result = closesocket(static_cast<SOCKET>(sockfd));
        #else
        int result = close(static_cast<int>(sockfd));
        #endif
        return SyscallResult(static_cast<int64_t>(result));
    }

} // namespace Syscall

} // namespace Runtime
} // namespace XXML

// ============================================
// Bring runtime classes into global scope
// ============================================
using XXML::Runtime::String;
using XXML::Runtime::Integer;
using XXML::Runtime::Bool;
using namespace XXML::Runtime;

