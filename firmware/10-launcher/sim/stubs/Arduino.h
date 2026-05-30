/**
 * Arduino.h — Host (native) stub for the ESPScreen simulator.
 *
 * Resolves `#include <Arduino.h>` to this file on the native build via the
 * -iquote/-I ordering in platformio.ini ([sim_common]). Provides just enough
 * of the Arduino API surface that the launcher construction path (and the
 * broader P2 screen set) needs to COMPILE and LINK on the host:
 *
 *   - Serial (print/println/printf → stdout)
 *   - millis()/micros()/delay() backed by std::chrono
 *   - pinMode/digitalWrite/digitalRead no-ops
 *   - a minimal std::string-backed String (wifi_profiles.h / settings.cpp use it)
 *
 * This is intentionally header-only and dependency-free. It is NOT a faithful
 * Arduino emulation — only the symbols the simulated screens touch.
 */
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cstddef>
#include <string>
#include <chrono>
#include <ctime>

/* ── Pin / timing primitives ─────────────────────────────────────────────── */

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif

#ifndef DEC
#define DEC 10
#endif
#ifndef HEX
#define HEX 16
#endif

inline unsigned long millis() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

inline unsigned long micros() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (unsigned long)duration_cast<microseconds>(steady_clock::now() - t0).count();
}

inline void delay(unsigned long)        {}
inline void delayMicroseconds(unsigned) {}
inline void pinMode(int, int)           {}
inline void digitalWrite(int, int)      {}
inline int  digitalRead(int)            { return 0; }
inline int  analogRead(int)             { return 0; }
inline void yield()                     {}

/* ESP system: on-device this is exposed transitively via Arduino.h. The guard
 * lets esp_heap_caps.h skip its own copy when both headers land in one TU. */
#ifndef ESPSCREEN_SIM_HAVE_FREE_HEAP
#define ESPSCREEN_SIM_HAVE_FREE_HEAP
inline uint32_t esp_get_free_heap_size() { return 70000; }
#endif

/* strptime is absent on MinGW; provide a minimal subset sufficient for the
 * ISO-8601 patterns claude_widget parses ("%Y-%m-%dT%H:%M:%S"). Only used on a
 * code path the validator harness never executes. */
inline char* strptime(const char* s, const char* fmt, struct tm* tm) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) >= 3) {
        tm->tm_year = y - 1900; tm->tm_mon = mo - 1; tm->tm_mday = d;
        tm->tm_hour = h; tm->tm_min = mi; tm->tm_sec = se;
        return const_cast<char*>(s + std::strlen(s));
    }
    (void)fmt;
    return nullptr;
}

/* ── Minimal String ──────────────────────────────────────────────────────── */
/* Backed by std::string. Implements the subset the simulated screens use:
 * construction from C-string/std::string, c_str(), length(), concatenation,
 * comparison, and equalsIgnoreCase() (settings.cpp / wifi flows). */
class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v)          { s_ = std::to_string(v); }
    String(unsigned v)     { s_ = std::to_string(v); }
    String(long v)         { s_ = std::to_string(v); }
    String(unsigned long v){ s_ = std::to_string(v); }
    String(double v)       { s_ = std::to_string(v); }

    const char* c_str() const { return s_.c_str(); }
    unsigned length() const   { return (unsigned)s_.size(); }
    bool isEmpty() const      { return s_.empty(); }

    bool equals(const char* o) const   { return o && s_ == o; }
    bool equals(const String& o) const { return s_ == o.s_; }

    bool equalsIgnoreCase(const char* o) const {
        if (!o) return false;
        std::string b(o);
        if (s_.size() != b.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i)
            if (lower(s_[i]) != lower(b[i])) return false;
        return true;
    }
    bool equalsIgnoreCase(const String& o) const { return equalsIgnoreCase(o.c_str()); }

    String& operator+=(const char* o)   { if (o) s_ += o; return *this; }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    unsigned char concat(const char* o)  { if (o) s_ += o; return 1; }   /* ArduinoJson writer */
    unsigned char concat(const String& o){ s_ += o.s_; return 1; }
    unsigned char concat(char c)          { s_ += c; return 1; }
    String  operator+(const String& o) const { return String(s_ + o.s_); }
    String  operator+(const char* o)   const { return String(s_ + (o ? o : "")); }
    bool    operator==(const String& o) const { return s_ == o.s_; }
    bool    operator==(const char* o)   const { return o && s_ == o; }
    bool    operator!=(const String& o) const { return s_ != o.s_; }
    bool    operator!=(const char* o)   const { return !(*this == o); }
    char    operator[](unsigned i) const { return i < s_.size() ? s_[i] : '\0'; }

    const std::string& str() const { return s_; }
    operator std::string() const { return s_; }

private:
    static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    std::string s_;
};

/* const char* + String — needed for expressions like "Bearer " + token */
inline String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs ? lhs : "") + rhs.str());
}

/* ── BSD strlcpy/strlcat (absent on MinGW) ──────────────────────────────────── */
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t srclen = src ? std::strlen(src) : 0;
    if (size) {
        size_t n = (srclen < size - 1) ? srclen : size - 1;
        if (src) std::memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}
inline size_t strlcat(char* dst, const char* src, size_t size) {
    size_t dlen = std::strlen(dst);
    if (dlen >= size) return size + (src ? std::strlen(src) : 0);
    return dlen + strlcpy(dst + dlen, src, size - dlen);
}

/* ── Serial ──────────────────────────────────────────────────────────────── */
class HardwareSerial {
public:
    void begin(unsigned long = 0)         {}
    void end()                            {}
    explicit operator bool() const        { return true; }
    int  available()                      { return 0; }
    int  read()                           { return -1; }
    void flush()                          { fflush(stdout); }

    size_t print(const char* s)           { return s ? fputs(s, stdout), std::strlen(s) : 0; }
    size_t print(char c)                  { fputc(c, stdout); return 1; }
    size_t print(int v, int base = DEC)   { return printf(base == HEX ? "%x" : "%d", v); }
    size_t print(unsigned v, int b = DEC) { return printf(b == HEX ? "%x" : "%u", v); }
    size_t print(long v)                  { return printf("%ld", v); }
    size_t print(unsigned long v)         { return printf("%lu", v); }
    size_t print(double v)                { return printf("%f", v); }
    size_t print(const String& s)         { return print(s.c_str()); }

    size_t println()                      { fputc('\n', stdout); return 1; }
    size_t println(const char* s)         { size_t n = print(s); return n + println(); }
    size_t println(const String& s)       { return println(s.c_str()); }
    size_t println(int v)                 { size_t n = print(v); return n + println(); }
    size_t println(unsigned v)            { size_t n = print(v); return n + println(); }
    size_t println(long v)                { size_t n = print(v); return n + println(); }
    size_t println(unsigned long v)       { size_t n = print(v); return n + println(); }

    size_t printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        return n < 0 ? 0 : (size_t)n;
    }
};

extern HardwareSerial Serial;
