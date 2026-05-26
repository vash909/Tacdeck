#include "StatusBar.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Radio.h"
#include "Widgets.h"
#include <Arduino.h>

// ================================================================
// Layout constants (x positions inside the 320×24 bar)
// ================================================================
static constexpr int SB_H      = STATUS_BAR_H;  // 24px
static constexpr int SB_W      = 320;

static constexpr int X_MODE    = 4;             // mode label start
static constexpr int W_MODE    = 72;
static constexpr int X_GPS     = 80;
static constexpr int W_GPS     = 56;
static constexpr int X_TIME    = 140;
static constexpr int W_TIME    = 56;
static constexpr int X_RSSI    = 200;           // RSSI text (e.g. "-100dB")
static constexpr int W_RSSI    = 42;
static constexpr int X_BAT     = 246;           // battery icon
static constexpr int W_BAT     = 74;            // icon(22)+nub(3)+gap(2)+text(24)+margin
static constexpr int TEXT_Y    = 8;

// ================================================================
void StatusBar::begin(Display* disp, GPS* gps, Radio* radio) {
    _disp      = disp;
    _gps       = gps;
    _radio     = radio;
    _dirty     = true;
    _lastBatPct= 255;   // force first draw
    _lastFix   = false;
    _lastSats  = 255;
    _lastTime[0] = '\0';
}

void StatusBar::setModeName(const char* name, uint16_t color) {
    strncpy(_modeName, name ? name : "---", sizeof(_modeName) - 1);
    _modeName[sizeof(_modeName) - 1] = '\0';
    _modeColor = color;
    _dirty     = true;
}

// ================================================================
// update() — called at 1 Hz. Performs differential redraws:
// only the elements whose value actually changed are touched.
// ================================================================
void StatusBar::update() {
    if (!_disp) return;

    if (_dirty) {
        // Full redraw (screen change, first boot, etc.)
        _drawBackground();
        _drawMode();
        _drawGPS();
        _drawTime();
        _drawRSSI();
        _drawBattery();
        _dirty = false;
        return;
    }

    // --- Differential updates ---
    bool changed = false;

    // Mode — only changes via setModeName(); already handled by _dirty flag
    // (nothing to do here)

    // GPS
    bool  newFix  = _gps ? _gps->hasFix()  : false;
    uint8_t newSats = _gps ? _gps->sats()  : 0;
    if (newFix != _lastFix || newSats != _lastSats) {
        _clearRegion(X_GPS, W_GPS);
        _drawGPS();
        changed = true;
    }

    // Time
    char newTime[9] = "--:--:--";
    if (_gps && _gps->hasFix()) {
        const GpsData& d = _gps->data();
        snprintf(newTime, sizeof(newTime), "%02u:%02u:%02u",
                 d.hour, d.minute, d.second);
    }
    if (strcmp(newTime, _lastTime) != 0) {
        _clearRegion(X_TIME, W_TIME);
        _drawTime();
        changed = true;
    }

    // RSSI — update every tick but only repaint if meaningfully different
    if (_radio && _radio->ready()) {
        float r = _radio->getRSSI();
        if (fabsf(r - _lastRSSI) >= 2.0f) {
            _clearRegion(X_RSSI, W_RSSI);
            _drawRSSI();
            changed = true;
        }
    }

    // Battery — smoothed ADC; only redraw when % changes
    uint8_t newPct = _readBatteryPct();
    if (newPct != _lastBatPct) {
        _clearRegion(X_BAT, W_BAT);
        _drawBattery();
        changed = true;
    }

    (void)changed; // suppress unused warning
}

// ================================================================
// Internal helpers
// ================================================================

void StatusBar::_drawBackground() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, 0, SB_W, SB_H, COL_BG_HEADER);
    gfx.drawFastHLine(0, SB_H - 1, SB_W, COL_DIVIDER);
}

// Erase a vertical slice of the bar (prepare for redraw of one element)
void StatusBar::_clearRegion(int x, int w) {
    auto& gfx = _disp->gfx();
    gfx.fillRect(x, 0, w, SB_H - 1, COL_BG_HEADER);
}

