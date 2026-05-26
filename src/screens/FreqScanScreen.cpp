#include "FreqScanScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>
#include <cstdlib>

FreqScanScreen::FreqScanScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

static inline int _freqScanMaxOffset(int numChannels) {
    return (numChannels > 14) ? (numChannels - 14) : 0;
}

void FreqScanScreen::onEnter() {
    char buf[16];

    Storage::getString(NVS_KEY_SCAN_START, buf, sizeof(buf), "430.000");
    float f = strtof(buf, nullptr);
    _startFreq = (f >= 100.f && f < 960.f) ? f : SCANNER_START_FREQ;

    Storage::getString(NVS_KEY_SCAN_END, buf, sizeof(buf), "440.000");
    f = strtof(buf, nullptr);
    _endFreq = (f > _startFreq && f <= 960.f) ? f : SCANNER_END_FREQ;

    Storage::getString(NVS_KEY_SCAN_STEP, buf, sizeof(buf), "12.5");
    f = strtof(buf, nullptr);
    _stepKHz = (f > 0.f && f <= 1000.f) ? f : SCANNER_STEP_KHZ;

    Storage::getString(NVS_KEY_SCAN_SQUELCH, buf, sizeof(buf), "-90");
    f = strtof(buf, nullptr);
    _squelch = (f < 0.f) ? f : SCANNER_SQUELCH_DBM;

    _buildChannelList();

    FSKCfg cfg;
    cfg.freq    = _startFreq;
    cfg.bitRate = 4.8f;
    cfg.freqDev = 9.6f;
    cfg.rxBW    = 156.2f;
    _radio->fskBegin(cfg);

    _dirty = true;
}

void FreqScanScreen::onExit() {
    _radio->standby();
}

void FreqScanScreen::_buildChannelList() {
    _numChannels = 0;
    float stepMHz = _stepKHz / 1000.0f;
    if (stepMHz <= 0.f) stepMHz = 0.0125f;

    for (float f = _startFreq; f <= _endFreq && _numChannels < MAX_CHANNELS;
         f += stepMHz) {
        _channels[_numChannels].freqMHz     = f;
        _channels[_numChannels].rssi        = -120.f;
        _channels[_numChannels].lastActiveMs= 0;
        _channels[_numChannels].hitCount    = 0;
        _numChannels++;
    }
    _curIdx = 0;
    _viewOffset = 0;
}

void FreqScanScreen::update() {
    if (_scanning) {
        // _scanStep() already sets _dirty when a new lock/unlock event occurs.
        // We also redraw the list whenever the current-channel index advances
        // so the highlight moves without waiting for a dirty-flag redraw.
        int prevIdx = _curIdx;
        _scanStep();
        if (!_dirty && _curIdx != prevIdx) {
            // Only update the channel list rows — avoid the full-screen clear
            // from _drawAll() and avoid the 200 ms periodic fillRect+redraw
            // that was causing visible flicker every 5 frames.
            _drawChannelList();
        }
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }
}

void FreqScanScreen::_scanStep() {
    if (_numChannels <= 0) return;
    if (millis() - _lastStepMs < DWELL_MS) return;
    _lastStepMs = millis();

    // If locked, check if we should unlock
    if (_lockedIdx >= 0) {
        uint32_t age = millis() - _channels[_lockedIdx].lastActiveMs;
        float rssi   = _radio->getChannelRSSI(_channels[_lockedIdx].freqMHz);
        _channels[_lockedIdx].rssi = rssi;
        if (rssi > _squelch) {
            _channels[_lockedIdx].lastActiveMs = millis();
        } else if (age > LOCK_MS) {
            _lockedIdx = -1;   // resume scanning
        }
        return;
    }

    // Scan next channel
    _curIdx = (_curIdx + 1) % _numChannels;
    float freq = _channels[_curIdx].freqMHz;
    float rssi = _radio->getChannelRSSI(freq);
    _channels[_curIdx].rssi = rssi;

    if (rssi > _squelch) {
        _channels[_curIdx].lastActiveMs = millis();
        _channels[_curIdx].hitCount++;
        _lockedIdx = _curIdx;
        _dirty = true;
    }

    _curRSSI = rssi;
}

