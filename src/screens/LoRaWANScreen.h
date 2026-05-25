#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// LoRaWAN — OTAA Join + Uplink (using RadioLib LoRaWAN)
// ================================================================
class LoRaWANScreen : public Screen {
public:
    LoRaWANScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "LoRaWAN"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    // OTAA keys (configure in Settings)
    uint8_t  _joinEUI[8]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    uint8_t  _devEUI[8]   = {0x70,0xB3,0xD5,0x7E,0xD0,0x06,0x00,0x01};
    uint8_t  _appKey[16]  = {0x2B,0x7E,0x15,0x16,0x28,0xAE,0xD2,0xA6,
                              0xAB,0xF7,0x15,0x88,0x09,0xCF,0x4F,0x3C};

    enum class State { IDLE, JOINING, JOINED, UPLINK, ERROR } _state;
    int      _fCntUp  = 0;
    int      _fCntDn  = 0;
    uint16_t _devAddr = 0;

    TextLog  _log;
    int      _tabSel = 0;  // 0=Status, 1=Keys

    void _drawAll();
    void _drawStatus();
    void _drawKeys();
    void _doJoin();
    void _sendUplink();
};
