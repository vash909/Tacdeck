#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// LoRa P2P Chat — send/receive text messages over LoRa
// ================================================================

struct ChatMsg {
    char     text[54];
    bool     mine;           // true = sent by me
    int8_t   rssi;
    uint32_t ts;
};

class LoraChat : public Screen {
public:
    LoraChat(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "LoRa Chat"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    static constexpr int MAX_MSGS = 10;
    ChatMsg  _msgs[MAX_MSGS];
    int      _msgCount = 0;
    InputBox _input;
    bool     _typing   = false;

    LoRaCfg  _cfg;

    void _drawAll();
    void _drawMessages();
    void _drawInputArea();
    void _addMsg(const char* text, bool mine, int8_t rssi = 0);
    void _sendMsg();
    void _pollRx();
};
