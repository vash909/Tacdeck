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
    _prevMarkerBin = _markerBin;

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
    if (_dirty) {
        _drawStaticLayout();
        _drawPlot();
        _drawInfo(true);
        _dirty = false;
    }

    if (_sweeping) {
        // Step through a few bins per frame to maintain responsiveness.
        for (int s = 0; s < SWEEP_STEPS_PER_FRAME; s++) {
            int changedBin = _curBin;
            _sweepStep();
            _drawBin(changedBin);
        }
    }

    // Marker moved: redraw previous and current marker columns only.
    if (_prevMarkerBin != _markerBin) {
        if (_prevMarkerBin >= 0 && _prevMarkerBin < NUM_BINS) _drawBin(_prevMarkerBin);
        if (_markerBin >= 0 && _markerBin < NUM_BINS)         _drawBin(_markerBin);
        _prevMarkerBin = _markerBin;
    }

    _drawInfo();
}

void SpectrumScreen::_sweepStep() {
    float freq = _binToFreq(_curBin);
    float rssi = _radio->getChannelRSSI(freq);

    // Direct RSSI — no EMA.  EMA caused each new sweep to start with
    // different (higher) values than the trailing end of the previous sweep,
    // producing a visible "step" discontinuity (different color/height) at
    // the wrap point.  Without EMA the display updates immediately.
    _rssi[_curBin] = rssi;

    if (_peakHold && rssi > _peakRssi[_curBin])
        _peakRssi[_curBin] = rssi;

    _curBin++;
    if (_curBin >= NUM_BINS) {
        _curBin = 0;
        // On wrap: reset all bins so old bars don't persist into the new sweep.
        // Without the reset, bins not yet redrawn in the new pass still show
        // stale (higher-converged) values from the previous pass.
        if (!_peakHold) {
            for (int i = 0; i < NUM_BINS; i++) _rssi[i] = -120.f;
            _drawPlot();   // clear all bars immediately — fast (all h=0)
        }
    }
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

    // Plot border and axes.
    gfx.drawFastVLine(0, PLOT_Y, PLOT_H + 1, COL_BORDER);
    gfx.drawFastHLine(0, AXIS_Y, PLOT_W, COL_BORDER);

    // Freq labels: start, center, end
    float span   = ZOOM_SPANS[_zoomLevel];
    float center = (_startFreq + _endFreq) / 2.0f;
    char buf[16];

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);

    snprintf(buf, sizeof(buf), "%.1f", center - span/2);
    gfx.setCursor(0, AXIS_Y + 2); gfx.print(buf);

    snprintf(buf, sizeof(buf), "%.1f", center);
    gfx.setCursor(146, AXIS_Y + 2); gfx.print(buf);

    snprintf(buf, sizeof(buf), "%.1fMHz", center + span/2);
    gfx.setCursor(262, AXIS_Y + 2); gfx.print(buf);

    // dBm gridlines
    for (int db = -120; db <= -40; db += 20) {
        int y = AXIS_Y - _rssiToHeight((float)db);
        if (y < PLOT_Y || y > AXIS_Y) continue;
        if (y == AXIS_Y) continue; // keep x-axis clean
        gfx.drawFastHLine(0, y, PLOT_W, COL_BG_PANEL);
        snprintf(buf, sizeof(buf), "%d", db);
        int ly = y - 4;
        if (ly < TOP_INFO_Y + TOP_INFO_H + 1) ly = TOP_INFO_Y + TOP_INFO_H + 1;
        gfx.setCursor(4, ly); gfx.print(buf);
    }
}

void SpectrumScreen::_drawStaticLayout() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Spectrum Analyzer", COL_SPECTRUM);
    drawHints(&gfx, "HOLD=Back", "P=PeakHold", "Z=Zoom");
    _drawAxes();
}

void SpectrumScreen::_drawPlot() {
    // Full plot redraw (used only on enter/zoom/full invalidate).
    for (int x = 0; x < PLOT_W; x++) {
        _drawBin(x);
    }
}

void SpectrumScreen::_restoreColumnBackground(int x) {
    auto& gfx = _disp->gfx();
    gfx.drawFastVLine(x, PLOT_Y, PLOT_H + 1, COL_BG);

    // Restore static background elements for this single column.
    for (int db = -120; db <= -40; db += 20) {
        int y = AXIS_Y - _rssiToHeight((float)db);
        if (y < PLOT_Y || y > AXIS_Y) continue;
        gfx.drawPixel(x, y, (y == AXIS_Y) ? COL_BORDER : COL_BG_PANEL);
    }
    if (x == 0) {
        gfx.drawFastVLine(0, PLOT_Y, PLOT_H + 1, COL_BORDER);
    }
}

int SpectrumScreen::_rssiToHeight(float rssi) const {
    int h = (int)((rssi + 120.f) / 80.f * PLOT_H);
    if (h < 0)      h = 0;
    if (h > PLOT_H) h = PLOT_H;
    return h;
}

void SpectrumScreen::_drawBin(int bin) {
    if (bin < 0 || bin >= NUM_BINS) return;
    auto& gfx = _disp->gfx();

    _restoreColumnBackground(bin);

    float rssi = _rssi[bin];
    int h = _rssiToHeight(rssi);

    if (h > 0) {
        uint16_t col = rssiToColor(rssi);
        gfx.drawFastVLine(bin, AXIS_Y - h, h, col);
    }

    if (_peakHold) {
        int ph = _rssiToHeight(_peakRssi[bin]);
        if (ph > 0) {
            gfx.drawPixel(bin, AXIS_Y - ph, COL_TEXT);
        }
    }

    if (_markerBin == bin) {
        gfx.drawFastVLine(bin, PLOT_Y, PLOT_H, COL_YELLOW);
    }
}

void SpectrumScreen::_drawInfo(bool force) {
    auto& gfx = _disp->gfx();
    static uint32_t lastInfoMs = 0;
    if (!force && millis() - lastInfoMs < 120) return;
    lastInfoMs = millis();

    gfx.fillRect(0, TOP_INFO_Y, 320, TOP_INFO_H, COL_BG);
    gfx.fillRect(0, BOTTOM_INFO_Y, 320, BOTTOM_INFO_H, COL_BG);
    gfx.setTextSize(FONT_TINY);

    float span   = ZOOM_SPANS[_zoomLevel];
    char buf[64];
    snprintf(buf, sizeof(buf), "Span:%.0fMHz  Step:%.1fkHz  Peak:%s  Bin:%03d",
             span, (float)span * 1000.f / NUM_BINS,
             _peakHold ? "ON" : "off", _curBin);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, TOP_INFO_Y + 3);
    gfx.print(buf);

    if (_markerBin >= 0) {
        float mf = _binToFreq(_markerBin);
        snprintf(buf, sizeof(buf), "Mrk:%.3fMHz  %.0fdBm",
                 mf, (double)_rssi[_markerBin]);
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(4, BOTTOM_INFO_Y + 3);
        gfx.print(buf);
    } else {
        snprintf(buf, sizeof(buf), "Center: %.3fMHz", _binToFreq(NUM_BINS / 2));
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(4, BOTTOM_INFO_Y + 3);
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
        _dirty = true;
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
    (void)dy;
    if (dx != 0) {
        if (_markerBin < 0) _markerBin = NUM_BINS / 2;
        _prevMarkerBin = _markerBin;
        _markerBin += dx * 2;
        if (_markerBin < 0)         _markerBin = 0;
        if (_markerBin >= NUM_BINS) _markerBin = NUM_BINS - 1;
    }
    if (click) {
        _prevMarkerBin = _markerBin;
        _markerBin = -1;
    }
}
