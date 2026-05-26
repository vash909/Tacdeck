#include "LoraAPRS.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"
#include "../utils/Storage.h"
#include <Arduino.h>

LoraAPRS::LoraAPRS(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void LoraAPRS::onEnter() {
    // Load callsign from NVS
    Storage::getString(NVS_KEY_CALLSIGN, _callsign, sizeof(_callsign), "N0CALL-9");
    Storage::getString(NVS_KEY_APRS_COMMENT, _comment, sizeof(_comment), "TDeck-RFMaster");

    // Configure LoRa for APRS
    LoRaCfg cfg;
    cfg.freq     = APRS_LORA_FREQ;
    cfg.bw       = APRS_LORA_BW;
    cfg.sf       = APRS_LORA_SF;
    cfg.cr       = APRS_LORA_CR;
    cfg.syncWord = APRS_LORA_SYNC;
    cfg.power    = APRS_LORA_POWER;
    _radio->loraBegin(cfg);
    _radio->loraStartRx();

    _lastTxMs = millis() - (_txIntervalSec * 1000UL); // TX soon
    _dirty = true;
}

void LoraAPRS::onExit() {
    _radio->standby();
}

void LoraAPRS::update() {
    _pollRx();

    // Auto-beacon
    uint32_t elapsed = millis() - _lastTxMs;
    if (elapsed >= _txIntervalSec * 1000UL) {
        _txBeacon();
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    // Update countdown/live info every second without full redraw.
    static uint32_t lastSec = 0;
    if (millis() - lastSec > 1000) {
        lastSec = millis();
        _drawStatusRow();
        if (_tabSel == 1) _drawTXPanel();
    }
}

// ================================================================
void LoraAPRS::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "LoRa APRS", COL_APRS);

    // Tabs
    drawButton(&gfx,   0, 44, 100, 14, "RX Log",  _tabSel==0, COL_APRS);
    drawButton(&gfx, 102, 44, 100, 14, "TX/Config",_tabSel==1, COL_APRS);

    _drawStatusRow();

    if (_tabSel == 0) _drawRXLog();
    else              _drawTXPanel();

    drawHints(&gfx, "HOLD=Back", "T=TX Now", "TAB=Switch");
}

void LoraAPRS::_drawStatusRow() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, 61, 320, 10, COL_BG);

    char statLine[48];
    uint32_t elapsed = millis() - _lastTxMs;
    uint32_t nextTx = (elapsed >= _txIntervalSec * 1000UL) ? 0 :
                      (_txIntervalSec * 1000UL - elapsed) / 1000;
    bool hasFix = _gps && _gps->hasFix();
    snprintf(statLine, sizeof(statLine), "%s  %.3fMHz  TX in %lus",
             _callsign, (double)APRS_LORA_FREQ, (unsigned long)nextTx);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(hasFix ? COL_GREEN : COL_YELLOW, COL_BG);
    gfx.setCursor(2, 61);
    gfx.print(statLine);
    gfx.drawFastHLine(0, 71, 320, COL_DIVIDER);
}

void LoraAPRS::_drawRXLog() {
    _log.draw(&_disp->gfx(), 0, 72, 320, 140, COL_GREEN);
}

