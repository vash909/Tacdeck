#include "POCSAGScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../ui/UIManager.h"
#include <Arduino.h>

constexpr float POCSAGScreen::BAUDS[];

POCSAGScreen::POCSAGScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void POCSAGScreen::onEnter() {
    FSKCfg cfg;
    cfg.freq    = _freq;
    cfg.bitRate = BAUDS[_baudIdx];
    cfg.freqDev = POCSAG_FREQDEV;
    cfg.rxBW    = 19.5f;
    cfg.ook     = false;
    _radio->fskBegin(cfg);
    _radio->fskStartRx();
    _dirty = true;
}

void POCSAGScreen::onExit() {
    _radio->standby();
}

void POCSAGScreen::update() {
    _pollRx();
    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    // Periodic partial refresh (RSSI and content timestamps) without full-screen blink.
    static uint32_t lastLiveRefresh = 0;
    if (millis() - lastLiveRefresh > 1000) {
        lastLiveRefresh = millis();
        _drawStatusLine();
        if (_showRaw) _rawLog.draw(&_disp->gfx(), 0, 83, 320, 130, COL_POCSAG);
        else          _drawMessages();
    }
}

void POCSAGScreen::_pollRx() {
    if (!_radio->fskAvailable()) return;
    RxPacket pkt;
    if (!_radio->fskRead(pkt) || !pkt.valid) return;

    _listenRSSI = pkt.rssi;

    char rawLine[54];
    char hex[36] = "";
    for (size_t i = 0; i < pkt.len && i < 12; i++) {
        char h[4]; snprintf(h, sizeof(h), "%02X ", pkt.data[i]);
        strncat(hex, h, sizeof(hex) - strlen(hex) - 1);
    }
    snprintf(rawLine, sizeof(rawLine), "%.0fdBm: %s", (double)pkt.rssi, hex);
    _rawLog.addLine(rawLine);

    _decodePOCSAG(pkt.data, pkt.len);
    if (!_dirty) {
        _drawStatusLine();
        if (_showRaw) _rawLog.draw(&_disp->gfx(), 0, 83, 320, 130, COL_POCSAG);
        else          _drawMessages();
    }
}

// ---- Minimal POCSAG frame decoder ----
bool POCSAGScreen::_decodePOCSAG(const uint8_t* data, size_t len) {
    if (len < 4) return false;

    // Look for POCSAG sync word: 0x7CD215D8
    for (size_t i = 0; i + 4 <= len; i++) {
        uint32_t word = ((uint32_t)data[i]   << 24) |
                        ((uint32_t)data[i+1] << 16) |
                        ((uint32_t)data[i+2] <<  8) |
                         (uint32_t)data[i+3];

        if (word == 0x7CD215D8) {
            // Found sync — try to parse address word
            if (i + 8 <= len) {
                uint32_t addr_word = ((uint32_t)data[i+4] << 24) |
                                     ((uint32_t)data[i+5] << 16) |
                                     ((uint32_t)data[i+6] <<  8) |
                                      (uint32_t)data[i+7];

                // Address codeword: bit 31=0 (address), bits 30-11 = address/8, bits 10-9 = func
                if ((addr_word & 0x80000000) == 0) {
                    uint32_t capcode = ((addr_word >> 13) & 0x3FFFF) * 8 +
                                       ((addr_word >> 11) & 0x3);
                    // Try to extract message from following message words
                    char text[40] = "";
                    int tIdx = 0;
                    for (size_t j = i + 8; j + 4 <= len && tIdx < 39; j += 4) {
                        uint32_t mw = ((uint32_t)data[j]   << 24) |
                                      ((uint32_t)data[j+1] << 16) |
                                      ((uint32_t)data[j+2] <<  8) |
                                       (uint32_t)data[j+3];
                        if (mw & 0x80000000) {  // message word
                            // Extract 7-bit ASCII from bits 30-11 (20 bits = ~2.8 chars)
                            for (int b = 30; b >= 11 && tIdx < 39; b -= 7) {
                                char c = (char)((mw >> (b - 6)) & 0x7F);
                                if (c == 0x03) goto done;   // ETX
                                if (c >= 0x20 && c <= 0x7E) text[tIdx++] = c;
                            }
                        }
                    }
                    done:
                    text[tIdx] = '\0';
                    _addMsg(capcode, text);
                    return true;
                }
            }
        }
    }
    return false;
}

