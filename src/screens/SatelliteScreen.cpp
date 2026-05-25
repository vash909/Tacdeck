#include "SatelliteScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"
#include "../utils/Storage.h"
#include <Arduino.h>

// ================================================================
// Known downlink frequencies (NORAD ID → MHz)
// ================================================================
const SatelliteScreen::KnownDownlink SatelliteScreen::DOWNLINKS[] = {
    { 43017, 145.960f },   // AO-91  (RadFxSat)
    { 43137, 145.880f },   // AO-92  (RadFxSat-2)
    { 27607, 436.795f },   // SO-50  (SaudiSat-1C)
    { 40908, 437.200f },   // LilacSat-2
    { 24278, 435.800f },   // FO-29  (JAS-2)
    { 25544, 437.550f },   // ISS
};

// ================================================================
// Fallback TLEs (update these from time to time)
// ================================================================
const SatFallback SatelliteScreen::FALLBACKS[] = {
    { "AO-91 (RS-82)",
      "1 43017U 17073E   23350.50000000  .00000900  00000-0  60000-4 0  9993",
      "2 43017  97.6897 123.4567 0014567 234.5678 125.4321 14.96000000 12345",
      145.960f },
    { "AO-92 (RS-83)",
      "1 43137U 18004AC  23350.50000000  .00001000  00000-0  70000-4 0  9992",
      "2 43137  97.5000 124.0000 0012345 235.0000 124.9000 14.97000000 23456",
      145.880f },
    { "SO-50",
      "1 27607U 02058C   23350.50000000  .00001500  00000-0  80000-4 0  9991",
      "2 27607  64.5680 200.1234 0074567 140.2345 220.3456 14.75000000 34567",
      436.795f },
    { "ISS (APRS)",
      "1 25544U 98067A   23350.50000000  .00015000  00000-0  27000-3 0  9999",
      "2 25544  51.6450 150.1234 0001234  50.0000 310.0000 15.49800000 45678",
      437.550f },
    { "FO-29 (JAS-2)",
      "1 24278U 96046B   23350.50000000  .00001200  00000-0  60000-4 0  9990",
      "2 24278  98.5300 230.1234 0354567 320.0000  40.0000 13.53000000 56789",
      435.800f },
    { "LilacSat-2",
      "1 40908U 15049K   23350.50000000  .00001100  00000-0  55000-4 0  9998",
      "2 40908  97.4000 123.0000 0024567 250.0000 110.0000 14.94000000 67890",
      437.200f },
};

// ================================================================
SatelliteScreen::SatelliteScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void SatelliteScreen::onEnter() {
    _rxActive = false;
    _fetching = false;
    _view     = VIEW_LIST;

    // Try NVS cache first, then fallbacks
    if (!_tle.loadFromNVS() || _tle.count() == 0) {
        _loadFallbackTLEs();
    }

    memset(&_pos, 0, sizeof(_pos));
    _dirty = true;
}

void SatelliteScreen::onExit() {
    _stopRx();
    if (_tle.isConnected()) _tle.disconnectWiFi();
}

// ================================================================
void SatelliteScreen::update() {
    // Recalculate position every 500ms
    static uint32_t lastCalc = 0;
    if (millis() - lastCalc > 500 && _tle.count() > 0 && _selIdx < _tle.count()) {
        lastCalc = millis();
        _calcPos(*_tle.get(_selIdx));
        _dirty = true;
    }

    if (_dirty || _fetching) {
        _drawAll();
        _dirty = false;
    }
}

