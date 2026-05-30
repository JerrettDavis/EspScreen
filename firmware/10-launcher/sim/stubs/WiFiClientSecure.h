/**
 * WiFiClientSecure.h — host stub. Only needs to satisfy compilation of
 * claude_widget's network path (never executed by the harness).
 */
#pragma once
#include <Arduino.h>

class WiFiClientSecure {
public:
    void setInsecure() {}
    void stop() {}
    int  connected() { return 0; }
};
