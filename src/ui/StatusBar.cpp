#include "StatusBar.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Radio.h"
#include <Arduino.h>

void StatusBar::begin(Display* disp, GPS* gps, Radio* radio) {
    _disp  = disp;
    _gps   = gps;
    _radio = radio;
    _dirty = true;
}

void StatusBar::setModeName(const char* name, uint16_t color) {
    strncpy(_modeName, name, sizeof(_modeName) - 1);
    _modeColor = color;
    _dirty = true;
}

void StatusBar::update() {
    if (!_disp) return;
    _drawFull();
    _dirty = false;
}

void StatusBar::_drawFull() {
    auto& gfx = _disp->gfx();
    constexpr int H = STATUS_BAR_H;

    // Background
    gfx.fillRect(0, 0, 320, H, COL_BG_HEADER);
    gfx.drawFastHLine(0, H - 1, 320, COL_DIVIDER);

    // -- Mode name (left) --
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(_modeColor, COL_BG_HEADER);
    gfx.setCursor(4, 8);
    gfx.print(_modeName);

    // -- GPS status (center-left) --
    if (_gps) {
        bool fix  = _gps->hasFix();
        uint8_t s = _gps->sats();
        char buf[16];

        if (fix) {
            snprintf(buf, sizeof(buf), "GPS %usat", s);
            gfx.setTextColor(COL_GREEN, COL_BG_HEADER);
        } else {
            snprintf(buf, sizeof(buf), "No GPS");
            gfx.setTextColor(COL_RED, COL_BG_HEADER);
        }
        gfx.setCursor(80, 8);
        gfx.print(buf);
    }

    // -- UTC time (center) --
    if (_gps && _gps->hasFix()) {
        const GpsData& d = _gps->data();
        char timebuf[10];
        snprintf(timebuf, sizeof(timebuf), "%02u:%02u:%02u",
                 d.hour, d.minute, d.second);
        gfx.setTextColor(COL_CYAN, COL_BG_HEADER);
        gfx.setCursor(152, 8);
        gfx.print(timebuf);
    } else {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
        gfx.setCursor(152, 8);
        gfx.print("--:--:--");
    }

    // -- RSSI indicator (right area) --
    if (_radio && _radio->ready()) {
        float rssi = _radio->getRSSI();
        drawRSSIBar(&gfx, 240, 6, 30, 12, rssi);
    }

    // -- Battery (far right) --
    uint8_t batPct = _readBatteryPct();
    _drawBattery(276, 4, batPct);
}

void StatusBar::_drawBattery(int x, int y, uint8_t pct) {
    auto& gfx = _disp->gfx();
    // Battery icon 20x12 + 2x6 terminal
    constexpr int W = 20, H = 12;
    uint16_t col = pct > 50 ? COL_GREEN : (pct > 20 ? COL_YELLOW : COL_RED);

    gfx.drawRect(x, y, W, H, COL_TEXT_DIM);
    gfx.fillRect(x + W, y + 3, 2, H - 6, COL_TEXT_DIM);  // terminal nub

    int fill = (int)(pct * (W - 2) / 100);
    gfx.fillRect(x + 1, y + 1, fill, H - 2, col);
    gfx.fillRect(x + 1 + fill, y + 1, W - 2 - fill, H - 2, COL_BG);

    // Percentage text inside battery
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(pct > 50 ? COL_BG : COL_TEXT, COL_BG);
    char buf[5]; snprintf(buf, sizeof(buf), "%u%%", pct);
    int tw = strlen(buf) * 6;
    gfx.setCursor(x + (W - tw) / 2, y + 2);
    gfx.print(buf);
}

uint8_t StatusBar::_readBatteryPct() {
    // T-Deck battery ADC: GPIO4, voltage divider 100k/100k
    // BAT_ADC reads 0-4095, Vref=3.3V, divider factor=2
    uint16_t raw = analogRead(TDECK_BATTERY_ADC);
    float vBat   = (raw / 4095.0f) * 3.3f * 2.0f;
    // LiPo: 3.0V=0%, 4.2V=100%
    float pct = (vBat - 3.0f) / 1.2f * 100.0f;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}
