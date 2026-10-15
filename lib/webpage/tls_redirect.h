#pragma once
#include <Arduino.h>

class TLSRedirectServer {
public:
    bool init();
    void begin();
    void stop();
    void handle();
private:
    bool _running = false;
};

void portalDebug(const char* fmt, ...);
String getPortalDebugLog();
