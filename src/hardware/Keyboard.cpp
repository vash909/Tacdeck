#include "Keyboard.h"

Keyboard* Keyboard::_instance = nullptr;

void IRAM_ATTR Keyboard::isrUp()    { if (_instance) _instance->_tbDy--; }
void IRAM_ATTR Keyboard::isrDown()  { if (_instance) _instance->_tbDy++; }
void IRAM_ATTR Keyboard::isrLeft()  { if (_instance) _instance->_tbDx--; }
void IRAM_ATTR Keyboard::isrRight() { if (_instance) _instance->_tbDx++; }
void IRAM_ATTR Keyboard::isrClick() {}

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
    // Do not use interrupt here: we need press/release timing for long-press BACK.
    pinMode(TDECK_TB_CLICK, INPUT_PULLUP);
    _tbPressed = (digitalRead(TDECK_TB_CLICK) == LOW);
    _tbLongPressSent = false;
    _tbPressStartMs = 0;

    _initialized = true;
    Serial.println("[KB] Keyboard + trackball ready");
    return true;
}

void Keyboard::update() {
    if (!_initialized) return;
    const uint32_t now = millis();

    // Read keyboard even if INT is unreliable; INT still fast-paths new keys.
    if (digitalRead(TDECK_KB_INT) == LOW || (now - _lastKbPollMs) >= KB_POLL_MS) {
        _lastKbPollMs = now;

        // Drain a few bytes per loop; keyboard may queue multiple keypresses.
        for (int i = 0; i < 4; i++) {
            char k = _readI2CKey();
            if (k == KEY_NONE) break;

            // Handle modifiers
            if (k == 0x01) { _altHeld   = !_altHeld;   continue; }
            if (k == 0x02) { _shiftHeld = !_shiftHeld; continue; }

            _enqueueKey(k);
        }
    }

    // Trackball button: short press = click, long press = global back (ESC).
    bool pressed = (digitalRead(TDECK_TB_CLICK) == LOW);
    if (pressed && !_tbPressed) {
        _tbPressed = true;
        _tbLongPressSent = false;
        _tbPressStartMs = now;
    } else if (!pressed && _tbPressed) {
        if (!_tbLongPressSent) _tbClick = true;
        _tbPressed = false;
        _tbLongPressSent = false;
        _tbPressStartMs = 0;
    } else if (pressed && !_tbLongPressSent &&
               _tbPressStartMs > 0 &&
               (now - _tbPressStartMs) >= TB_LONG_PRESS_MS) {
        _tbLongPressSent = true;
        _tbClick = false;         // prevent accidental short-click action
        _enqueueKey(KEY_ESC);     // global back
    }
}

char Keyboard::getKey() {
    return _dequeueKey();
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
            case 0x09: return KEY_TAB;
            case 0x0A: return KEY_ENTER;   // '\n' — some keyboard firmware variants
            case 0x0D: return KEY_ENTER;   // '\r' — standard
            case 0x7F: return KEY_BACKSPACE; // DEL from some firmware variants
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

bool Keyboard::_enqueueKey(char key) {
    uint8_t next = (uint8_t)((_qHead + 1) % KEY_Q_CAP);
    if (next == _qTail) {
        // Queue full: drop oldest key to keep latest input responsive.
        _qTail = (uint8_t)((_qTail + 1) % KEY_Q_CAP);
    }
    _keyQueue[_qHead] = key;
    _qHead = next;
    return true;
}

char Keyboard::_dequeueKey() {
    if (_qHead == _qTail) return KEY_NONE;
    char key = _keyQueue[_qTail];
    _qTail = (uint8_t)((_qTail + 1) % KEY_Q_CAP);
    return key;
}
