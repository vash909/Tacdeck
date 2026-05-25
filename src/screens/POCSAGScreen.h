#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// POCSAG Pager Receiver — 512 / 1200 / 2400 bps
// Uses SX1262 in FSK/OOK mode, software POCSAG framing
// ================================================================
struct POCSAGMsg {
    uint32_t capcode;
    char     text[40];
    uint16_t baud;
    uint32_t ts;
};

class POCSAGScreen : public Screen {
public:
    POCSAGScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "POCSAG"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    float    _freq     = POCSAG_FREQ;
    float    _baudRate = 1.2f;   // 1200 bps default
    int      _baudIdx  = 1;
    static constexpr float BAUDS[] = {0.512f, 1.2f, 2.4f};

    static constexpr int MAX_MSGS = 12;
    POCSAGMsg _msgs[MAX_MSGS];
    int       _msgCount  = 0;

    TextLog   _rawLog;
    bool      _showRaw = false;
    float     _listenRSSI = -120.f;

    // POCSAG sync & decode state
    uint32_t _shiftReg = 0;
    int      _bitCount = 0;

    void _drawAll();
    void _drawStatusLine();
    void _drawMessages();
    void _pollRx();
    bool _decodePOCSAG(const uint8_t* data, size_t len);
    void _addMsg(uint32_t capcode, const char* text);
};
