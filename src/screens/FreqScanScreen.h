#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

struct ScannedCh {
    float   freqMHz;
    float   rssi;
    uint32_t lastActiveMs;
    int      hitCount;
};

// ================================================================
// Frequency Scanner — sweeps channels, stops on activity
// ================================================================
class FreqScanScreen : public Screen {
public:
    FreqScanScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Freq Scanner"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    float _startFreq = SCANNER_START_FREQ;
    float _endFreq   = SCANNER_END_FREQ;
    float _stepKHz   = SCANNER_STEP_KHZ;
    float _squelch   = SCANNER_SQUELCH_DBM;

    static constexpr int MAX_CHANNELS = 64;
    ScannedCh _channels[MAX_CHANNELS];
    int       _numChannels = 0;
    int       _curIdx      = 0;
    int       _lockedIdx   = -1;  // -1 = scanning
    int       _viewOffset  = 0;   // scroll for list

    bool      _scanning    = false;
    float     _curRSSI     = -120.f;
    uint32_t  _lastStepMs  = 0;
    static constexpr uint32_t DWELL_MS = 30;
    static constexpr uint32_t LOCK_MS  = 3000;  // stay locked 3s

    void _drawAll();
    void _drawChannelList();
    void _scanStep();
    void _buildChannelList();
};
