#pragma once
#include <LovyanGFX.hpp>
#include "Theme.h"

class Display;
class GPS;
class Radio;

// ================================================================
// Status bar — top 24px strip
// [Mode icon | Freq | GPS | Sats | Time | Bat | RSSI]
// ================================================================
class StatusBar {
public:
    StatusBar() = default;

    void begin(Display* disp, GPS* gps, Radio* radio);

    // Call every ~1 second
    void update();

    // Force full redraw (call after screen change)
    void invalidate() { _dirty = true; }

    void setModeName(const char* name, uint16_t color);

private:
    Display*  _disp  = nullptr;
    GPS*      _gps   = nullptr;
    Radio*    _radio = nullptr;
    bool      _dirty = true;

    char      _modeName[16]  = "---";
    uint16_t  _modeColor     = COL_TEXT_DIM;

    // Cached values to avoid redrawing unchanged parts
    uint8_t   _lastSats    = 255;
    bool      _lastFix     = false;
    uint8_t   _lastBatPct  = 255;
    char      _lastTime[9] = "";
    float     _lastRSSI    = 0;

    void _drawBackground();
    void _clearRegion(int x, int w);
    void _drawMode();
    void _drawGPS();
    void _drawTime();
    void _drawRSSI();
    void _drawBattery();
    uint8_t _readBatteryPct();
};