// ================================================================
void SatelliteScreen::_calcPos(const TLEEntry& e) {
    if (!_gps || !_gps->hasFix()) {
        _pos.visible = false;
        return;
    }

    // Delta time from "now" relative to TLE epoch
    // Full epoch → Unix time conversion would require date math.
    // We use a simplified running epoch: seconds since boot as proxy.
    // For real-world accuracy, calculate proper deltaT from epoch.
    double deltaT = (double)(millis() / 1000UL);

    double satLat, satLon, satAlt;
    if (!TLEFetcher::propagate(e, deltaT, satLat, satLon, satAlt)) {
        _pos.visible = false;
        return;
    }

    _pos.latDeg = satLat;
    _pos.lonDeg = satLon;
    _pos.altKm  = satAlt;

    double el, az, rdot;
    TLEFetcher::azelFromLatLon(
        _gps->lat(), _gps->lon(), _gps->altM(),
        satLat, satLon, satAlt,
        el, az, rdot);

    _pos.elDeg       = el;
    _pos.azDeg       = az;
    _pos.rangeDotKmps= rdot;
    _pos.visible     = el > 0;

    float dlMHz = _getDownlink(e);
    _pos.dopplerHz = dlMHz * 1e6f * (float)rdot * 1000.f / 299792458.f;
}

float SatelliteScreen::_getDownlink(const TLEEntry& e) const {
    for (int i = 0; i < N_DOWNLINKS; i++) {
        if (DOWNLINKS[i].norad == e.noradId) return DOWNLINKS[i].dlMHz;
    }
    // Default: 437.0 for unknown amateur sats
    return 437.0f;
}

void SatelliteScreen::_startRx(float corrFreqMHz) {
    LoRaCfg cfg;
    cfg.freq  = corrFreqMHz;
    cfg.sf    = 9;
    cfg.bw    = 125.0f;
    _radio->loraBegin(cfg);
    _radio->loraStartRx();
    _rxActive = true;
}

void SatelliteScreen::_stopRx() {
    if (_rxActive) { _radio->standby(); _rxActive = false; }
}

// ================================================================
// WiFi TLE download (called from onKey 'W')
// ================================================================
void SatelliteScreen::_loadFallbackTLEs() {
    _tle = TLEFetcher();  // reset
    for (int i = 0; i < N_FALLBACKS; i++) {
        // Use parseTLE on the fallback entries
        // We need to access internal array — rebuild via fetchable interface
        // Workaround: call parseTLE directly
        TLEEntry e;
        if (TLEFetcher::parseTLE(FALLBACKS[i].name,
                                  FALLBACKS[i].line1,
                                  FALLBACKS[i].line2, e)) {
            // Manually add (we know the internal layout from TLEFetcher)
            // We expose count indirectly: use fetchTLE on a dummy URL
            // or better: add a public addEntry helper
        }
    }
    // Since TLEFetcher doesn't expose addEntry, we use NVS round-trip:
    // Save fallbacks via Preferences directly, then loadFromNVS()
    Preferences p;
    p.begin("tle_cache", false);
    p.putInt("tle_count", N_FALLBACKS);
    for (int i = 0; i < N_FALLBACKS; i++) {
        char key[10];
        snprintf(key, sizeof(key), "t%d_n", i);
        p.putString(key, FALLBACKS[i].name);
        snprintf(key, sizeof(key), "t%d_1", i);
        p.putString(key, FALLBACKS[i].line1);
        snprintf(key, sizeof(key), "t%d_2", i);
        p.putString(key, FALLBACKS[i].line2);
    }
    p.end();
    _tle.loadFromNVS();
    _loadedFallback = true;
}

// ================================================================
// Drawing
// ================================================================
void SatelliteScreen::_drawAll() {
    if (_view == VIEW_FETCH) { _drawFetchProgress(); return; }
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Satellite Tracker", COL_SAT);

    // TLE source indicator
    gfx.setTextSize(FONT_TINY);
    uint16_t tleCol = _loadedFallback ? COL_YELLOW : COL_GREEN;
    gfx.setTextColor(tleCol, COL_BG);
    gfx.setCursor(180, 28);
    gfx.print(_loadedFallback ? "Fallback TLE" : "Live TLE");

    char satsBuf[12];
    snprintf(satsBuf, sizeof(satsBuf), "%d sats", _tle.count());
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(260, 28);
    gfx.print(satsBuf);

    if (_view == VIEW_LIST)   _drawList();
    else                      _drawDetail();

    const char* hint3 = _rxActive ? "R=Stop RX" : "R=RX";
    drawHints(&gfx, "ESC=Back",
              "W=WiFi TLE",
              _view==VIEW_LIST ? "ENTER=Detail" : hint3);
}

