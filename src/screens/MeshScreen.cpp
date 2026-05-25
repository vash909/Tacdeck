#include "MeshScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>

MeshScreen::MeshScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void MeshScreen::onEnter() {
    _myNodeId = _makeNodeId();
    Storage::getString(NVS_KEY_MESH_NODE_ID, _myName, sizeof(_myName), "TDECK-1");

    LoRaCfg cfg;
    cfg.freq     = MESH_LORA_FREQ;
    cfg.bw       = MESH_LORA_BW;
    cfg.sf       = MESH_LORA_SF;
    cfg.cr       = MESH_LORA_CR;
    cfg.syncWord = MESH_SYNC_WORD;
    _radio->loraBegin(cfg);
    _radio->loraStartRx();

    _sendBeacon();
    _dirty = true;
}

void MeshScreen::onExit() {
    _radio->standby();
}

void MeshScreen::update() {
    _pollRx();

    // Periodic beacon
    if (millis() - _lastBeaconMs > MESH_BEACON_INTERVAL_S * 1000UL) {
        _sendBeacon();
    }

    if (_dirty) {
        _drawAll();
        _dirty = false;
    }
}

// ================================================================
void MeshScreen::_sendBeacon() {
    // Packet: TYPE | TTL | SRC(4) | DST(4)=BCAST | SEQ(2) | NAME
    uint8_t pkt[32];
    pkt[0] = (uint8_t)MeshPktType::BEACON;
    pkt[1] = MESH_MAX_HOPS;
    pkt[2] = (_myNodeId >> 24) & 0xFF;
    pkt[3] = (_myNodeId >> 16) & 0xFF;
    pkt[4] = (_myNodeId >>  8) & 0xFF;
    pkt[5] =  _myNodeId        & 0xFF;
    pkt[6] = 0xFF; pkt[7] = 0xFF; pkt[8] = 0xFF; pkt[9] = 0xFF; // BCAST
    pkt[10] = (_seqNum >> 8) & 0xFF;
    pkt[11] = _seqNum & 0xFF;
    _seqNum++;

    // GPS payload if available
    size_t nameLen = strlen(_myName);
    memcpy(pkt + 12, _myName, nameLen);
    size_t pktLen = 12 + nameLen;

    if (_gps && _gps->hasFix()) {
        pkt[pktLen++] = '|';
        char gpsBuf[20];
        snprintf(gpsBuf, sizeof(gpsBuf), "%.4f,%.4f",
                 _gps->lat(), _gps->lon());
        size_t gl = strlen(gpsBuf);
        memcpy(pkt + pktLen, gpsBuf, gl);
        pktLen += gl;
    }

    _radio->loraTx(pkt, pktLen);
    _radio->loraStartRx();
    _lastBeaconMs = millis();
}

void MeshScreen::_sendMessage(const char* text) {
    if (!text || strlen(text) == 0) return;

    uint8_t pkt[80];
    pkt[0] = (uint8_t)MeshPktType::MSG;
    pkt[1] = MESH_MAX_HOPS;
    pkt[2] = (_myNodeId >> 24) & 0xFF;
    pkt[3] = (_myNodeId >> 16) & 0xFF;
    pkt[4] = (_myNodeId >>  8) & 0xFF;
    pkt[5] =  _myNodeId        & 0xFF;
    pkt[6] = 0xFF; pkt[7] = 0xFF; pkt[8] = 0xFF; pkt[9] = 0xFF;
    pkt[10] = (_seqNum >> 8) & 0xFF;
    pkt[11] = _seqNum & 0xFF;
    _seqNum++;

    size_t nameLen = strlen(_myName);
    size_t textLen = strlen(text);
    memcpy(pkt + 12, _myName, nameLen);
    pkt[12 + nameLen] = ':';
    memcpy(pkt + 13 + nameLen, text, textLen);
    size_t pktLen = 13 + nameLen + textLen;

    _radio->loraTx(pkt, min(pktLen, (size_t)80));
    _radio->loraStartRx();

    // Add to local chat
    if (_msgCount < MAX_MSGS) {
        MeshMsg& m = _msgs[_msgCount++];
        strncpy(m.from, _myName, sizeof(m.from) - 1);
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.ts   = millis();
        m.hops = 0;
        m.acked= false;
    }
    _dirty = true;
}

