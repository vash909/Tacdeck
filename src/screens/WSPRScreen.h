#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// WSPR Beacon — uses RadioLib WSPRClient
// Standard format: callsign, 4-char grid, power (dBm)
// Transmits on even 2-minute slots
// ================================================================
class WSPRScreen : public Screen {
public:
    WSPRScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "WSPR"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    char    _callsign[12] = "N0CALL";
    char    _grid[5]      = "AA00";
    int8_t  _powerDbm     = WSPR_DEFAULT_POWER_DBM;
    float   _freq         = WSPR_TX_FREQ;

    bool    _enabled    = false;
    bool    _txing      = false;
    int     _txCount    = 0;
    uint32_t _txStartMs = 0;
    uint32_t _nextTxMs  = 0;
    static constexpr uint32_t WSPR_TX_MS = 110700;  // ~110.7 sec TX

    TextLog _log;
    int     _editField = 0;   // 0=none, 1=call, 2=grid, 3=pwr
    InputBox _editBuf;
    uint32_t _lastLiveUpdateMs = 0;

    void _drawAll();
    void _drawLiveStatus();
    void _startBeacon();
    void _stopBeacon();
    bool _isEvenMinute();
};