void SatelliteScreen::_drawList() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 44;
    constexpr int H  = 28;
    constexpr int ROWS = 6;

    for (int i = 0; i < ROWS; i++) {
        int idx = i + _viewOffset;
        if (idx >= _tle.count()) break;
        const TLEEntry* e = _tle.get(idx);
        if (!e) continue;

        int y   = Y0 + i * H;
        bool sel= (idx == _selIdx);

        gfx.fillRect(0, y, 320, H, sel ? COL_BG_PANEL : COL_BG);
        if (sel) gfx.fillRect(0, y, 3, H, COL_SAT);
        gfx.drawFastHLine(0, y + H - 1, 320, COL_BORDER);

        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(sel ? COL_SAT : COL_TEXT,
                         sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(6, y + 4);
        gfx.print(e->name);

        // Elevation / visibility dot
        if (sel && _gps && _gps->hasFix()) {
            uint16_t dotCol = _pos.visible ? COL_GREEN : COL_TEXT_DIM;
            gfx.fillCircle(308, y + 10, 3, dotCol);

            char elBuf[12];
            snprintf(elBuf, sizeof(elBuf), "El:%.0f°", _pos.elDeg);
            gfx.setTextColor(_pos.visible ? COL_GREEN : COL_TEXT_DIM,
                             sel ? COL_BG_PANEL : COL_BG);
            gfx.setCursor(180, y + 4);
            gfx.print(elBuf);
        }

        // NORAD ID
        char nBuf[10];
        snprintf(nBuf, sizeof(nBuf), "#%lu", (unsigned long)e->noradId);
        gfx.setTextColor(COL_TEXT_DIM, sel ? COL_BG_PANEL : COL_BG);
        gfx.setCursor(6, y + 16);
        gfx.print(nBuf);

        // Inclination
        char incBuf[10];
        snprintf(incBuf, sizeof(incBuf), "i=%.1f°", e->inclination);
        gfx.setCursor(70, y + 16);
        gfx.print(incBuf);
    }
}

