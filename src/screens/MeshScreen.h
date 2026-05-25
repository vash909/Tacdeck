#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// LoRa Mesh Network — store-and-forward mesh protocol
// Custom lightweight protocol compatible with the project
// Packet format: [TYPE(1) | TTL(1) | SRC(4) | DST(4) | SEQ(2) | PAYLOAD]
// ================================================================

enum class MeshPktType : uint8_t {
    BEACON  = 0x01,   // Node alive broadcast
    MSG     = 0x02,   // Text message
    ACK     = 0x03,   // Acknowledgment
    TRACE   = 0x04,   // Route trace
    GPS_POS = 0x05,   // GPS position share
};

struct MeshNode {
    uint32_t nodeId;
    char     name[12];
    float    lat, lon;
    int8_t   rssi;
    uint32_t lastSeenMs;
    uint8_t  hops;
};

struct MeshMsg {
    char     from[12];
    char     text[48];
    uint32_t ts;
    uint8_t  hops;
    bool     acked;
};

class MeshScreen : public Screen {
public:
    MeshScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Mesh Net"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    uint32_t _myNodeId = 0;
    char     _myName[12] = "TDECK-1";

    static constexpr int MAX_NODES = 8;
    static constexpr int MAX_MSGS  = 16;
    MeshNode _nodes[MAX_NODES];
    int      _nodeCount = 0;
    MeshMsg  _msgs[MAX_MSGS];
    int      _msgCount  = 0;

    int      _tabSel   = 0;   // 0=Chat, 1=Nodes
    InputBox _chatInput;
    bool     _typing   = false;
    uint32_t _lastBeaconMs = 0;
    uint16_t _seqNum   = 0;

    void _drawAll();
    void _drawChat();
    void _drawNodes();
    void _sendBeacon();
    void _sendMessage(const char* text);
    void _pollRx();
    bool _parsePkt(const uint8_t* data, size_t len);
    void _rebroadcast(const uint8_t* data, size_t len);
    uint32_t _makeNodeId();
};