void POCSAGScreen::_addMsg(uint32_t capcode, const char* text) {
    if (_msgCount < MAX_MSGS) {
        POCSAGMsg& m = _msgs[_msgCount++];
        m.capcode = capcode;
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.text[sizeof(m.text) - 1] = '\0';
        m.baud = (uint16_t)(BAUDS[_baudIdx] * 1000);
        m.ts   = millis();
    } else {
        memmove(_msgs, _msgs + 1, sizeof(POCSAGMsg) * (MAX_MSGS - 1));
        POCSAGMsg& m = _msgs[MAX_MSGS - 1];
        m.capcode = capcode;
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.text[sizeof(m.text) - 1] = '\0';
        m.baud = (uint16_t)(BAUDS[_baudIdx] * 1000);
        m.ts   = millis();
    }
}

// ================================================================
void POCSAGScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "POCSAG Receiver", COL_POCSAG);

    _drawStatusLine();

    drawButton(&gfx,   0, 68, 90, 12, "Messages", !_showRaw, COL_POCSAG);
    drawButton(&gfx,  92, 68, 90, 12, "Raw",       _showRaw, COL_POCSAG);
    drawButton(&gfx, 270, 68, 48, 12, "Baud+", false, COL_POCSAG);
    gfx.drawFastHLine(0, 82, 320, COL_DIVIDER);

    if (_showRaw) {
        _rawLog.draw(&gfx, 0, 83, 320, 130, COL_POCSAG);
    } else {
        _drawMessages();
    }

    drawHints(&gfx, "HOLD=Back", "B=Baud", "R=Raw");
}

void POCSAGScreen::_drawStatusLine() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, 47, 320, 20, COL_BG);

    char cfgLine[40];
    snprintf(cfgLine, sizeof(cfgLine),
             "%.3fMHz  %.0fbps  RSSI:%.0fdBm",
             (double)_freq, BAUDS[_baudIdx] * 1000, (double)_listenRSSI);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, 47);
    gfx.print(cfgLine);
    drawRSSIBar(&gfx, 4, 57, 200, 8, _listenRSSI);
    gfx.drawFastHLine(0, 67, 320, COL_DIVIDER);
}

void POCSAGScreen::_drawMessages() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 83;
    constexpr int H  = 16;

    if (_msgCount == 0) {
        // No messages yet — full clear is fine here (nothing useful on screen).
        gfx.fillRect(0, Y0, 320, 130, COL_BG);
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(50, Y0 + 30);
        gfx.print("Listening for pages...");
        return;
    }

    // First message arrival: clear once to erase old "Listening..." text.
    // After that, per-row fills (below) keep content up to date without a
    // full-area clear at 1 Hz.
    static int lastMsgCount = 0;
    if (lastMsgCount == 0) {
        gfx.fillRect(0, Y0, 320, 130, COL_BG);
    }
    lastMsgCount = _msgCount;

    gfx.setTextSize(FONT_TINY);
    for (int i = 0; i < _msgCount && i < 8; i++) {
        const POCSAGMsg& m = _msgs[i];
        int y = Y0 + i * H;

        gfx.fillRect(0, y, 320, H, (i % 2) ? COL_BG_PANEL : COL_BG);
        gfx.setTextColor(COL_POCSAG, (i % 2) ? COL_BG_PANEL : COL_BG);
        char capBuf[12];
        snprintf(capBuf, sizeof(capBuf), "%07lu", (unsigned long)m.capcode);
        gfx.setCursor(4, y + 2);
        gfx.print(capBuf);

        gfx.setTextColor(COL_TEXT, (i % 2) ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(60, y + 2);
        if (strlen(m.text) > 0) {
            gfx.print(m.text);
        } else {
            gfx.setTextColor(COL_TEXT_DIM, (i % 2) ? COL_BG_PANEL : COL_BG);
            gfx.print("[tone/numeric]");
        }
    }
}

// ================================================================
void POCSAGScreen::onKey(char key) {
    if (key == 'b' || key == 'B') {
        _baudIdx = (_baudIdx + 1) % 3;
        onExit(); onEnter();
        return;
    }
    if (key == 'r' || key == 'R') {
        _showRaw = !_showRaw; _dirty = true; return;
    }
}

void POCSAGScreen::onTrackball(int dx, int dy, bool click) {
    if (click) { _showRaw = !_showRaw; _dirty = true; }
}