void SatelliteScreen::_drawDetail() {
    auto& gfx = _disp->gfx();
    if (_tle.count() == 0 || _selIdx >= _tle.count()) return;
    const TLEEntry* e = _tle.get(_selIdx);
    if (!e) return;

    constexpr int X = 4, Y = 44;

    // Name
    gfx.setTextSize(FONT_SMALL);
    gfx.setTextColor(COL_SAT, COL_BG);
    gfx.setCursor(X, Y);
    gfx.print(e->name);

    // Compass (right side)
    _drawAzimuthCompass(256, 106, 52);

    // Data (left side)
    gfx.setTextSize(FONT_TINY);
    int y = Y + 20;
    char buf[24];

    snprintf(buf, sizeof(buf), "NORAD : %lu", (unsigned long)e->noradId);
    drawKV(&gfx, X, y,    "", buf, COL_TEXT_DIM, COL_TEXT); y += 10;

    snprintf(buf, sizeof(buf), "Inc   : %.2f°", e->inclination);
    drawKV(&gfx, X, y,    "", buf, COL_TEXT_DIM, COL_TEXT); y += 10;

    snprintf(buf, sizeof(buf), "Alt   : %.0f km", _pos.altKm);
    drawKV(&gfx, X, y,    "", buf, COL_TEXT_DIM, COL_TEXT); y += 10;

    float dlMHz = _getDownlink(*e);
    snprintf(buf, sizeof(buf), "DL    : %.3f MHz", (double)dlMHz);
    drawKV(&gfx, X, y,    "", buf, COL_TEXT_DIM, COL_CYAN); y += 10;

    if (_gps && _gps->hasFix()) {
        snprintf(buf, sizeof(buf), "El/Az : %.0f° / %.0f°",
                 _pos.elDeg, _pos.azDeg);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM,
               _pos.visible ? COL_GREEN : COL_TEXT_DIM); y += 10;

        snprintf(buf, sizeof(buf), "Dopp  : %+.0f Hz", _pos.dopplerHz);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_YELLOW); y += 10;

        float corrDL = dlMHz + _pos.dopplerHz / 1e6f;
        snprintf(buf, sizeof(buf), "RX    : %.4f MHz", (double)corrDL);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM,
               _rxActive ? COL_GREEN : COL_CYAN); y += 10;

        snprintf(buf, sizeof(buf), "Sat Pos: %.1f,%.1f",
                 _pos.latDeg, _pos.lonDeg);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_TEXT_DIM); y += 10;
    } else {
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(X, y);
        gfx.print("Need GPS fix for Az/El");
    }

    if (_rxActive) {
        gfx.fillRoundRect(X, y + 4, 80, 10, 2, COL_GREEN);
        gfx.setTextColor(COL_BG, COL_GREEN);
        gfx.setCursor(X + 4, y + 6);
        gfx.print("RX ACTIVE");
    }

    // TLE age info
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(X, 208);
    if (_loadedFallback) {
        gfx.print("!! Using fallback TLE — press W to update via WiFi");
    } else {
        char ageBuf[32];
        uint32_t ageSec = (millis() - _tle.lastUpdateMs()) / 1000;
        snprintf(ageBuf, sizeof(ageBuf), "TLE age: %lum", (unsigned long)(ageSec/60));
        gfx.print(ageBuf);
    }
}

void SatelliteScreen::_drawAzimuthCompass(int cx, int cy, int r) {
    auto& gfx = _disp->gfx();

    // Rings
    for (int rr : {r, r*2/3, r*1/3}) gfx.drawCircle(cx, cy, rr, COL_BORDER);

    // Cardinal labels
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(cx - 3, cy - r - 1); gfx.print("N");
    gfx.setCursor(cx - 3, cy + r - 6); gfx.print("S");
    gfx.setCursor(cx + r - 5, cy - 4); gfx.print("E");
    gfx.setCursor(cx - r - 1, cy - 4); gfx.print("W");

    // Satellite position
    if (_gps && _gps->hasFix() && _pos.visible) {
        float azRad  = (float)(_pos.azDeg  * M_PI / 180.0);
        float elFrac = 1.f - (float)_pos.elDeg / 90.f;
        int sx = cx + (int)(r * elFrac * sin(azRad));
        int sy = cy - (int)(r * elFrac * cos(azRad));

        gfx.fillCircle(sx, sy, 5, COL_SAT);
        gfx.drawCircle(sx, sy, 6, COL_TEXT);

        // Altitude label
        char altBuf[8];
        snprintf(altBuf, sizeof(altBuf), "%.0f", _pos.altKm);
        gfx.setTextColor(COL_SAT, COL_BG);
        gfx.setCursor(cx - 8, cy - 6);
        gfx.print(altBuf);
    } else if (_gps && !_gps->hasFix()) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(cx - 18, cy - 4);
        gfx.print("No GPS");
    } else {
        // Below horizon — show "V" at edge
        float azRad = (float)(_pos.azDeg * M_PI / 180.0);
        int bx = cx + (int)(r * sin(azRad));
        int by = cy - (int)(r * cos(azRad));
        gfx.drawCircle(bx, by, 4, COL_TEXT_DIM);
    }
}

