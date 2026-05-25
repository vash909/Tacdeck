#include "Display.h"
#include <Arduino.h>

bool Display::begin() {
    if (_initialized) return true;

    // Power on the board peripherals
    pinMode(TDECK_POWERON, OUTPUT);
    digitalWrite(TDECK_POWERON, HIGH);
    delay(100);

    _gfx.init();
    _gfx.setRotation(1);           // landscape, keyboard at bottom
    _gfx.setBrightness(_brightness);
    _gfx.fillScreen(TFT_BLACK);

    _initialized = true;
    return true;
}

void Display::setBrightness(uint8_t value) {
    _brightness = value;
    _gfx.setBrightness(value);
}

bool Display::createSprite(int w, int h) {
    _sprite.deleteSprite();
    return (_sprite.createSprite(w, h) != nullptr);
}

void Display::pushSprite(int x, int y) {
    _sprite.pushSprite(x, y);
}
