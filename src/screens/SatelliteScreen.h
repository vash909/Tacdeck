#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"
#include "../utils/TLEFetcher.h"
#include <cmath>

class Display; class GPS; class UIManager;

// ================================================================
// Satellite Tracker
// • Real TLE via WiFi download from CelesTrak
// • SGP4-lite propagation (TLEFetcher::propagate)
// • Doppler-corrected LoRa/FSK RX
// • Azimuth/elevation compass
// • Falls back to hard-coded TLE if no WiFi
// ================================================================

// Fallback hard-coded TLE entries (updated at compile time)
struct SatFallback {
    const char* name;
    const char* line1;
    const char* line2;
    float       downlinkMHz;
};

class SatelliteScreen : public Screen {
public:
    SatelliteScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Satellite"; }
    bool handlesEsc() const override { return true; }

private:
    Display*    _disp;
    Radio*      _radio;
    GPS*        _gps;
    UIManager*  _ui;
    TLEFetcher  _tle;

    int     _selIdx   = 0;
    bool    _rxActive = false;
    bool    _fetching = false;

    // Current satellite position (recomputed every 500ms)
    struct SatPos {
        double elDeg;
        double azDeg;
        double latDeg;
        double lonDeg;
        double altKm;
        double rangeDotKmps;
        bool   visible;
        float  dopplerHz;    // Doppler shift relative to DL (Hz)
        float  dopplerUlHz;  // Doppler correction for UL TX (Hz)
    } _pos;

    // Frequency table for known satellites (matched by NORAD ID)
    struct KnownDownlink {
        uint32_t    norad;
        float       dlMHz;  // downlink (receive)
        float       ulMHz;  // uplink (transmit), 0 = beacon-only
        const char* mode;   // e.g. "V/U FM", "U/V SSB"
    };
    static const KnownDownlink DOWNLINKS[];
    static constexpr int       N_DOWNLINKS = 15;

    const KnownDownlink* _lookupSat(const TLEEntry& e) const;
    float _getDownlink(const TLEEntry& e) const;
    void  _calcPos(const TLEEntry& e);
    void  _startRx(float corrFreqMHz);
    void  _stopRx();

    // Views
    enum View { VIEW_LIST, VIEW_DETAIL, VIEW_FETCH } _view = VIEW_LIST;
    int  _viewOffset = 0;

    void _drawAll();
    void _drawList();
    void _drawDetail();
    void _drawFetchProgress();
    void _drawAzimuthCompass(int cx, int cy, int r);
    void _refreshLivePos();   // partial position update (no full-screen clear)

    char  _fetchStatus[48] = "";
    int   _fetchPct = 0;

    // Fallback TLEs (hardcoded, compile-time)
    static const SatFallback FALLBACKS[];
    static constexpr int     N_FALLBACKS = 9;
    bool _loadedFallback = false;
    void _loadFallbackTLEs();
};