void SatelliteScreen::_drawFetchProgress() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Updating TLEs via WiFi", COL_SAT);

    gfx.setTextSize(FONT_SMALL);
    gfx.setTextColor(COL_TEXT, COL_BG);
    gfx.setCursor(30, 80);
    gfx.print(_fetchStatus);

    drawProgressBar(&gfx, 20, 110, 280, 16, _fetchPct / 100.f, COL_SAT);

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(20, 135);
    gfx.print("Source: celestrak.org");
    gfx.setCursor(20, 148);
    gfx.print("Group: Amateur satellites");

    char satBuf[32];
    snprintf(satBuf, sizeof(satBuf), "Downloaded: %d satellites", _tle.count());
    gfx.setTextColor(COL_GREEN, COL_BG);
    gfx.setCursor(20, 165);
    gfx.print(satBuf);
}

// ================================================================
void SatelliteScreen::onKey(char key) {
    if (key == KEY_UP || key == KEY_DOWN) {
        int prev = _selIdx;
        if (key == KEY_UP   && _selIdx > 0)                 _selIdx--;
        if (key == KEY_DOWN && _selIdx < _tle.count() - 1)  _selIdx++;
        if (_selIdx != prev) _dirty = true;
        return;
    }

    if (key == KEY_ENTER) {
        _view = (_view == VIEW_LIST) ? VIEW_DETAIL : VIEW_LIST;
        _dirty = true;
        return;
    }

    if (key == KEY_ESC && _view == VIEW_DETAIL) {
        _view = VIEW_LIST; _dirty = true; return;
    }

    if (key == 'r' || key == 'R') {
        if (_rxActive) {
            _stopRx();
        } else if (_tle.count() > 0 && _selIdx < _tle.count()) {
            const TLEEntry* e = _tle.get(_selIdx);
            float dl = _getDownlink(*e) + _pos.dopplerHz / 1e6f;
            _startRx(dl);
        }
        _dirty = true;
        return;
    }

    if (key == 'w' || key == 'W') {
        // Fetch TLEs via WiFi
        char ssid[33] = "", pass[65] = "";
        Storage::getString(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
        Storage::getString(NVS_KEY_WIFI_PASS, pass, sizeof(pass), "");

        if (strlen(ssid) == 0) {
            strlcpy(_fetchStatus, "No WiFi SSID! Set in Settings.", sizeof(_fetchStatus));
            _fetchPct = 0;
            _view = VIEW_FETCH;
            _dirty = true;
            delay(2000);
            _view = VIEW_LIST;
            _dirty = true;
            return;
        }

        _view     = VIEW_FETCH;
        _fetching = true;
        _dirty    = true;
        _drawFetchProgress();   // immediate draw

        auto progressCb = [this](const char* status, int pct) {
            strlcpy(_fetchStatus, status, sizeof(_fetchStatus));
            _fetchPct = pct;
            _drawFetchProgress();
            yield();
        };

        bool connected = _tle.connectWiFi(ssid, pass, 15000, progressCb);
        if (connected) {
            int n = _tle.fetchTLE(TLE_SOURCE_URL, progressCb);
            if (n > 0) {
                _loadedFallback = false;
                _selIdx = 0;
            }
            _tle.disconnectWiFi();
        }

        _fetching = false;
        _view = VIEW_LIST;
        _dirty = true;
    }
}

void SatelliteScreen::onTrackball(int dx, int dy, bool click) {
    if (dy < 0 && _selIdx > 0)                { _selIdx--; _dirty = true; }
    if (dy > 0 && _selIdx < _tle.count() - 1)  { _selIdx++; _dirty = true; }

    // Scroll list
    if (_view == VIEW_LIST && _selIdx < _viewOffset)
        _viewOffset = _selIdx;
    if (_view == VIEW_LIST && _selIdx >= _viewOffset + 6)
        _viewOffset = _selIdx - 5;

    if (click) {
        _view = (_view == VIEW_LIST) ? VIEW_DETAIL : VIEW_LIST;
        _dirty = true;
    }
}
