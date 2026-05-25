#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// RTTY — RadioLib RTTYClient for TX, FSK-based RX display
// Modes: 45.45 / 50 / 75 / 100 baud, shift 170/450/850 Hz
// ================================================================
class RTTYScreen : public Screen {
public:
    RTTYScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "RTTY"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    float    _freq    = 434.0f;
    uint32_t _shift   = 450;      // Hz
    float    _baud    = 45.45f;
    int      _baudIdx = 0;
    int      _shiftIdx= 1;

    static constexpr float   BAUD_OPTIONS[]  = {45.45f, 50.f, 75.f, 100.f};
    static constexpr uint32_t SHIFT_OPTIONS[] = {170, 450, 850};

    bool     _txMode   = false;
    bool     _txActive = false;
    InputBox _txInput;
    TextLog  _rxLog;
    bool     _rxMode   = false;

    void _drawAll();
    void _startTX();
    void _stopTX();
    void _startRX();
    void _stopRX();
    void _pollRx();
    void _drawTXPanel();
    void _drawRXPanel();
};
