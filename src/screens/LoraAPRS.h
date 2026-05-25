#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"
#include "../utils/APRSUtils.h"

class Display; class GPS; class UIManager;

// ================================================================
// LoRa APRS — Beacon TX (with GPS) + Packet RX + display
// ================================================================
class LoraAPRS : public Screen {
public:
    LoraAPRS(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "LoRa APRS"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    // Config (loaded from NVS)
    char  _callsign[12] = "N0CALL-9";
    char  _comment[32]  = "TDeck-RFMaster";
    char  _symbol       = '>';   // car
    uint16_t _txIntervalSec = 120;

    // State
    uint32_t _lastTxMs  = 0;
    bool     _txing     = false;
    int      _rxCount   = 0;

    TextLog  _log;
    int      _tabSel    = 0;   // 0=RX log, 1=TX/Config

    void _drawAll();
    void _drawRXLog();
    void _drawTXPanel();
    void _txBeacon();
    void _pollRx();
    void _decodeAndLog(const RxPacket& pkt);
};
