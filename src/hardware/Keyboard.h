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
    bool hasKey() const { return _qHead != _qTail; }
    bool getTrackball(int& dx, int& dy, bool& click);

    // Check if ALT is currently held
    bool altHeld() const { return _altHeld; }
    bool shiftHeld() const { return _shiftHeld; }

private:
    bool   _initialized = false;
    bool   _altHeld     = false;
    bool   _shiftHeld   = false;

    // Key queue (prevents dropped keys when typing quickly)
    static constexpr uint8_t KEY_Q_CAP = 16;
    char    _keyQueue[KEY_Q_CAP] = {};
    uint8_t _qHead = 0;
    uint8_t _qTail = 0;

    // Keyboard polling
    uint32_t _lastKbPollMs = 0;

    // Trackball
    volatile int _tbDx     = 0;
    volatile int _tbDy     = 0;
    bool         _tbClick  = false;
    bool         _tbPressed = false;
    bool         _tbLongPressSent = false;
    uint32_t     _tbPressStartMs  = 0;
    uint32_t      _lastTbTime    = 0;
    uint32_t      _lastClickTime = 0;
    static constexpr uint32_t KB_POLL_MS       = 12;
    static constexpr uint32_t TB_LONG_PRESS_MS = 700;

    // Trackball ISR (static for IRAM_ATTR)
    static Keyboard* _instance;
    static void IRAM_ATTR isrUp();
    static void IRAM_ATTR isrDown();
    static void IRAM_ATTR isrLeft();
    static void IRAM_ATTR isrRight();
    static void IRAM_ATTR isrClick();

    char _readI2CKey();
    bool _enqueueKey(char key);
    char _dequeueKey();
};
