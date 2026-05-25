#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"
#include "../utils/CWUtils.h"

class Display; class GPS; class UIManager;

// ================================================================
// CW / Morse Code Beacon + Decoder
// TX via RadioLib MorseClient (OOK carrier)
// ================================================================
class CWScreen : public Screen {
public:
    CWScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "CW Beacon"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    float   _freq        = CW_DEFAULT_FREQ;
    int     _wpm         = CW_DEFAULT_SPEED;
    int8_t  _power       = CW_DEFAULT_POWER;
    char    _beaconText[64] = "CQ CQ DE TDECK-RFMASTER K";
    bool    _beaconEnabled  = false;
    bool    _txing          = false;
    uint32_t _lastTxMs      = 0;
    uint32_t _txIntervalSec = 60;

    InputBox _msgInput;
    bool     _editing    = false;
    TextLog  _txLog;
    uint32_t _lastLiveUpdateMs = 0;

    void _drawAll();
    void _drawLiveStatus();
    void _txMessage(const char* msg);
    void _drawMorseVisual(const char* morse);
};