void LoraAPRS::_drawTXPanel() {
    auto& gfx = _disp->gfx();
    int y = 72;
    // Fixed rows (callsign/comment/interval/rxcount) use drawKV with COL_BG
    // background and overwrite themselves.  Only the two GPS content rows at
    // y+36 and y+48 alternate between different layouts (position+grid vs
    // "Waiting..."), so clear just those 24 px instead of the full 140 px.
    gfx.fillRect(0, y + 36, 320, 24, COL_BG);

    drawKV(&gfx, 4, y,     "Callsign : ", _callsign);
    drawKV(&gfx, 4, y+12,  "Comment  : ", _comment);

    char buf[32];
    snprintf(buf, sizeof(buf), "%u sec", _txIntervalSec);
    drawKV(&gfx, 4, y+24, "Interval : ", buf);

    bool hasFix = _gps && _gps->hasFix();
    if (hasFix) {
        char posBuf[32];
        GPS::encodeAPRSPos(_gps->lat(), _gps->lon(), _symbol, posBuf, sizeof(posBuf));
        drawKV(&gfx, 4, y+36, "Position : ", posBuf, COL_TEXT_DIM, COL_GREEN);

        char gridBuf[8];
        GPS::latLonToGrid(_gps->lat(), _gps->lon(), gridBuf, sizeof(gridBuf));
        drawKV(&gfx, 4, y+48, "Grid     : ", gridBuf, COL_TEXT_DIM, COL_CYAN);
    } else {
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(4, y+36);
        gfx.setTextSize(FONT_TINY);
        gfx.print("Waiting for GPS fix...");
    }

    snprintf(buf, sizeof(buf), "RX count : %d", _rxCount);
    drawKV(&gfx, 4, y+72, "", buf, COL_TEXT_DIM, COL_CYAN);
}

// ================================================================
void LoraAPRS::_txBeacon() {
    if (!_gps || !_gps->hasFix()) {
        _log.addLine("[TX] No GPS — skipped");
        _lastTxMs = millis();
        _dirty = true;
        return;
    }

    // Build APRS packet: header!position comment
    char posBuf[24];
    GPS::encodeAPRSPos(_gps->lat(), _gps->lon(), _symbol, posBuf, sizeof(posBuf));

    char packet[128];
    snprintf(packet, sizeof(packet), "%s>APRS,RFONLY:!%s %s",
             _callsign, posBuf, _comment);

    bool ok = _radio->loraTx((const uint8_t*)packet, strlen(packet));

    char logLine[60];
    snprintf(logLine, sizeof(logLine), ok ? "[TX] %s" : "[TX ERR] %s", _callsign);
    _log.addLine(logLine);

    _lastTxMs = millis();
    _radio->loraStartRx();
    _dirty = true;
}

void LoraAPRS::_pollRx() {
    if (!_radio->loraAvailable()) return;

    RxPacket pkt;
    if (_radio->loraRead(pkt) && pkt.valid) {
        _rxCount++;
        _decodeAndLog(pkt);
        if (!_dirty) {
            _drawStatusRow();
            if (_tabSel == 0) _drawRXLog();
            else              _drawTXPanel();
        }
    }
}

void LoraAPRS::_decodeAndLog(const RxPacket& pkt) {
    // Copy to a local null-terminated buffer (pkt.data is const here)
    char payload[257];
    size_t payLen = pkt.len < 256 ? pkt.len : 256;
    memcpy(payload, pkt.data, payLen);
    payload[payLen] = '\0';
    char line[54];

    // Try to extract callsign (first token before '>')
    char* arrowPos = (char*)memchr(payload, '>', payLen);
    if (arrowPos) {
        size_t callLen = arrowPos - payload;
        if (callLen > 10) callLen = 10;
        char call[11];
        memcpy(call, payload, callLen);
        call[callLen] = '\0';
        snprintf(line, sizeof(line), "[RX] %s  %.0fdBm SNR%.0f",
                 call, (double)pkt.rssi, (double)pkt.snr);
    } else {
        snprintf(line, sizeof(line), "[RX] %.0fdBm (raw)", (double)pkt.rssi);
    }
    _log.addLine(line);
}

// ================================================================
void LoraAPRS::onKey(char key) {
    if (key == KEY_TAB || key == '\t') {
        _tabSel = (_tabSel + 1) % 2;
        _dirty = true;
        return;
    }
    if (key == 't' || key == 'T') {
        _txBeacon();
        return;
    }
}

void LoraAPRS::onTrackball(int dx, int dy, bool click) {
    if (click) { _txBeacon(); return; }
    if (dx != 0) { _tabSel = (_tabSel + 1) % 2; _dirty = true; }
}