void MeshScreen::_pollRx() {
    if (!_radio->loraAvailable()) return;
    RxPacket pkt;
    if (!_radio->loraRead(pkt) || !pkt.valid) return;

    if (!_parsePkt(pkt.data, pkt.len)) return;

    // Re-broadcast with decremented TTL
    if (pkt.len > 1 && pkt.data[1] > 1) {
        uint8_t fwd[256];
        memcpy(fwd, pkt.data, pkt.len);
        fwd[1]--;   // decrement TTL
        delay(random(10, 50));   // random backoff
        _radio->loraTx(fwd, pkt.len);
        _radio->loraStartRx();
    }
    _dirty = true;
}

bool MeshScreen::_parsePkt(const uint8_t* data, size_t len) {
    if (len < 12) return false;

    MeshPktType type = (MeshPktType)data[0];
    uint8_t     ttl  = data[1];
    uint32_t    src  = ((uint32_t)data[2] << 24) | ((uint32_t)data[3] << 16) |
                       ((uint32_t)data[4] <<  8) |  (uint32_t)data[5];

    // Ignore our own packets
    if (src == _myNodeId) return false;

    if (type == MeshPktType::BEACON || type == MeshPktType::MSG) {
        // Extract name from payload
        char nameStr[12] = "?";
        size_t payloadOff = 12;
        if (payloadOff < len) {
            size_t nlen = 0;
            while (payloadOff + nlen < len && data[payloadOff + nlen] != '|'
                   && data[payloadOff + nlen] != ':' && nlen < 11) nlen++;
            memcpy(nameStr, data + payloadOff, nlen);
            nameStr[nlen] = '\0';
        }

        // Update node table
        bool found = false;
        for (int i = 0; i < _nodeCount; i++) {
            if (_nodes[i].nodeId == src) {
                _nodes[i].lastSeenMs = millis();
                _nodes[i].hops       = MESH_MAX_HOPS - ttl;
                found = true;
                break;
            }
        }
        if (!found && _nodeCount < MAX_NODES) {
            MeshNode& n = _nodes[_nodeCount++];
            n.nodeId     = src;
            strncpy(n.name, nameStr, sizeof(n.name) - 1);
            n.lastSeenMs = millis();
            n.hops       = MESH_MAX_HOPS - ttl;
        }

        // For MSG type, extract text
        if (type == MeshPktType::MSG && _msgCount < MAX_MSGS) {
            MeshMsg& m = _msgs[_msgCount++];
            strncpy(m.from, nameStr, sizeof(m.from) - 1);
            // Text after "name:"
            const char* colonPos = (const char*)memchr(
                data + 12, ':', len - 12);
            if (colonPos) {
                size_t textOff = (colonPos - (const char*)data) + 1;
                size_t textLen = len - textOff;
                if (textLen >= sizeof(m.text)) textLen = sizeof(m.text) - 1;
                memcpy(m.text, data + textOff, textLen);
                m.text[textLen] = '\0';
            }
            m.ts   = millis();
            m.hops = MESH_MAX_HOPS - ttl;
            m.acked= false;
        }
    }
    return true;
}

// ================================================================
void MeshScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "LoRa Mesh", COL_MESH);

    // My ID
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "%s [%08lX]",
             _myName, (unsigned long)_myNodeId);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_MESH, COL_BG);
    gfx.setCursor(4, 47);
    gfx.print(idBuf);

    drawButton(&gfx,   0, 57, 90, 12, "Chat", _tabSel==0, COL_MESH);
    drawButton(&gfx,  92, 57, 90, 12, "Nodes",_tabSel==1, COL_MESH);

    // Node count badge
    char nBuf[6];
    snprintf(nBuf, sizeof(nBuf), "%d", _nodeCount);
    gfx.setTextColor(COL_GREEN, COL_BG);
    gfx.setCursor(240, 60);
    gfx.print(nBuf);
    gfx.print(" nodes");

    gfx.drawFastHLine(0, 71, 320, COL_DIVIDER);

    if (_tabSel == 0) _drawChat();
    else              _drawNodes();

    drawHints(&gfx, "HOLD=Back",
              "TAB=Switch",
              _typing ? "ENTER=Send" : "T=Type");
}

