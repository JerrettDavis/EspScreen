/**
 * WiFi.h — host stub. Minimal surface; included transitively by some net code.
 */
#pragma once
#include <Arduino.h>

#ifndef WL_CONNECTED
#define WL_CONNECTED    3
#define WL_DISCONNECTED 6
#define WL_IDLE_STATUS  0
#endif

class WiFiStubClass {
public:
    int     status() { return WL_DISCONNECTED; }
    int     scanNetworks() { return 0; }
    String  SSID() { return String(""); }
    String  SSID(int) { return String(""); }
    int     RSSI() { return 0; }
    int     RSSI(int) { return 0; }
    void    disconnect() {}
};

static WiFiStubClass WiFi;
