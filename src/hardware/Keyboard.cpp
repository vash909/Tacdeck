#include "Keyboard.h"

Keyboard* Keyboard::_instance = nullptr;

void IRAM_ATTR Keyboard::isrUp()    { if (_instance) _instance->_tbDy--; }
void IRAM_ATTR Keyboard::isrDown()  { if (_instance) _instance->_tbDy++; }
void IRAM_ATTR Keyboard::isrLeft()  { if (_instance) _instance->_tbDx--; }
void IRAM_ATTR Keyboard::isrRight() { if (_instance) _instance->_tbDx++; }
void IRAM_ATTR Keyboard::isrClick() { if (_instance) _instance->_tbClick = true; }

bool Keyboard::begin() {
    _instance = this;

    Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL, TDECK_I2C_FREQ);

    // Keyboard INT pin
    pinMode(TDECK_KB_INT, INPUT_PULLUP);

    // Trackball pins (active LOW, use FALLING edge)
    pinMode(TDECK_TB_UP,    INPUT_PULLUP);
    pinMode(TDECK_TB_DOWN,  INPUT_PULLUP);
    pinMode(TDECK_TB_LEFT,  INPUT_PULLUP);
    pinMode(TDECK_TB_RIGHT, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(TDECK_TB_UP),    isrUp,    FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TB_DOWN),  isrDown,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TB_LEFT),  isrLeft,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TB_RIGHT), isrRight, FALLING);

    // Trackball click button (GPIO0 = BOOT, active LOW, pulled up by board)
    pinMode(TDECK_TB_CLICK, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TDECK_TB_CLICK), isrClick, FALLING);

    _initialized = true;
    Serial.println("[KB] Keyboard + trackball ready");
    return true;
}

void Keyboard::update() {
    // Poll keyboard INT pin
    if (digitalRead(TDECK_KB_INT) == LOW) {
        char k = _readI2CKey();
        if (k != KEY_NONE) {
            // Handle modifiers
            if (k == 0x01) { _altHeld   = !_altHeld;   return; }
            if (k == 0x02) { _shiftHeld = !_shiftHeld; return; }

            // Trackball click comes as ENTER from keyboard
            // Map ENTER to click when no text input context
            _lastKey  = k;
            _keyAvail = true;
        }
    }
}

char Keyboard::getKey() {
    if (!_keyAvail) return KEY_NONE;
    _keyAvail = false;
    return _lastKey;
}

bool Keyboard::getTrackball(int& dx, int& dy, bool& click) {
    noInterrupts();
    dx = _tbDx; dy = _tbDy; click = _tbClick;
    _tbDx = 0; _tbDy = 0; _tbClick = false;
    interrupts();

    if (dx == 0 && dy == 0 && !click) return false;

    uint32_t now = millis();

    // Directional movement: debounce 20 ms (encoder-style noise)
    if (dx != 0 || dy != 0) {
        if (now - _lastTbTime < 20) {
            dx = 0; dy = 0;
        } else {
            _lastTbTime = now;
        }
    }

    // Click: separate 80 ms debounce (mechanical button)
    if (click) {
        if (now - _lastClickTime < 80) {
            click = false;
        } else {
            _lastClickTime = now;
        }
    }

    return (dx != 0 || dy != 0 || click);
}

char Keyboard::_readI2CKey() {
    Wire.requestFrom(TDECK_KB_ADDR, 1);
    if (Wire.available()) {
        uint8_t raw = Wire.read();
        if (raw == 0x00) return KEY_NONE;

        // BB Q10 keyboard key mapping
        // Most keys send ASCII directly
        // Special keys:
        switch (raw) {
            case 0x08: return KEY_BACKSPACE;
            case 0x0A: return KEY_ENTER;   // '\n' — some keyboard firmware variants
            case 0x0D: return KEY_ENTER;   // '\r' — standard
            case 0x11: return KEY_UP;
            case 0x12: return KEY_DOWN;
            case 0x13: return KEY_LEFT;
            case 0x14: return KEY_RIGHT;
            case 0x1B: return KEY_ESC;
            default:
                // Printable ASCII
                if (raw >= 0x20 && raw <= 0x7E) return (char)raw;
                return KEY_NONE;
        }
    }
    return KEY_NONE;
}
