#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// Spectrum Analyzer — sweeps a frequency range and plots RSSI
// Uses SX1262 RSSI in FSK mode, one channel at a time
// ================================================================
class SpectrumScreen : public Screen {
public:
    SpectrumScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Spectrum"; }
    bool handlesEsc() const override { return false; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    // Sweep parameters
    float _startFreq = SPECTRUM_START_FREQ;
    float _endFreq   = SPECTRUM_END_FREQ;
    float _stepKHz   = SPECTRUM_STEP_KHZ;

    static constexpr int  PLOT_W = 320;
    static constexpr int  PLOT_H = 128;   // content area used for plot
    static constexpr int  PLOT_Y = 64;    // y start of plot
    static constexpr int  NUM_BINS = PLOT_W;

    float _rssi[NUM_BINS];
    float _peakRssi[NUM_BINS];   // peak hold
    int   _curBin   = 0;
    bool  _sweeping = true;
    bool  _peakHold = false;
    float _markerFreq = 0.f;
    int   _markerBin  = -1;

    int   _zoomLevel  = 0;   // 0=10MHz, 1=5MHz, 2=2MHz, 3=1MHz span
    static constexpr float ZOOM_SPANS[] = {10.f, 5.f, 2.f, 1.f};

    void _drawPlot();
    void _drawAxes();
    void _drawInfo();
    void _sweepStep();

    float _binToFreq(int bin) const;
    int   _freqToBin(float freq) const;
};
