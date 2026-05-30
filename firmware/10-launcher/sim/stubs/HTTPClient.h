/**
 * HTTPClient.h — host stub. Satisfies compilation of claude_widget's network
 * path only (never executed by the harness).
 */
#pragma once
#include <Arduino.h>

class WiFiClientSecure;  /* fwd */

class HTTPClient {
public:
    template <typename Client>
    bool begin(Client& /*c*/, const char* /*url*/) { return true; }
    bool begin(const char* /*url*/) { return true; }
    void end() {}
    void setTimeout(uint16_t) {}
    void addHeader(const String& /*name*/, const String& /*value*/) {}
    void addHeader(const char* /*name*/, const char* /*value*/) {}
    int  GET() { return -1; }
    int  POST(const String& /*body*/) { return -1; }
    String getString() { return String(""); }
};
