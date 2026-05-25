#include "LoraChat.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/StatusBar.h"
#include <Arduino.h>

LoraChat::LoraChat(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void LoraChat::onEnter() {
    // Configure LoRa
    _cfg.freq     = LORA_DEFAULT_FREQ;
    _cfg.sf       = LORA_DEFAULT_SF;
    _cfg.bw       = LORA_DEFAULT_BW;
    _cfg.cr       = LORA_DEFAULT_CR;
    _cfg.syncWord = LORA_DEFAULT_SYNC;
    _cfg.power    = LORA_DEFAULT_POWER;

    _radio->loraBegin(_cfg);
    _radio->loraStartRx();

    _input.clear();
    _typing  = false;
    _dirty   = true;
}

void LoraChat::onExit() {
    _radio->standby();
}

void LoraChat::update() {
    _pollRx();

    if (_dirty) {
        _drawAll();
        _dirty = false;
    } else if (_typing) {
        // Redraw input area only (cursor blink)
        _drawInputArea();
    }
}

// ================================================================
void LoraChat::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);

    drawHeader(&gfx, "LoRa Chat", COL_LORA);

    // Freq / config line
    char cfgLine[40];
    snprintf(cfgLine, sizeof(cfgLine), "%.3f MHz  SF%u  BW%.0f",
             _cfg.freq, _cfg.sf, _cfg.bw);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, 47);
    gfx.print(cfgLine);

    gfx.drawFastHLine(0, 57, 320, COL_DIVIDER);

    _drawMessages();
    _drawInputArea();
    drawHints(&gfx, "ESC=Back", "ENTER=Send", "T=Type");
}

void LoraChat::_drawMessages() {
    auto& gfx = _disp->gfx();
    constexpr int AREA_Y = 58;
    constexpr int AREA_H = 144;   // up to input box
    constexpr int LINE_H = 14;

    gfx.fillRect(0, AREA_Y, 320, AREA_H, COL_BG);

    int start = _msgCount > (AREA_H / LINE_H) ?
                _msgCount - (AREA_H / LINE_H) : 0;

    for (int i = start; i < _msgCount; i++) {
        const ChatMsg& m = _msgs[i];
        int y = AREA_Y + (i - start) * LINE_H;

        if (m.mine) {
            // Right-aligned, cyan
            gfx.setTextColor(COL_CYAN, COL_BG);
            int tw = strlen(m.text) * 6;
            gfx.setCursor(316 - tw, y + 2);
            gfx.setTextSize(FONT_TINY);
            gfx.print(m.text);
        } else {
            // Left-aligned, green
            gfx.setTextColor(COL_GREEN, COL_BG);
            gfx.setTextSize(FONT_TINY);
            gfx.setCursor(2, y + 2);
            gfx.print(m.text);
            // RSSI badge
            char rssiStr[8];
            snprintf(rssiStr, sizeof(rssiStr), "%ddB", m.rssi);
            gfx.setTextColor(COL_TEXT_DIM, COL_BG);
            gfx.setCursor(316 - (int)(strlen(rssiStr)*6), y + 2);
            gfx.print(rssiStr);
        }
    }
}

void LoraChat::_drawInputArea() {
    auto& gfx = _disp->gfx();
    constexpr int Y = 204;

    gfx.fillRect(0, Y, 320, 12, COL_BG);
    gfx.drawFastHLine(0, Y, 320, COL_DIVIDER);

    if (_typing) {
        _input.active = true;
        _input.draw(&gfx, 2, Y + 1, 290, COL_LORA);
        drawButton(&gfx, 294, Y, 24, 12, "TX", false, COL_LORA);
    } else {
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(4, Y + 2);
        gfx.print("Press T to type a message...");
    }
}

// ================================================================
void LoraChat::_addMsg(const char* text, bool mine, int8_t rssi) {
    if (_msgCount < MAX_MSGS) {
        ChatMsg& m = _msgs[_msgCount++];
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.mine = mine;
        m.rssi = rssi;
        m.ts   = millis();
    } else {
        memmove(_msgs, _msgs + 1, sizeof(ChatMsg) * (MAX_MSGS - 1));
        ChatMsg& m = _msgs[MAX_MSGS - 1];
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.mine = mine;
        m.rssi = rssi;
        m.ts   = millis();
    }
    _drawMessages();
}

void LoraChat::_sendMsg() {
    if (strlen(_input.buf) == 0) return;

    bool ok = _radio->loraTx(
        (const uint8_t*)_input.buf, strlen(_input.buf));

    if (ok) {
        _addMsg(_input.buf, true);
    } else {
        _addMsg("[TX failed]", true);
    }

    _input.clear();
    _typing = false;
    _radio->loraStartRx();
    _dirty = true;
}

void LoraChat::_pollRx() {
    if (!_radio->loraAvailable()) return;

    RxPacket pkt;
    if (_radio->loraRead(pkt) && pkt.valid) {
        pkt.data[pkt.len] = '\0';
        // Filter non-printable
        for (size_t i = 0; i < pkt.len; i++) {
            if (pkt.data[i] < 0x20 || pkt.data[i] > 0x7E)
                pkt.data[i] = '.';
        }
        _addMsg((char*)pkt.data, false, (int8_t)pkt.rssi);
    }
}

// ================================================================
void LoraChat::onKey(char key) {
    if (key == KEY_ESC && _typing) {
        _typing = false;
        _input.clear();
        _dirty = true;
        return;
    }
    if (key == 't' || key == 'T') {
        _typing = true;
        _dirty  = true;
        return;
    }
    if (_typing) {
        if (key == KEY_ENTER) { _sendMsg(); return; }
        _input.input(key);
        _drawInputArea();
        return;
    }
}

void LoraChat::onTrackball(int dx, int dy, bool click) {
    if (click && !_typing) {
        _typing = true;
        _dirty  = true;
    } else if (click && _typing) {
        _sendMsg();
    }
}
