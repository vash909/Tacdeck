#include "WeatherScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include <Arduino.h>
#include <cstring>

WeatherScreen::WeatherScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void WeatherScreen::onEnter() {
    memset(_sensors, 0, sizeof(_sensors));
    _sensorCount = 0;

    // OOK mode for 433.92 MHz ISM sensors
    _radio->ookBegin(WEATHER_FREQ, WEATHER_OOK_BITRATE, WEATHER_OOK_RXBW);
    _radio->ookStartRx();
    _dirty = true;
}

void WeatherScreen::onExit() {
    _radio->standby();
}

void WeatherScreen::update() {
    _pollRx();

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    // Periodic partial refresh (age timers, RSSI text) without full-screen redraw.
    static uint32_t lastLiveRefresh = 0;
    if (millis() - lastLiveRefresh > 1000) {
        lastLiveRefresh = millis();
        _drawStatusLine();
        if (_showRaw) _drawRawLog();
        else          _drawSensorGrid();
    }
}

void WeatherScreen::_pollRx() {
    if (!_radio->fskAvailable()) return;
    RxPacket pkt;
    if (!_radio->fskRead(pkt) || !pkt.valid) return;

    _listenRSSI = pkt.rssi;

    WeatherSensor sensor;
    memset(&sensor, 0, sizeof(sensor));

    bool decoded = false;

    // Try Oregon Scientific first
    if (pkt.len >= 8 && _decodeOregon(pkt.data, pkt.len, sensor)) {
        decoded = true;
        strlcpy(sensor.type, "Oregon", sizeof(sensor.type));
    }
    // Try generic OOK
    else if (pkt.len >= 4 && _decodeGenericOOK(pkt.data, pkt.len, sensor)) {
        decoded = true;
    }

    if (decoded) {
        sensor.lastSeenMs = millis();
        sensor.valid      = true;
        _addOrUpdateSensor(sensor);

        char line[54];
        snprintf(line, sizeof(line), "[%s] %.1fC %.0f%% %.0fdBm",
                 sensor.id, sensor.tempC, sensor.humRH, (double)pkt.rssi);
        _rawLog.addLine(line);
        if (!_dirty) {
            _drawStatusLine();
            if (_showRaw) _drawRawLog();
            else          _drawSensorGrid();
        }
    } else {
        // Log raw hex
        char line[54];
        char hex[30] = "";
        for (size_t i = 0; i < pkt.len && i < 12; i++) {
            char h[4];
            snprintf(h, sizeof(h), "%02X ", pkt.data[i]);
            strncat(hex, h, sizeof(hex) - strlen(hex) - 1);
        }
        snprintf(line, sizeof(line), "[RAW] %s (%.0fdBm)", hex, (double)pkt.rssi);
        _rawLog.addLine(line);
        if (!_dirty && _showRaw) _drawRawLog();
    }
}

// ---- Oregon Scientific v2.1 minimal decoder ----
bool WeatherScreen::_decodeOregon(const uint8_t* data, size_t len,
                                    WeatherSensor& out) {
    if (len < 8) return false;
    // Oregon v2.1 sync: nibble 0 = 0xA
    if ((data[0] >> 4) != 0xA) return false;

    // Sensor ID (nibbles 1-4)
    uint16_t id = ((uint16_t)(data[0] & 0x0F) << 12) |
                  ((uint16_t)(data[1]) << 4) |
                  ((uint16_t)(data[2] >> 4));
    snprintf(out.id, sizeof(out.id), "%04X", id);

    // Channel (nibble 5)
    uint8_t ch = data[2] & 0x07;
    snprintf(out.type, sizeof(out.type), "Ore%04X-ch%d", id, ch);

    // Temperature (nibbles 10-12, BCD, 1/10 C)
    if (len >= 7) {
        int tempRaw = ((data[5] & 0x0F) * 100) +
                      ((data[6] >> 4) * 10)  +
                       (data[6] & 0x0F);
        bool neg = (data[5] >> 7) & 1;
        out.tempC = tempRaw / 10.0f * (neg ? -1 : 1);
    }

    // Humidity (nibbles 14-15, BCD)
    if (len >= 8) {
        out.humRH = (float)(((data[7] >> 4) * 10) + (data[7] & 0x0F));
    }

    // Battery: nibble 9 bit 2
    if (len >= 5) {
        out.battery = (data[4] & 0x04) ? 10 : 100;
    }

    return true;
}

// ---- Generic OOK PWM/PPM sensor (heuristic decode) ----
bool WeatherScreen::_decodeGenericOOK(const uint8_t* data, size_t len,
                                        WeatherSensor& out) {
    if (len < 4) return false;

    // Look for plausible sensor ID + temp pattern
    uint32_t id = ((uint32_t)data[0] << 8) | data[1];
    snprintf(out.id,   sizeof(out.id),   "%04lX", (unsigned long)id);
    snprintf(out.type, sizeof(out.type), "Generic");

    // Very rough temp decode (varies by manufacturer)
    int16_t rawTemp = ((int16_t)(data[2] & 0x0F) << 8) | data[3];
    if (rawTemp & 0x800) rawTemp |= 0xF000;  // sign extend 12-bit
    out.tempC  = rawTemp / 10.0f;
    out.humRH  = (len >= 5) ? (float)(data[4] & 0x7F) : 0;
    out.battery= 100;

    // Sanity check: temp must be in -40 to +60 C
    return (out.tempC >= -40.f && out.tempC <= 60.f);
}

