#include "WSPRScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>

WSPRScreen::WSPRScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void WSPRScreen::onEnter() {
    Storage::getString(NVS_KEY_WSPR_CALL, _callsign, sizeof(_callsign), "N0CALL");
    Storage::getString(NVS_KEY_WSPR_GRID, _grid,     sizeof(_grid),     "AA00");
    _powerDbm = Storage::getInt(NVS_KEY_WSPR_PWR, WSPR_DEFAULT_POWER_DBM);

    // If GPS fix available, update grid
    if (_gps && _gps->hasFix()) {
        char g6[8];
        GPS::latLonToGrid(_gps->lat(), _gps->lon(), g6, sizeof(g6));
        g6[4] = '\0';   // WSPR uses 4-char grid
        memcpy(_grid, g6, 5);
    }

    _dirty = true;
}

void WSPRScreen::onExit() {
    _stopBeacon();
    _radio->standby();
}

void WSPRScreen::update() {
    // Manage TX state
    if (_txing && millis() - _txStartMs > WSPR_TX_MS) {
        _txing = false;
        _radio->standby();
        char line[48];
        snprintf(line, sizeof(line), "[TX#%d] Done  %.3f MHz",
                 _txCount, (double)_freq);
        _log.addLine(line);
        _nextTxMs = millis() + WSPR_SLOT_SECONDS * 1000UL;
        _dirty = true;
    }

    if (_enabled && !_txing && millis() >= _nextTxMs) {
        _startBeacon();
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    static uint32_t lastSec = 0;
    if (millis() - lastSec > 1000) { lastSec = millis(); _dirty = true; }
}

void WSPRScreen::_startBeacon() {
    if (!_radio->initWSPR(_freq, 0.0f)) {
        _log.addLine("[ERR] WSPR init failed");
        _dirty = true;
        return;
    }

    WSPRClient* w = _radio->wspr();
    if (!w) return;

    // WSPR transmit (blocking ~110s — run in background in real use)
    // For now we start it and let it run
    int16_t s = w->transmit(_callsign, _grid, _powerDbm);
    if (s == RADIOLIB_ERR_NONE) {
        _txCount++;
        _txing    = true;
        _txStartMs= millis();
    } else {
        _log.addLine("[ERR] WSPR TX failed");
    }
    _dirty = true;
}

void WSPRScreen::_stopBeacon() {
    _enabled = false;
    _txing   = false;
    _radio->standby();
}

bool WSPRScreen::_isEvenMinute() {
    if (!_gps || !_gps->hasFix()) return false;
    return (_gps->data().minute % 2) == 0 && _gps->data().second < 2;
}

void WSPRScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "WSPR Beacon", COL_WSPR);

    int y = 44;

    // Status badge
    if (_txing) {
        uint32_t elapsed = (millis() - _txStartMs) / 1000;
        uint32_t remain  = WSPR_TX_MS / 1000 - elapsed;
        gfx.fillRoundRect(200, y, 116, 14, 3, COL_WSPR);
        gfx.setTextColor(COL_BG, COL_WSPR);
        gfx.setTextSize(FONT_TINY);
        char buf[20];
        snprintf(buf, sizeof(buf), "TX %lus / 110s", (unsigned long)elapsed);
        gfx.setCursor(204, y + 3);
        gfx.print(buf);
        drawProgressBar(&gfx, 0, y, 196, 14,
                        (float)elapsed / 110.f, COL_WSPR);
    } else if (_enabled) {
        gfx.fillRoundRect(200, y, 116, 14, 3, COL_GREEN_DIM);
        gfx.setTextColor(COL_TEXT, COL_GREEN_DIM);
        gfx.setTextSize(FONT_TINY);
        uint32_t waitSec = (_nextTxMs > millis()) ? (_nextTxMs - millis()) / 1000 : 0;
        char buf[20];
        snprintf(buf, sizeof(buf), "Next: %lus", (unsigned long)waitSec);
        gfx.setCursor(204, y + 3);
        gfx.print(buf);
    } else {
        drawButton(&gfx, 200, y, 116, 14, "BEACON OFF", false, COL_WSPR);
    }

    gfx.drawFastHLine(0, y + 16, 320, COL_DIVIDER);
    y += 18;

    drawKV(&gfx, 4, y,      "Callsign : ", _callsign, COL_TEXT_DIM, COL_WSPR);
    drawKV(&gfx, 4, y + 12, "Grid     : ", _grid,     COL_TEXT_DIM, COL_WSPR);

    char pwrBuf[8], freqBuf[16];
    snprintf(pwrBuf,  sizeof(pwrBuf),  "%d dBm", _powerDbm);
    snprintf(freqBuf, sizeof(freqBuf), "%.3f MHz", (double)_freq);
    drawKV(&gfx, 4, y + 24, "Power    : ", pwrBuf,  COL_TEXT_DIM, COL_TEXT);
    drawKV(&gfx, 4, y + 36, "Frequency: ", freqBuf, COL_TEXT_DIM, COL_TEXT);

    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%d", _txCount);
    drawKV(&gfx, 4, y + 48, "TX count : ", cntBuf, COL_TEXT_DIM, COL_GREEN);

    gfx.drawFastHLine(0, y + 60, 320, COL_DIVIDER);
    _log.draw(&gfx, 0, y + 62, 320, 88, COL_WSPR);

    drawHints(&gfx, "ESC=Back",
              _enabled ? "E=Disable" : "E=Enable",
              "T=TX Now");
}

void WSPRScreen::onKey(char key) {
    if (key == 'e' || key == 'E') {
        _enabled = !_enabled;
        if (_enabled) {
            _nextTxMs = millis() + 2000;
            _log.addLine("[INFO] Beacon enabled");
        } else {
            _stopBeacon();
            _log.addLine("[INFO] Beacon disabled");
        }
        _dirty = true;
        return;
    }
    if (key == 't' || key == 'T') {
        if (!_txing) _startBeacon();
        return;
    }
}

void WSPRScreen::onTrackball(int dx, int dy, bool click) {
    if (click) onKey('e');
}
