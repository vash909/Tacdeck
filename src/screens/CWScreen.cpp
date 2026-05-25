#include "CWScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>

CWScreen::CWScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void CWScreen::onEnter() {
    Storage::getString(NVS_KEY_CW_TEXT, _beaconText,
                       sizeof(_beaconText), "CQ DE TDECK K");
    _msgInput.clear();
    _editing = false;
    _dirty   = true;
}

void CWScreen::onExit() {
    _radio->standby();
    _beaconEnabled = false;
}

void CWScreen::update() {
    // Auto-beacon
    if (_beaconEnabled && !_txing) {
        uint32_t elapsed = millis() - _lastTxMs;
        if (elapsed >= _txIntervalSec * 1000UL) {
            _txMessage(_beaconText);
        }
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    static uint32_t lastSec = 0;
    if (millis() - lastSec > 1000) { lastSec = millis(); _dirty = true; }
}

void CWScreen::_txMessage(const char* msg) {
    if (!_radio->initMorse(_freq, _power)) {
        _txLog.addLine("[ERR] Morse init failed");
        _dirty = true;
        return;
    }

    MorseClient* m = _radio->morse();
    if (!m) return;

    _txing = true;
    _dirty = true;
    _drawAll();

    m->print(msg);

    char line[54];
    snprintf(line, sizeof(line), "[TX] %s", msg);
    _txLog.addLine(line);

    _txing    = false;
    _lastTxMs = millis();

    // Return to standby
    _radio->standby();
    _dirty = true;
}

// ================================================================
void CWScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "CW / Morse Beacon", COL_CW);

    int y = 46;

    // Freq / WPM / Power row
    char cfgBuf[40];
    snprintf(cfgBuf, sizeof(cfgBuf), "%.3f MHz  %d WPM  %ddBm",
             (double)_freq, _wpm, (int)_power);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y);
    gfx.print(cfgBuf);
    y += 12;
    gfx.drawFastHLine(0, y, 320, COL_DIVIDER);
    y += 2;

    // Beacon text
    drawKV(&gfx, 4, y, "Text: ", _beaconText, COL_TEXT_DIM, COL_CW);
    y += 12;

    // Interval
    char intBuf[12];
    snprintf(intBuf, sizeof(intBuf), "%lus", (unsigned long)_txIntervalSec);
    drawKV(&gfx, 4, y, "Interval: ", intBuf, COL_TEXT_DIM, COL_TEXT);
    y += 12;

    // Beacon status
    if (_txing) {
        gfx.setTextColor(COL_CW, COL_BG);
        gfx.setTextSize(FONT_MEDIUM);
        gfx.setCursor(60, y + 4);
        gfx.print("TRANSMITTING...");
        y += 30;
    } else if (_beaconEnabled) {
        uint32_t nextTx = _txIntervalSec - (millis() - _lastTxMs) / 1000;
        char nBuf[24];
        snprintf(nBuf, sizeof(nBuf), "Beacon ON  next: %lus",
                 (unsigned long)nextTx);
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_GREEN, COL_BG);
        gfx.setCursor(4, y);
        gfx.print(nBuf);
        y += 12;
    } else {
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(4, y);
        gfx.print("Beacon OFF");
        y += 12;
    }

    gfx.drawFastHLine(0, y, 320, COL_DIVIDER);
    y += 2;

    // Manual input
    if (_editing) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_TINY);
        gfx.setCursor(4, y);
        gfx.print("Send now:");
        _msgInput.draw(&gfx, 4, y + 10, 280, COL_CW);
        y += 26;
    }

    // Morse visual for beacon text (dots/dashes)
    char morseStr[80];
    CWUtils::textToMorse(_beaconText, morseStr, sizeof(morseStr));
    _drawMorseVisual(morseStr);

    gfx.drawFastHLine(0, y + 40, 320, COL_DIVIDER);
    _txLog.draw(&gfx, 0, y + 42, 320, 64, COL_CW);

    drawHints(&gfx, "ESC=Back",
              _beaconEnabled ? "B=Disable" : "B=Beacon",
              "T=Type+TX");
}

void CWScreen::_drawMorseVisual(const char* morse) {
    auto& gfx = _disp->gfx();
    constexpr int Y = 155, DOT_W = 4, DASH_W = 12, H = 8, GAP = 2;
    int x = 4;

    gfx.fillRect(0, Y, 320, H + 4, COL_BG);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(x, Y - 8);
    gfx.print("Morse:");

    for (size_t i = 0; morse[i] && x < 316; i++) {
        char c = morse[i];
        if (c == '.') {
            gfx.fillRect(x, Y, DOT_W, H, COL_CW);
            x += DOT_W + GAP;
        } else if (c == '-') {
            gfx.fillRect(x, Y, DASH_W, H, COL_CW);
            x += DASH_W + GAP;
        } else if (c == ' ') {
            x += DASH_W;   // word space
        }
    }
}

// ================================================================
void CWScreen::onKey(char key) {
    if (key == 'b' || key == 'B') {
        _beaconEnabled = !_beaconEnabled;
        if (_beaconEnabled) _lastTxMs = millis() - _txIntervalSec * 1000UL;
        _dirty = true;
        return;
    }
    if (key == 't' || key == 'T') {
        if (!_editing) {
            _editing = true;
            _msgInput.clear();
        } else {
            _editing = false;
        }
        _dirty = true;
        return;
    }
    if (_editing) {
        if (key == KEY_ENTER) {
            if (strlen(_msgInput.buf) > 0) {
                _txMessage(_msgInput.buf);
                _msgInput.clear();
                _editing = false;
            }
            return;
        }
        _msgInput.input(key);
        _drawAll();
        return;
    }
    if (key == '+') { _wpm = min(_wpm + 2, 40); _dirty = true; }
    if (key == '-') { _wpm = max(_wpm - 2, 5);  _dirty = true; }
}

void CWScreen::onTrackball(int dx, int dy, bool click) {
    if (click) {
        if (_editing) onKey(KEY_ENTER);
        else          onKey('t');
    }
    if (dy < 0) { _wpm = min(_wpm + 2, 40); _dirty = true; }
    if (dy > 0) { _wpm = max(_wpm - 2, 5);  _dirty = true; }
}
