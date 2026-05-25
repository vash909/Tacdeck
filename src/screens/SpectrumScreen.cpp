#include "SpectrumScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"
#include <Arduino.h>
#include <cmath>

constexpr float SpectrumScreen::ZOOM_SPANS[];

SpectrumScreen::SpectrumScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void SpectrumScreen::onEnter() {
    for (int i = 0; i < NUM_BINS; i++) {
        _rssi[i]     = -120.f;
        _peakRssi[i] = -120.f;
    }
    _curBin = 0;

    // Init radio in FSK mode for RSSI measurement
    FSKCfg cfg;
    cfg.freq    = _startFreq;
    cfg.bitRate = 4.8f;
    cfg.freqDev = 9.6f;
    cfg.rxBW    = 156.2f;
    cfg.power   = 0;
    _radio->fskBegin(cfg);

    _dirty = true;
}

void SpectrumScreen::onExit() {
    _radio->standby();
}

void SpectrumScreen::update() {
    if (_sweeping) {
        // Step through a few bins per frame to maintain responsiveness
        for (int s = 0; s < 4 && _sweeping; s++) {
            _sweepStep();
        }
        _drawPlot();
        _drawInfo();
    } else if (_dirty) {
        auto& gfx = _disp->gfx();
        gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
        drawHeader(&gfx, "Spectrum Analyzer", COL_SPECTRUM);
        _drawAxes();
        _drawPlot();
        _drawInfo();
        drawHints(&gfx, "ESC=Back", "P=PeakHold", "Z=Zoom");
        _dirty = false;
    }
}

void SpectrumScreen::_sweepStep() {
    float freq = _binToFreq(_curBin);
    float rssi = _radio->getChannelRSSI(freq);

    // Exponential moving average for smoothing
    _rssi[_curBin] = _rssi[_curBin] * 0.6f + rssi * 0.4f;

    if (_peakHold && rssi > _peakRssi[_curBin])
        _peakRssi[_curBin] = rssi;

    _curBin++;
    if (_curBin >= NUM_BINS) _curBin = 0;
}

float SpectrumScreen::_binToFreq(int bin) const {
    float span = ZOOM_SPANS[_zoomLevel];
    float center = (_startFreq + _endFreq) / 2.0f;
    return center - span / 2.0f + (float)bin * span / NUM_BINS;
}

int SpectrumScreen::_freqToBin(float freq) const {
    float span   = ZOOM_SPANS[_zoomLevel];
    float center = (_startFreq + _endFreq) / 2.0f;
    float start  = center - span / 2.0f;
    return (int)((freq - start) / span * NUM_BINS);
}

void SpectrumScreen::_drawAxes() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = PLOT_Y + PLOT_H;

    // Freq axis
    gfx.drawFastHLine(0, Y0, PLOT_W, COL_BORDER);

    // Freq labels: start, center, end
    float span   = ZOOM_SPANS[_zoomLevel];
    float center = (_startFreq + _endFreq) / 2.0f;
    char buf[12];

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);

    snprintf(buf, sizeof(buf), "%.1f", center - span/2);
    gfx.setCursor(0, Y0 + 2); gfx.print(buf);

    snprintf(buf, sizeof(buf), "%.1f", center);
    gfx.setCursor(144, Y0 + 2); gfx.print(buf);

    snprintf(buf, sizeof(buf), "%.1fMHz", center + span/2);
    gfx.setCursor(264, Y0 + 2); gfx.print(buf);

    // dBm gridlines
    for (int db = -120; db <= -40; db += 20) {
        int y = PLOT_Y + PLOT_H - (int)((db + 120.f) / 80.f * PLOT_H);
        if (y < PLOT_Y || y > PLOT_Y + PLOT_H) continue;
        gfx.drawFastHLine(0, y, PLOT_W, COL_BG_PANEL);
        snprintf(buf, sizeof(buf), "%d", db);
        gfx.setCursor(0, y - 8); gfx.print(buf);
    }
}

void SpectrumScreen::_drawPlot() {
    auto& gfx = _disp->gfx();

    // Draw background + axes first on full redraw
    if (_dirty) {
        gfx.fillRect(0, PLOT_Y, PLOT_W, PLOT_H, COL_BG);
        _drawAxes();
    }

    // Draw spectrum bars
    for (int x = 0; x < PLOT_W; x++) {
        float rssi = _rssi[x];
        // Clamp: -120 dBm = 0, -40 dBm = PLOT_H
        int h = (int)((rssi + 120.f) / 80.f * PLOT_H);
        if (h < 0)      h = 0;
        if (h > PLOT_H) h = PLOT_H;

        // Erase old column
        gfx.drawFastVLine(x, PLOT_Y, PLOT_H - h, COL_BG);

        // Draw bar with gradient color
        uint16_t col = rssiToColor(rssi);
        gfx.drawFastVLine(x, PLOT_Y + PLOT_H - h, h, col);

        // Peak hold
        if (_peakHold) {
            int ph = (int)((_peakRssi[x] + 120.f) / 80.f * PLOT_H);
            if (ph > 0 && ph <= PLOT_H) {
                gfx.drawPixel(x, PLOT_Y + PLOT_H - ph, COL_TEXT);
            }
        }
    }

    // Marker
    if (_markerBin >= 0 && _markerBin < PLOT_W) {
        gfx.drawFastVLine(_markerBin, PLOT_Y, PLOT_H, COL_YELLOW);
    }
}

void SpectrumScreen::_drawInfo() {
    auto& gfx = _disp->gfx();
    constexpr int Y = PLOT_Y + PLOT_H + 12;

    gfx.fillRect(0, Y, 320, 16, COL_BG);
    gfx.setTextSize(FONT_TINY);

    float span   = ZOOM_SPANS[_zoomLevel];
    char buf[48];
    snprintf(buf, sizeof(buf), "Span:%.0fMHz  Step:%.0fkHz  Peak:%s",
             span, (float)span * 1000.f / NUM_BINS,
             _peakHold ? "ON" : "off");
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, Y);
    gfx.print(buf);

    if (_markerBin >= 0) {
        float mf = _binToFreq(_markerBin);
        snprintf(buf, sizeof(buf), "Mrk:%.3fMHz  %.0fdBm",
                 mf, (double)_rssi[_markerBin]);
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(4, Y + 10);
        gfx.print(buf);
    }
}

// ================================================================
void SpectrumScreen::onKey(char key) {
    if (key == 'p' || key == 'P') {
        _peakHold = !_peakHold;
        if (!_peakHold) {
            for (int i = 0; i < NUM_BINS; i++) _peakRssi[i] = -120.f;
        }
        return;
    }
    if (key == 'z' || key == 'Z') {
        _zoomLevel = (_zoomLevel + 1) % 4;
        for (int i = 0; i < NUM_BINS; i++) {
            _rssi[i]     = -120.f;
            _peakRssi[i] = -120.f;
        }
        _dirty = true;
        return;
    }
}

void SpectrumScreen::onTrackball(int dx, int dy, bool click) {
    if (dx != 0) {
        _markerBin += dx * 2;
        if (_markerBin < 0)         _markerBin = 0;
        if (_markerBin >= NUM_BINS) _markerBin = NUM_BINS - 1;
    }
    if (click) {
        _markerBin = -1;
    }
}
