#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"
#include "../utils/RadiosondeDecoder.h"

class Display; class GPS; class UIManager;

// ================================================================
// Radiosonde Receiver — RS41, DFM09, M10
// Scans 400-406 MHz in FSK mode, decodes frames
// NOTE: T-Deck 433MHz antenna has reduced sensitivity below 433MHz
// ================================================================
class Radiosonde : public Screen {
public:
    Radiosonde(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Radiosonde"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    // Scan parameters
    float _scanFreqs[8] = {
        402.0f, 402.5f, 403.0f, 403.5f,
        404.0f, 404.5f, 405.0f, 405.5f
    };
    int   _scanIdx   = 0;
    float _curFreq   = 402.0f;
    float _manualFreq= 402.0f;
    bool  _scanning  = true;    // false = locked to one freq

    uint32_t _lastScanMs = 0;
    static constexpr uint32_t SCAN_DWELL_MS = 500;

    // Decoded data
    SondeFrame  _lastFrame;
    bool        _hasFrame = false;
    float       _signalRSSI = -120.f;
    int         _frameCount = 0;
    TextLog     _log;

    void _drawAll();
    void _drawSondeData();
    void _doScan();
    void _tryDecode(const RxPacket& pkt);
};