void MeshScreen::_drawChat() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 72;
    constexpr int LINE_H = 14;
    constexpr int MSGS_H = 130;

    gfx.fillRect(0, Y0, 320, MSGS_H, COL_BG);

    int start = _msgCount > (MSGS_H / LINE_H) ?
                _msgCount - (MSGS_H / LINE_H) : 0;

    for (int i = start; i < _msgCount; i++) {
        const MeshMsg& m = _msgs[i];
        int y = Y0 + (i - start) * LINE_H;

        bool mine = (strcmp(m.from, _myName) == 0);
        uint16_t col = mine ? COL_MESH : COL_GREEN;

        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(col, COL_BG);
        char line[54];
        snprintf(line, sizeof(line), "[%s] %s", m.from, m.text);
        gfx.setCursor(2, y + 2);
        gfx.print(line);

        if (m.hops > 0) {
            char hopBuf[6];
            snprintf(hopBuf, sizeof(hopBuf), "+%dh", m.hops);
            gfx.setTextColor(COL_TEXT_DIM, COL_BG);
            gfx.setCursor(302, y + 2);
            gfx.print(hopBuf);
        }
    }

    gfx.drawFastHLine(0, Y0 + MSGS_H, 320, COL_DIVIDER);

    if (_typing) {
        _chatInput.active = true;
        _chatInput.draw(&gfx, 2, Y0 + MSGS_H + 2, 316, COL_MESH);
    } else {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(4, Y0 + MSGS_H + 4);
        gfx.print("Press T to send a message...");
    }
}

void MeshScreen::_drawNodes() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 72;
    constexpr int H  = 20;

    gfx.fillRect(0, Y0, 320, 144, COL_BG);

    if (_nodeCount == 0) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, Y0 + 40);
        gfx.print("No nodes heard yet");
        return;
    }

    for (int i = 0; i < _nodeCount && i < 7; i++) {
        const MeshNode& n = _nodes[i];
        int y = Y0 + i * H;

        gfx.drawFastHLine(0, y, 320, COL_BORDER);
        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_MESH, COL_BG);
        gfx.setCursor(4, y + 2);
        gfx.print(n.name);

        char idBuf[12];
        snprintf(idBuf, sizeof(idBuf), "%08lX", (unsigned long)n.nodeId);
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(80, y + 2);
        gfx.print(idBuf);

        char ageBuf[12];
        uint32_t age = (millis() - n.lastSeenMs) / 1000;
        snprintf(ageBuf, sizeof(ageBuf), "%lus", (unsigned long)age);
        gfx.setCursor(176, y + 2);
        gfx.print(ageBuf);

        char hopBuf[8];
        snprintf(hopBuf, sizeof(hopBuf), "%dhop", n.hops);
        gfx.setTextColor(COL_GREEN, COL_BG);
        gfx.setCursor(240, y + 2);
        gfx.print(hopBuf);
    }
}

// ================================================================
uint32_t MeshScreen::_makeNodeId() {
    // Use lower 32 bits of ESP32 chip ID (MAC derived)
    uint64_t mac = ESP.getEfuseMac();
    return (uint32_t)(mac & 0xFFFFFFFF);
}

void MeshScreen::onKey(char key) {
    if (key == KEY_ESC) {
        if (_typing) {
            _typing = false;
            _chatInput.clear();
            _dirty = true;
        } else {
            _ui->pop();
        }
        return;
    }

    if (key == KEY_TAB || key == '\t') {
        _tabSel = (_tabSel + 1) % 2; _dirty = true; return;
    }
    if (key == 't' || key == 'T') {
        if (!_typing) { _typing = true; _chatInput.clear(); _dirty = true; }
        return;
    }
    if (_typing) {
        if (key == KEY_ENTER) {
            _sendMessage(_chatInput.buf);
            _chatInput.clear();
            _typing = false;
            return;
        }
        _chatInput.input(key);
        _drawChat();
    }
    if (key == 'b' || key == 'B') { _sendBeacon(); }
}

void MeshScreen::onTrackball(int dx, int dy, bool click) {
    if (dx != 0) { _tabSel = (_tabSel + 1) % 2; _dirty = true; }
    if (click && _typing) {
        _sendMessage(_chatInput.buf);
        _chatInput.clear();
        _typing = false;
        _dirty = true;
    }
}