void WeatherScreen::_addOrUpdateSensor(const WeatherSensor& s) {
    // Check if we already have this sensor
    for (int i = 0; i < _sensorCount; i++) {
        if (strcmp(_sensors[i].id, s.id) == 0) {
            _sensors[i] = s;
            return;
        }
    }
    if (_sensorCount < MAX_SENSORS) {
        _sensors[_sensorCount++] = s;
    }
}

// ================================================================
void WeatherScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Weather Sensors", COL_WEATHER);

    _drawStatusLine();

    // Tabs
    drawButton(&gfx,   0, 58, 90, 12, "Sensors", !_showRaw, COL_WEATHER);
    drawButton(&gfx,  92, 58, 90, 12, "Raw Log",  _showRaw, COL_WEATHER);
    gfx.drawFastHLine(0, 72, 320, COL_DIVIDER);

    if (_showRaw) _drawRawLog();
    else          _drawSensorGrid();

    drawHints(&gfx, "HOLD=Back", "TAB=Switch", "R=Clear");
}

void WeatherScreen::_drawStatusLine() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, 47, 320, 10, COL_BG);

    char stLine[40];
    snprintf(stLine, sizeof(stLine),
             "433.92MHz OOK  RSSI:%.0fdBm  %d sensors",
             (double)_listenRSSI, _sensorCount);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(2, 47);
    gfx.print(stLine);
    gfx.drawFastHLine(0, 57, 320, COL_DIVIDER);
}

void WeatherScreen::_drawSensorGrid() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 73;
    gfx.fillRect(0, Y0, 320, 140, COL_BG);

    if (_sensorCount == 0) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(50, Y0 + 40);
        gfx.print("Listening...");
        gfx.setTextSize(FONT_TINY);
        gfx.setCursor(30, Y0 + 60);
        gfx.print("Oregon Sci / Acurite / Generic");
        return;
    }

    for (int i = 0; i < _sensorCount && i < 4; i++) {
        const WeatherSensor& s = _sensors[i];
        int x = (i % 2) * 160;
        int y = Y0 + (i / 2) * 70;

        bool sel = (i == _viewSel);
        gfx.fillRect(x + 1, y, 158, 68,
                     sel ? COL_BG_PANEL : COL_BG);
        gfx.drawRect(x, y, 160, 69,
                     sel ? COL_WEATHER : COL_BORDER);

        // Sensor ID + type
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_WEATHER, sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(x + 4, y + 3);
        gfx.print(s.type);
        gfx.setTextColor(COL_TEXT_DIM, sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(x + 4, y + 13);
        gfx.print(s.id);

        // Temperature
        char buf[10];
        snprintf(buf, sizeof(buf), "%.1fC", s.tempC);
        gfx.setTextColor(s.tempC > 0 ? COL_ORANGE : COL_CYAN,
                         sel ? COL_BG_PANEL : COL_BG);
        gfx.setTextSize(FONT_MEDIUM);
        gfx.setCursor(x + 4, y + 24);
        gfx.print(buf);

        // Humidity
        snprintf(buf, sizeof(buf), "%.0f%%", s.humRH);
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_CYAN, sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(x + 4, y + 46);
        gfx.print(buf);

        // Age
        uint32_t age = (millis() - s.lastSeenMs) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", (unsigned long)age);
        gfx.setTextColor(COL_TEXT_DIM, sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(x + 60, y + 46);
        gfx.print(buf);

        // Battery icon (small)
        uint16_t batCol = s.battery > 50 ? COL_GREEN :
                          s.battery > 20 ? COL_YELLOW : COL_RED;
        gfx.fillRect(x + 140, y + 3, (int)(s.battery * 16 / 100), 5, batCol);
        gfx.drawRect(x + 139, y + 2, 18, 7, COL_BORDER);
    }
}

void WeatherScreen::_drawRawLog() {
    _rawLog.draw(&_disp->gfx(), 0, 73, 320, 140, COL_WEATHER);
}

// ================================================================
void WeatherScreen::onKey(char key) {
    if (key == KEY_TAB || key == '\t') {
        _showRaw = !_showRaw;
        _dirty = true;
        return;
    }
    if (key == 'r' || key == 'R') {
        _sensorCount = 0;
        _dirty = true;
        return;
    }
}

void WeatherScreen::onTrackball(int dx, int dy, bool click) {
    if (dx != 0 || dy != 0) {
        _showRaw = !_showRaw;
        _dirty = true;
    }
}
