#include "Radiosonde.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../ui/UIManager.h"
#include <Arduino.h>

Radiosonde::Radiosonde(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void Radiosonde::onEnter() {
    FSKCfg cfg;
    cfg.freq    = _curFreq;
    cfg.bitRate = RADIOSONDE_BITRATE;
    cfg.freqDev = RADIOSONDE_FREQDEV;
    cfg.rxBW    = RADIOSONDE_RXBW;
    cfg.power   = 0;   // RX only
    cfg.ook     = false;
    _radio->fskBegin(cfg);
    _radio->fskStartRx();
    _dirty = true;
}

void Radiosonde::onExit() {
    _radio->standby();
}

void Radiosonde::update() {
    // Try to receive a FSK packet
    if (_radio->fskAvailable()) {
        RxPacket pkt;
        if (_radio->fskRead(pkt) && pkt.valid) {
            _signalRSSI = pkt.rssi;
            _tryDecode(pkt);
        }
    }

    // Auto-scan
    if (_scanning && millis() - _lastScanMs > SCAN_DWELL_MS) {
        _doScan();
        _lastScanMs = millis();
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }

    static uint32_t lastRefresh = 0;
    if (millis() - lastRefresh > 1000) {
        lastRefresh = millis();
        _dirty = true;
    }
}

void Radiosonde::_doScan() {
    _scanIdx = (_scanIdx + 1) % 8;
    _curFreq = _scanFreqs[_scanIdx];
    _radio->setFrequency(_curFreq);
    _radio->fskStartRx();
    _dirty = true;
}

void Radiosonde::_tryDecode(const RxPacket& pkt) {
    SondeFrame frame;
    RadiosondeDecoder::Type t = RadiosondeDecoder::decode(
        pkt.data, pkt.len, frame);

    if (t != RadiosondeDecoder::UNKNOWN) {
        _lastFrame = frame;
        _hasFrame  = true;
        _frameCount++;

        // Lock to this frequency
        if (_scanning) {
            _scanning = false;
        }

        char line[54];
        snprintf(line, sizeof(line),
                 "[%s] %.1fkm %.0fhPa %.1fC",
                 frame.serial, frame.altKm,
                 frame.pressHpa, frame.tempC);
        _log.addLine(line);
        _dirty = true;
    }
}

// ================================================================
void Radiosonde::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Radiosonde RX", COL_SONDE);

    // Frequency + scan status
    char freqLine[40];
    snprintf(freqLine, sizeof(freqLine),
             "%.3f MHz  %s  RSSI:%.0f dBm",
             (double)_curFreq,
             _scanning ? "SCAN" : "LOCK",
             (double)_signalRSSI);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(_scanning ? COL_YELLOW : COL_GREEN, COL_BG);
    gfx.setCursor(4, 47);
    gfx.print(freqLine);

    // Signal bar
    drawRSSIBar(&gfx, 4, 57, 200, 8, _signalRSSI);

    gfx.drawFastHLine(0, 67, 320, COL_DIVIDER);

    _drawSondeData();

    // Log area
    _log.draw(&gfx, 0, 148, 320, 66, COL_SONDE);

    drawHints(&gfx, "ESC=Back", "S=Scan/Lock", "F=Freq");
}

void Radiosonde::_drawSondeData() {
    auto& gfx = _disp->gfx();
    constexpr int Y = 68;

    if (!_hasFrame) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, Y + 20);
        gfx.print("Searching...");
        gfx.setTextSize(FONT_TINY);
        gfx.setCursor(60, Y + 40);
        gfx.print("RS41 / DFM09 / M10");
        gfx.setCursor(30, Y + 55);
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.print("Note: 433MHz antenna has");
        gfx.setCursor(30, Y + 65);
        gfx.print("reduced gain <433MHz");
        return;
    }

    const SondeFrame& f = _lastFrame;

    // Type badge
    gfx.fillRoundRect(4, Y, 50, 14, 2, COL_SONDE);
    gfx.setTextColor(COL_BG, COL_SONDE);
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(8, Y + 3);
    gfx.print(f.typeStr);

    gfx.setTextColor(COL_TEXT, COL_BG);
    gfx.setCursor(60, Y + 3);
    gfx.print(f.serial);

    // Big altitude display
    char altStr[12];
    snprintf(altStr, sizeof(altStr), "%.1f km", f.altKm);
    gfx.setTextColor(COL_SONDE, COL_BG);
    gfx.setTextSize(FONT_LARGE);
    gfx.setCursor(4, Y + 18);
    gfx.print(altStr);

    // Other params
    gfx.setTextSize(FONT_TINY);
    char buf[20];

    snprintf(buf, sizeof(buf), "%.1f hPa", f.pressHpa);
    drawKV(&gfx, 4,   Y + 52, "Pressure: ", buf, COL_TEXT_DIM, COL_TEXT);

    snprintf(buf, sizeof(buf), "%.1f C", f.tempC);
    drawKV(&gfx, 4,   Y + 63, "Temp    : ", buf, COL_TEXT_DIM, COL_CYAN);

    snprintf(buf, sizeof(buf), "%.0f%%", f.humRH);
    drawKV(&gfx, 160, Y + 52, "RH: ", buf, COL_TEXT_DIM, COL_CYAN);

    if (f.hasGPS) {
        snprintf(buf, sizeof(buf), "%.4f", f.lat);
        drawKV(&gfx, 160, Y + 63, "Lat:", buf, COL_TEXT_DIM, COL_YELLOW);
    }

    snprintf(buf, sizeof(buf), "%d", _frameCount);
    drawKV(&gfx, 4, Y + 74, "Frames: ", buf, COL_TEXT_DIM, COL_GREEN);
}

// ================================================================
void Radiosonde::onKey(char key) {
    if (key == 's' || key == 'S') {
        _scanning = !_scanning;
        _dirty = true;
    }
    if (key == '+') {
        _manualFreq = _curFreq + 0.1f;
        if (_manualFreq > 406.5f) _manualFreq = 400.0f;
        _curFreq = _manualFreq;
        _radio->setFrequency(_curFreq);
        _dirty = true;
    }
    if (key == '-') {
        _manualFreq = _curFreq - 0.1f;
        if (_manualFreq < 400.0f) _manualFreq = 406.5f;
        _curFreq = _manualFreq;
        _radio->setFrequency(_curFreq);
        _dirty = true;
    }
}

void Radiosonde::onTrackball(int dx, int dy, bool click) {
    if (click) { _scanning = !_scanning; _dirty = true; return; }
    if (dx > 0) { onKey('+'); }
    if (dx < 0) { onKey('-'); }
}