void StatusBar::_drawMode() {
    auto& gfx = _disp->gfx();
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(_modeColor, COL_BG_HEADER);
    gfx.setCursor(X_MODE, TEXT_Y);
    gfx.print(_modeName);
}

void StatusBar::_drawGPS() {
    if (!_gps) return;
    auto& gfx = _disp->gfx();
    _lastFix  = _gps->hasFix();
    _lastSats = _gps->sats();

    char buf[12];
    if (_lastFix) {
        snprintf(buf, sizeof(buf), "GPS %u\xB0", _lastSats);  // degree symbol
        gfx.setTextColor(COL_GREEN, COL_BG_HEADER);
    } else {
        snprintf(buf, sizeof(buf), "No GPS");
        gfx.setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
    }
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(X_GPS, TEXT_Y);
    gfx.print(buf);
}

void StatusBar::_drawTime() {
    auto& gfx = _disp->gfx();
    if (_gps && _gps->hasFix()) {
        const GpsData& d = _gps->data();
        snprintf(_lastTime, sizeof(_lastTime), "%02u:%02u:%02u",
                 d.hour, d.minute, d.second);
        gfx.setTextColor(COL_CYAN, COL_BG_HEADER);
    } else {
        strncpy(_lastTime, "--:--:--", sizeof(_lastTime));
        gfx.setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
    }
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(X_TIME, TEXT_Y);
    gfx.print(_lastTime);
}

void StatusBar::_drawRSSI() {
    if (!_radio || !_radio->ready()) return;
    auto& gfx = _disp->gfx();
    _lastRSSI = _radio->getRSSI();

    char buf[8];
    snprintf(buf, sizeof(buf), "%ddB", (int)_lastRSSI);

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(rssiToColor(_lastRSSI), COL_BG_HEADER);
    // Right-align within the RSSI region
    int tw = strlen(buf) * 6;
    gfx.setCursor(X_RSSI + W_RSSI - tw, TEXT_Y);
    gfx.print(buf);
}

void StatusBar::_drawBattery() {
    auto& gfx = _disp->gfx();
    _lastBatPct = _readBatteryPct();
    uint8_t pct = _lastBatPct;

    constexpr int x = X_BAT;
    constexpr int y = 4;
    constexpr int W = 22;
    constexpr int H = 16;

    uint16_t col = pct > 60 ? COL_GREEN
                 : pct > 25 ? COL_YELLOW
                 :             COL_RED;

    // Battery outline
    gfx.drawRect(x, y, W, H, COL_TEXT_DIM);
    // Terminal nub
    gfx.fillRect(x + W, y + 5, 3, H - 10, COL_TEXT_DIM);
    // Fill bar
    int fill = (int)((uint32_t)pct * (W - 2) / 100);
    if (fill > W - 2) fill = W - 2;
    gfx.fillRect(x + 1, y + 1, fill,                H - 2, col);
    gfx.fillRect(x + 1 + fill, y + 1, W-2-fill, H - 2, COL_BG_HEADER);

    // Percentage text to the right of the icon
    char buf[5];
    snprintf(buf, sizeof(buf), "%u%%", pct);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(col, COL_BG_HEADER);
    gfx.setCursor(x + W + 5, TEXT_Y);
    gfx.print(buf);
}

// ================================================================
// Battery ADC — 8-sample oversampling + hysteresis
// ================================================================
uint8_t StatusBar::_readBatteryPct() {
    // Oversample to reduce ADC noise
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += analogRead(TDECK_BATTERY_ADC);
        delayMicroseconds(200);
    }
    float raw  = sum / 8.0f;
    float vBat = (raw / 4095.0f) * 3.3f * 2.0f;  // voltage divider ×2
    float pct  = (vBat - 3.0f) / 1.2f * 100.0f;  // 3.0V=0%, 4.2V=100%
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint8_t newPct = (uint8_t)pct;

    // Hysteresis: only accept change if it differs by ≥2% from last value,
    // or if this is the first reading (lastBatPct == 255)
    if (_lastBatPct == 255) return newPct;
    int diff = (int)newPct - (int)_lastBatPct;
    if (diff < 0) diff = -diff;
    return (diff >= 2) ? newPct : _lastBatPct;
}
