#include "RTTYScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include <Arduino.h>

constexpr float    RTTYScreen::BAUD_OPTIONS[];
constexpr uint32_t RTTYScreen::SHIFT_OPTIONS[];

RTTYScreen::RTTYScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void RTTYScreen::onEnter() {
    _txInput.clear();
    _txMode   = false;
    _txActive = false;
    _rxMode   = false;
    _dirty    = true;
}

void RTTYScreen::onExit() {
    _stopTX();
    _stopRX();
    _radio->standby();
}

void RTTYScreen::update() {
    if (_rxMode) _pollRx();
    if (_dirty) {
        _drawAll();
        _dirty = false;
    }
}

void RTTYScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "RTTY", COL_RTTY);

    // Config bar
    char cfgLine[48];
    snprintf(cfgLine, sizeof(cfgLine),
             "%.3fMHz  %.1fbaud  %uHz shift",
             (double)_freq, (double)_baud, _shift);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, 47);
    gfx.print(cfgLine);
    gfx.drawFastHLine(0, 57, 320, COL_DIVIDER);

    // Mode buttons
    drawButton(&gfx,   0, 58, 80, 14, "RX",     _rxMode,   COL_RTTY);
    drawButton(&gfx,  82, 58, 80, 14, "TX Text", _txMode,   COL_RTTY);
    drawButton(&gfx, 164, 58, 60, 14, "Baud+",   false,     COL_RTTY);
    drawButton(&gfx, 226, 58, 60, 14, "Shift+",  false,     COL_RTTY);

    if (_txMode) _drawTXPanel();
    else         _drawRXPanel();

    const char* hint = _txMode ? "ENTER=Send  HOLD=Stop TX" : "HOLD=Back";
    drawHints(&gfx, "HOLD=Back", nullptr, hint);
}

void RTTYScreen::_drawRXPanel() {
    auto& gfx = _disp->gfx();
    if (!_rxMode) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, 100);
        gfx.print("Press R to start RX");
    } else {
        gfx.setTextColor(COL_GREEN, COL_BG);
        gfx.setTextSize(FONT_TINY);
        gfx.setCursor(4, 74);
        gfx.print("RECEIVING...");
        float rssi = _radio->getRSSI();
        drawRSSIBar(&gfx, 100, 74, 100, 8, rssi);
    }
    _rxLog.draw(&gfx, 0, 85, 320, 128, COL_RTTY);
}

void RTTYScreen::_drawTXPanel() {
    auto& gfx = _disp->gfx();
    constexpr int Y = 74;

    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(4, Y);
    gfx.print("Message to transmit:");
    _txInput.active = true;
    _txInput.draw(&gfx, 4, Y + 12, 312, COL_RTTY);

    if (_txActive) {
        gfx.setTextColor(COL_RTTY, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, Y + 60);
        gfx.print("TRANSMITTING...");
    }
}

// ================================================================
void RTTYScreen::_startTX() {
    _radio->initRTTY(_freq, _shift, _baud, RADIOLIB_ASCII);
    _txActive = true;
}

void RTTYScreen::_stopTX() {
    _txActive = false;
    _radio->standby();
}

void RTTYScreen::_startRX() {
    FSKCfg cfg;
    cfg.freq    = _freq;
    cfg.bitRate = _baud / 1000.0f;
    cfg.freqDev = _shift / 2000.0f;
    cfg.rxBW    = 9.7f;
    cfg.power   = 0;
    _radio->fskBegin(cfg);
    _radio->fskStartRx();
    _rxMode = true;
}

void RTTYScreen::_stopRX() {
    _rxMode = false;
    _radio->standby();
}

void RTTYScreen::_pollRx() {
    if (!_radio->fskAvailable()) return;
    RxPacket pkt;
    if (_radio->fskRead(pkt) && pkt.valid && pkt.len > 0) {
        pkt.data[pkt.len] = '\0';
        // Basic RTTY decode: raw bytes to ASCII
        for (size_t i = 0; i < pkt.len; i++) {
            if (pkt.data[i] < 0x20 || pkt.data[i] > 0x7E)
                pkt.data[i] = '.';
        }
        _rxLog.addLine((char*)pkt.data);
        _dirty = true;
    }
}

// ================================================================
void RTTYScreen::onKey(char key) {
    if (key == 'r' || key == 'R') {
        if (_rxMode) { _stopRX(); }
        else { _txMode = false; _startRX(); }
        _dirty = true;
        return;
    }
    if (key == 't' || key == 'T') {
        if (_txMode) { _txMode = false; _stopTX(); }
        else { _rxMode = false; _stopRX(); _txMode = true; }
        _dirty = true;
        return;
    }
    if (key == 'b' || key == 'B') {
        _baudIdx = (_baudIdx + 1) % 4;
        _baud = BAUD_OPTIONS[_baudIdx];
        _dirty = true;
        return;
    }
    if (key == 's' || key == 'S') {
        _shiftIdx = (_shiftIdx + 1) % 3;
        _shift = SHIFT_OPTIONS[_shiftIdx];
        _dirty = true;
        return;
    }
    if (_txMode) {
        if (key == KEY_ENTER && strlen(_txInput.buf) > 0) {
            _startTX();
            RTTYClient* rtty = _radio->rtty();
            if (rtty) {
                rtty->println(_txInput.buf);
                rtty->idle();
            }
            char line[54];
            snprintf(line, sizeof(line), "[TX] %s", _txInput.buf);
            _rxLog.addLine(line);
            _txInput.clear();
            _stopTX();
            _dirty = true;
            return;
        }
        _txInput.input(key);
        _drawTXPanel();
    }
}

void RTTYScreen::onTrackball(int dx, int dy, bool click) {
    if (click) {
        if (_txMode) onKey(KEY_ENTER);
        else { _txMode = false; _rxMode = false;
               _startRX(); _dirty = true; }
    }
}
