/**
 * Minimal Arduino API shim for host-side TinyCFG benchmarking (PC / MinGW / GCC).
 * Place benchmark/host on the include path BEFORE system headers.
 */
#ifndef ARDUINO_HOST_SHIM_H
#define ARDUINO_HOST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <chrono>

class String {
    char _buf[256];
public:
    String() { _buf[0] = '\0'; }
    String(const char* s) {
        if (s) {
            strncpy(_buf, s, sizeof(_buf) - 1);
            _buf[sizeof(_buf) - 1] = '\0';
        } else {
            _buf[0] = '\0';
        }
    }
    const char* c_str() const { return _buf; }
    operator const char*() const { return _buf; }
};

class Stream {
public:
    void println() const { printf("\n"); }
    void println(const char* s) const { printf("%s\n", s); }
    void print(const char* s) const { printf("%s", s); }
    void print(char c) const { char s[2] = {c, '\0'}; printf("%s", s); }
    void printf(const char* fmt, ...) const {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
};

extern Stream Serial;

inline uint32_t micros() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point t0 = clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
        clock::now() - t0).count();
}

#endif
