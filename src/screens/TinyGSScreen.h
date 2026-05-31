#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"
#include "../utils/TLEFetcher.h"
#include <cmath>

class Display; class GPS; class UIManager;

// ================================================================
// TinyGS-compatible LoRa satellite receiver
// Covers 430-440 MHz (SX1262 range on T-Deck 433)
// Satellites, frequencies and modem params from TinyGS open database
// NORAD IDs: verify/update at db.satnogs.org or tinygs.com
// ================================================================

// Minimum elevation for auto-RX (degrees)
#define TINYGS_EL_MIN   5.0
// Packet ring buffer depth
#define TINYGS_PKT_BUF  16
// Fetch full amateur-satellite group and filter to our 9 NORAD IDs client-side.
// More reliable than CATNR multi-query (some older sats may be absent there).
#define TINYGS_TLE_URL \
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"

// ----------------------------------------------------------------
struct TGSSat {
    uint32_t    norad;      // NORAD catalog number (for TLE lookup)
    const char* name;       // short display name (≤12 chars)
    float       freqMHz;    // nominal downlink frequency
    uint8_t     sf;         // LoRa spreading factor  7-12
    float       bwKHz;      // LoRa bandwidth (125 / 250 kHz)
    uint8_t     cr;         // coding rate denominator (5=4/5 … 8=4/8)
    uint8_t     sw;         // LoRa sync word (0x12 = RadioLib private)
    uint8_t     preamble;   // preamble length (symbols)
};

// ----------------------------------------------------------------
struct TGSPacket {
    uint8_t  data[48];   // raw payload (most cubesat telemetry fits here)
    uint8_t  len;
    float    rssi;
    float    snr;
    uint32_t tsMs;       // millis() at reception
    uint8_t  satIdx;     // index into TinyGSScreen::SATS[]
};

// ================================================================
class TinyGSScreen : public Screen {
public:
    TinyGSScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "TinyGS RX"; }
    bool handlesEsc() const override { return true; }

    // Public so MainMenu can draw the icon without pulling SATS[]
    static constexpr int N_SATS = 9;
    static const TGSSat  SATS[];

private:
    Display*    _disp;
    Radio*      _radio;
    GPS*        _gps;
    UIManager*  _ui;
    TLEFetcher  _tle;

    // Per-satellite computed state
    struct SatState {
        double elDeg;
        double azDeg;
        double rangeDotKmps;
        bool   hasTLE;
        bool   visible;
    } _state[N_SATS];

    int  _sortedIdx[N_SATS]; // indices into SATS[], ordered best→worst elevation
    int  _activeSat = -1;    // SATS[] index currently tuned (-1 = none)
    bool _rxActive  = false;
    bool _autoRx    = true;  // auto-select best visible sat

    // List cursor / scroll
    int _listSel = 0;
    int _listOff = 0;

    // Packet ring buffer (newest = _pkts[(_pktHead-1) % BUF])
    TGSPacket _pkts[TINYGS_PKT_BUF];
    int       _pktHead  = 0;
    int       _pktCount = 0;

    enum View { VIEW_LIST, VIEW_PKT, VIEW_FETCH } _view = VIEW_LIST;

    // WiFi / TLE fetch state
    char _fetchStatus[48] = "";
    int  _fetchPct        = 0;
    bool _loadedFallback  = true;
    bool _fetching        = false;

    // Timing
    uint32_t _lastCalcMs    = 0;
    uint32_t _lastDopplerMs = 0;

    // ---- Position & RX ----
    void _calcAllPositions();
    void _sortByElevation();
    int  _bestVisibleSat() const;    // SATS[] index or -1
    void _startRx(int satIdx);
    void _stopRx();
    void _updateDoppler();
    void _pollRx();
    void _addPacket(const RxPacket& raw, int satIdx);
    float _dopplerMHz(int satIdx) const;

    // ---- TLE / WiFi ----
    void _loadFromNVS();
    void _doWiFiFetch();

    // ---- Julian date (for proper SGP4 deltaT from GPS time) ----
    static double _jd(int y, int m, int d, int h, int min, int s);
    static double _jdFromTLEEpoch(const TLEEntry& tle);
    double        _deltaTSec(const TLEEntry& tle) const;

    // ---- Drawing ----
    void _drawAll();
    void _drawHeader();
    void _drawList();
    void _drawSatRow(int satIdx, bool sel, int y);
    void _drawPktMini(int y0, int h);
    void _drawFullPkt();
    void _drawFetchProgress();
};
