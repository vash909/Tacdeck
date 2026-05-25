#pragma once
#include <Wire.h>
#include <Arduino.h>
#include "pins.h"

// Special key codes returned by getKey()
#define KEY_NONE       0x00
#define KEY_BACKSPACE  0x08
#define KEY_TAB        0x09
#define KEY_ENTER      0x0D
#define KEY_ESC        0x1B
#define KEY_UP         0x11   // custom: trackball up
#define KEY_DOWN       0x12   // custom: trackball down
#define KEY_LEFT       0x13   // custom: trackball left
#define KEY_RIGHT      0x14   // custom: trackball right
#define KEY_CLICK      0x15   // custom: trackball click
#define KEY_ALT        0x80   // modifier flag (ORed)

struct TrackballEvent {
    int  dx;       // -N=left, +N=right
    int  dy;       // -N=up,   +N=down
    bool click;
};

class Keyboard {
public:
    Keyboard() = default;
    bool begin();
    void update();                    // call every loop

    char getKey();                    // returns 0 if no key
    bool hasKey() const { return _keyAvail; }
    bool getTrackball(int& dx, int& dy, bool& click);

    // Check if ALT is currently held
    bool altHeld() const { return _altHeld; }
    bool shiftHeld() const { return _shiftHeld; }

private:
    bool   _initialized = false;
    char   _lastKey     = KEY_NONE;
    bool   _keyAvail    = false;
    bool   _altHeld     = false;
    bool   _shiftHeld   = false;

    // Trackball
    volatile int  _tbDx   = 0;
    volatile int  _tbDy   = 0;
    volatile bool _tbClick= false;
    uint32_t      _lastTbTime    = 0;
    uint32_t      _lastClickTime = 0;

    // Trackball ISR (static for IRAM_ATTR)
    static Keyboard* _instance;
    static void IRAM_ATTR isrUp();
    static void IRAM_ATTR isrDown();
    static void IRAM_ATTR isrLeft();
    static void IRAM_ATTR isrRight();
    static void IRAM_ATTR isrClick();

    char _readI2CKey();
};
