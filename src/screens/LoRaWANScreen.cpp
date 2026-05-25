#include "LoRaWANScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include <Arduino.h>

LoRaWANScreen::LoRaWANScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui), _state(State::IDLE) {}

void LoRaWANScreen::onEnter() {
    _dirty = true;
}

void LoRaWANScreen::onExit() {
    _radio->standby();
}

void LoRaWANScreen::update() {
    if (_dirty) {
        _drawAll();
        _dirty = false;
    }
}

void LoRaWANScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "LoRaWAN  EU433", COL_LORAWAN);

    drawButton(&gfx,   0, 44, 90, 12, "Status",  _tabSel==0, COL_LORAWAN);
    drawButton(&gfx,  92, 44, 90, 12, "Keys",     _tabSel==1, COL_LORAWAN);
    gfx.drawFastHLine(0, 58, 320, COL_DIVIDER);

    if (_tabSel == 0) _drawStatus();
    else              _drawKeys();

    const char* hint = (_state == State::JOINED) ? "U=Uplink" :
                       (_state == State::IDLE)   ? "J=Join"   : "...";
    drawHints(&gfx, "HOLD=Back", "TAB=Switch", hint);
}

void LoRaWANScreen::_drawStatus() {
    auto& gfx = _disp->gfx();
    int y = 60;

    // State badge
    const char* stateStr;
    uint16_t stateCol;
    switch (_state) {
        case State::IDLE:    stateStr="IDLE";     stateCol=COL_TEXT_DIM; break;
        case State::JOINING: stateStr="JOINING";  stateCol=COL_YELLOW;   break;
        case State::JOINED:  stateStr="JOINED";   stateCol=COL_GREEN;    break;
        case State::UPLINK:  stateStr="UPLINK TX";stateCol=COL_LORAWAN;  break;
        case State::ERROR:   stateStr="ERROR";    stateCol=COL_RED;      break;
        default:             stateStr="?";        stateCol=COL_TEXT_DIM; break;
    }
    gfx.fillRoundRect(4, y, 80, 14, 3, stateCol);
    gfx.setTextColor(COL_BG, stateCol);
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(8, y + 3);
    gfx.print(stateStr);

    y += 18;

    if (_state == State::JOINED) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%04X", _devAddr);
        drawKV(&gfx, 4, y,     "DevAddr  : ", buf, COL_TEXT_DIM, COL_LORAWAN);
        snprintf(buf, sizeof(buf), "%d", _fCntUp);
        drawKV(&gfx, 4, y+12,  "FCnt Up  : ", buf, COL_TEXT_DIM, COL_TEXT);
        snprintf(buf, sizeof(buf), "%d", _fCntDn);
        drawKV(&gfx, 4, y+24,  "FCnt Down: ", buf, COL_TEXT_DIM, COL_TEXT);

        if (_gps && _gps->hasFix()) {
            char pos[24];
            snprintf(pos, sizeof(pos), "%.4f,%.4f",
                     _gps->lat(), _gps->lon());
            drawKV(&gfx, 4, y+36, "GPS Pos  : ", pos, COL_TEXT_DIM, COL_GREEN);
        }
    } else if (_state == State::IDLE) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(30, y + 20);
        gfx.print("Press J to OTAA Join");
        gfx.setTextSize(FONT_TINY);
        gfx.setCursor(30, y + 40);
        gfx.print("Configure keys in Keys tab");
    }

    gfx.drawFastHLine(0, 150, 320, COL_DIVIDER);
    _log.draw(&gfx, 0, 152, 320, 62, COL_LORAWAN);
}

void LoRaWANScreen::_drawKeys() {
    auto& gfx = _disp->gfx();
    int y = 62;

    auto printHex = [&](int x, int row, const uint8_t* key, int len) {
        char buf[40] = "";
        for (int i = 0; i < len; i++) {
            char h[4];
            snprintf(h, sizeof(h), "%02X", key[i]);
            strncat(buf, h, sizeof(buf) - strlen(buf) - 1);
            if (i == len/2 - 1) strncat(buf, " ", 2);
        }
        gfx.setCursor(x, row);
        gfx.print(buf);
    };

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y); gfx.print("JoinEUI:");
    gfx.setTextColor(COL_LORAWAN, COL_BG);
    printHex(4, y + 10, _joinEUI, 8);

    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y + 24); gfx.print("DevEUI:");
    gfx.setTextColor(COL_LORAWAN, COL_BG);
    printHex(4, y + 34, _devEUI, 8);

    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y + 48); gfx.print("AppKey:");
    gfx.setTextColor(COL_LORAWAN, COL_BG);
    printHex(4, y + 58, _appKey, 16);

    gfx.setTextColor(COL_YELLOW, COL_BG);
    gfx.setCursor(4, y + 90);
    gfx.print("Edit keys via Settings menu");
}

void LoRaWANScreen::_doJoin() {
    _state = State::JOINING;
    _log.addLine("[OTAA] Sending Join Request...");
    _dirty = true;
    _drawAll();

    // Note: RadioLib LoRaWAN requires LoRaWANNode class
    // Full implementation needs: LoRaWANNode node(&radio, &channels, plan)
    // This is a structural placeholder — add full OTAA in production
    // For now, simulate the flow
    delay(500);
    _log.addLine("[OTAA] Join Accept received");
    _log.addLine("[OTAA] Network joined!");
    _devAddr = 0x1234ABCD & 0xFFFF;
    _state   = State::JOINED;
    _dirty   = true;
}

void LoRaWANScreen::_sendUplink() {
    if (_state != State::JOINED) {
        _log.addLine("[ERR] Not joined");
        _dirty = true;
        return;
    }

    _state = State::UPLINK;
    _dirty = true;
    _drawAll();

    // Build payload: lat/lon if GPS available
    uint8_t payload[12];
    size_t  payloadLen = 0;

    if (_gps && _gps->hasFix()) {
        int32_t lat = (int32_t)(_gps->lat() * 1e6);
        int32_t lon = (int32_t)(_gps->lon() * 1e6);
        payload[0] = (lat >> 16) & 0xFF;
        payload[1] = (lat >>  8) & 0xFF;
        payload[2] =  lat        & 0xFF;
        payload[3] = (lon >> 16) & 0xFF;
        payload[4] = (lon >>  8) & 0xFF;
        payload[5] =  lon        & 0xFF;
        payloadLen = 6;
    } else {
        payload[0] = 0xDE; payload[1] = 0xAD;
        payloadLen = 2;
    }

    // Transmit via LoRa (would normally go through LoRaWANNode)
    bool ok = _radio->loraTx(payload, payloadLen);

    char line[40];
    snprintf(line, sizeof(line), ok ? "[UP] FCnt=%d sent" : "[UP] TX failed",
             _fCntUp);
    _log.addLine(line);
    if (ok) _fCntUp++;

    _state = State::JOINED;
    _dirty = true;
}

void LoRaWANScreen::onKey(char key) {
    if (key == KEY_TAB || key == '\t') {
        _tabSel = (_tabSel + 1) % 2; _dirty = true; return;
    }
    if (key == 'j' || key == 'J') { _doJoin(); return; }
    if (key == 'u' || key == 'U') { _sendUplink(); return; }
}

void LoRaWANScreen::onTrackball(int dx, int dy, bool click) {
    if (dx != 0) { _tabSel = (_tabSel + 1) % 2; _dirty = true; }
    if (click) {
        if (_state == State::IDLE)   _doJoin();
        else if (_state == State::JOINED) _sendUplink();
    }
}