// ================================================================
void FreqScanScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Frequency Scanner", COL_SCANNER);

    // Config line
    char buf[48];
    snprintf(buf, sizeof(buf),
             "%.3f-%.3fMHz  step:%.1fkHz  sq:%.0fdBm",
             (double)_startFreq, (double)_endFreq,
             (double)_stepKHz, (double)_squelch);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(2, 47);
    gfx.print(buf);

    // Status
    if (_lockedIdx >= 0) {
        snprintf(buf, sizeof(buf), "LOCKED  %.3f MHz  %.0f dBm",
                 (double)_channels[_lockedIdx].freqMHz,
                 (double)_channels[_lockedIdx].rssi);
        gfx.setTextColor(COL_GREEN, COL_BG);
    } else if (_scanning) {
        snprintf(buf, sizeof(buf), "SCANNING  %.3f MHz  %.0f dBm",
                 (double)_channels[_curIdx].freqMHz,
                 (double)_curRSSI);
        gfx.setTextColor(COL_YELLOW, COL_BG);
    } else {
        snprintf(buf, sizeof(buf), "PAUSED");
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    }
    gfx.setCursor(2, 57);
    gfx.print(buf);
    gfx.drawFastHLine(0, 67, 320, COL_DIVIDER);

    _drawChannelList();
    drawHints(&gfx, "HOLD=Back",
              _scanning ? "S=Pause" : "S=Scan",
              "+/-=Squelch");
}

void FreqScanScreen::_drawChannelList() {
    auto& gfx = _disp->gfx();
    constexpr int Y0   = 68;
    constexpr int LINE = 10;
    constexpr int ROWS = 14;

    // Do NOT use a single fillRect(0, Y0, 320, ROWS*LINE, COL_BG) here —
    // that clears 140 px in one shot and causes a visible flash every scan
    // step.  Instead, fill each row's background individually so the display
    // always shows *something* valid (no momentary black strip).
    gfx.setTextSize(FONT_TINY);

    for (int i = 0; i < ROWS && (i + _viewOffset) < _numChannels; i++) {
        int idx = i + _viewOffset;
        const ScannedCh& ch = _channels[idx];
        int y = Y0 + i * LINE;

        bool isLocked  = (idx == _lockedIdx);
        bool isCurrent = (idx == _curIdx);
        bool isActive  = ch.rssi > _squelch;

        // Row background — fill every row (not just locked) so stale content
        // from a previous view offset is always overwritten.
        uint16_t rowBg = isLocked ? COL_BG_PANEL : COL_BG;
        gfx.fillRect(0, y, 320, LINE, rowBg);

        // Indicator
        uint16_t indCol = isLocked  ? COL_GREEN  :
                          isActive  ? COL_YELLOW :
                          isCurrent ? COL_CYAN   : COL_BORDER;
        gfx.fillRect(0, y + 2, 3, LINE - 4, indCol);

        // Frequency
        char freqBuf[12];
        snprintf(freqBuf, sizeof(freqBuf), "%.3f", (double)ch.freqMHz);
        gfx.setTextColor(isLocked ? COL_GREEN : COL_TEXT, COL_BG);
        gfx.setCursor(6, y + 1);
        gfx.print(freqBuf);

        // RSSI bar
        drawRSSIBar(&gfx, 72, y + 2, 160, LINE - 4, ch.rssi);

        // dBm value
        char rssiBuf[10];
        snprintf(rssiBuf, sizeof(rssiBuf), "%.0f", (double)ch.rssi);
        gfx.setTextColor(rssiToColor(ch.rssi), COL_BG);
        gfx.setCursor(236, y + 1);
        gfx.print(rssiBuf);

        // Hit count
        if (ch.hitCount > 0) {
            char hitBuf[6];
            snprintf(hitBuf, sizeof(hitBuf), "x%d", ch.hitCount);
            gfx.setTextColor(COL_TEXT_DIM, COL_BG);
            gfx.setCursor(272, y + 1);
            gfx.print(hitBuf);
        }
    }
}

// ================================================================
void FreqScanScreen::onKey(char key) {
    if (key == 's' || key == 'S') {
        _scanning = !_scanning;
        _dirty = true;
        return;
    }
    if (key == '+') { _squelch += 5; _dirty = true; }
    if (key == '-') { _squelch -= 5; _dirty = true; }
    if (key == KEY_UP   && _viewOffset > 0) _viewOffset--;
    if (key == KEY_DOWN && _viewOffset < _freqScanMaxOffset(_numChannels)) _viewOffset++;
    if (key == 'r' || key == 'R') {
        _buildChannelList();
        _dirty = true;
    }
}

void FreqScanScreen::onTrackball(int dx, int dy, bool click) {
    (void)dx;
    if (dy != 0) {
        _viewOffset += dy;
        if (_viewOffset < 0) _viewOffset = 0;
        int maxOff = _freqScanMaxOffset(_numChannels);
        if (_viewOffset > maxOff) _viewOffset = maxOff;
    }
    if (click) { _scanning = !_scanning; _dirty = true; }
}
