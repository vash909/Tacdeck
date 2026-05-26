#include "SatelliteScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"
#include "../utils/Storage.h"
#include <Arduino.h>

// ================================================================
// Known satellite frequencies (NORAD ID → DL / UL MHz, mode)
// ================================================================
const SatelliteScreen::KnownDownlink SatelliteScreen::DOWNLINKS[] = {
    // { norad,  dlMHz,    ulMHz,    mode          }
    { 43017, 145.960f, 435.250f, "V/U FM"      },  // AO-91  (RadFxSat)
    { 43137, 145.880f, 435.350f, "V/U FM"      },  // AO-92  (RadFxSat-2)
    { 43770, 145.920f, 435.300f, "V/U FM"      },  // AO-95  (Fox-1Cliff)
    { 27607, 436.795f, 145.850f, "U/V FM"      },  // SO-50  (SaudiSat-1C)
    { 24278, 435.800f, 145.900f, "U/V SSB"     },  // FO-29  (JAS-2)
    { 44909, 435.640f, 145.935f, "U/V SSB"     },  // RS-44  (DOSAAF-85)
    { 39444, 145.935f, 435.150f, "V/U SSB"     },  // AO-73  (FUNcube-1)
    { 42017, 145.940f, 435.045f, "V/U SSB"     },  // EO-88  (Nayif-1)
    { 43678, 145.900f, 437.500f, "V/U FM"      },  // PO-101 (Diwata-2B)
    { 57166, 435.310f, 435.310f, "U Digipeat"  },  // IO-117 (GREENCUBE)
    { 40908, 437.200f, 144.350f, "U/V FM"      },  // LilacSat-2
    {  7530, 145.975f, 432.125f, "V/U SSB"     },  // AO-7
    { 51069, 436.400f, 145.970f, "U/V FM"      },  // TEVEL-2
    { 51063, 436.400f, 145.970f, "U/V FM"      },  // TEVEL-4
    { 25544, 145.800f,       0.f, "V Beacon"   },  // ISS (APRS/SSTV)
};

// ================================================================
// Fallback TLEs — approximate orbital elements, fetch via WiFi for accuracy
// ================================================================
const SatFallback SatelliteScreen::FALLBACKS[] = {
    { "AO-91 (RadFxSat)",
      "1 43017U 17073E   24001.50000000  .00000900  00000-0  60000-4 0  9993",
      "2 43017  97.6897 123.4567 0014567 234.5678 125.4321 14.96000000 12340",
      145.960f },
    { "AO-92 (RadFxSat-2)",
      "1 43137U 18004AC  24001.50000000  .00001000  00000-0  70000-4 0  9992",
      "2 43137  97.5000 124.0000 0012345 235.0000 124.9000 14.97000000 23450",
      145.880f },
    { "AO-95 (Fox-1Cliff)",
      "1 43770U 18111AJ  24001.50000000  .00001100  00000-0  50000-4 0  9991",
      "2 43770  97.4000 140.0000 0011234 190.0000 170.0000 14.89000000 40010",
      145.920f },
    { "SO-50",
      "1 27607U 02058C   24001.50000000  .00001500  00000-0  80000-4 0  9990",
      "2 27607  64.5680 200.1234 0074567 140.2345 220.3456 14.75000000 34570",
      436.795f },
    { "RS-44 (DOSAAF-85)",
      "1 44909U 19096E   24001.50000000  .00000800  00000-0  35000-4 0  9994",
      "2 44909  82.5000 120.0000 0006789 270.0000  90.0000 12.79000000 20010",
      435.640f },
    { "AO-73 (FUNcube-1)",
      "1 39444U 13066AE  24001.50000000  .00001200  00000-0  50000-4 0  9995",
      "2 39444  97.8000 150.0000 0012345 200.0000 160.0000 14.81000000 10010",
      145.935f },
    { "ISS",
      "1 25544U 98067A   24001.50000000  .00015000  00000-0  27000-3 0  9999",
      "2 25544  51.6450 150.1234 0001234  50.0000 310.0000 15.49800000 45670",
      145.800f },
    { "FO-29 (JAS-2)",
      "1 24278U 96046B   24001.50000000  .00001200  00000-0  60000-4 0  9989",
      "2 24278  98.5300 230.1234 0354567 320.0000  40.0000 13.53000000 56780",
      435.800f },
    { "IO-117 (GREENCUBE)",
      "1 57166U 23057A   24001.50000000  .00000500  00000-0  25000-4 0  9997",
      "2 57166  97.5000 160.0000 0008000 230.0000 130.0000 15.09000000 15010",
      435.310f },
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
// Partial update for live position data — avoids the full 216-px screen
// clear that was triggering a visible 2 Hz flicker when _dirty was set
// on every position recalculation.
// ================================================================
void SatelliteScreen::_refreshLivePos() {
    if (_view == VIEW_LIST) {
        // Only redraw the visible list rows — per-row fills (28 px each) are
        // much less visible than a single 216-px clear.
        _drawList();
    } else if (_view == VIEW_DETAIL) {
        // _drawDetail() uses drawKV() / setTextColor(fg,bg) for each line so
        // it does not need a preceding fillRect.  The compass sub-region must
        // be cleared explicitly before redraw to erase the old dot.
        constexpr int CX = 256, CY = 106, R = 52;
        _disp->gfx().fillRect(CX - R - 6, CY - R - 6,
                               2*(R + 6), 2*(R + 6), COL_BG);
        _drawDetail();
    }
}

void SatelliteScreen::update() {
    // Recalculate position every 500ms and do a PARTIAL update (no full-screen
    // clear) to avoid the 2 Hz flicker that the old _dirty=true approach caused.
    static uint32_t lastCalc = 0;
    if (millis() - lastCalc > 500 && _tle.count() > 0 && _selIdx < _tle.count()) {
        lastCalc = millis();
        _calcPos(*_tle.get(_selIdx));
        if (!_dirty) _refreshLivePos();
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

    const KnownDownlink* kd = _lookupSat(e);
    float dlMHz = kd ? kd->dlMHz : 437.0f;
    float ulMHz = kd ? kd->ulMHz : 0.0f;

    // Doppler: dopplerHz > 0 when satellite moves away (rdot > 0)
    _pos.dopplerHz   = dlMHz * 1e6f * (float)rdot * 1000.f / 299792458.f;
    _pos.dopplerUlHz = ulMHz > 0.f
                       ? ulMHz * 1e6f * (float)rdot * 1000.f / 299792458.f
                       : 0.f;
}

const SatelliteScreen::KnownDownlink* SatelliteScreen::_lookupSat(const TLEEntry& e) const {
    for (int i = 0; i < N_DOWNLINKS; i++) {
        if (DOWNLINKS[i].norad == e.noradId) return &DOWNLINKS[i];
    }
    return nullptr;
}

float SatelliteScreen::_getDownlink(const TLEEntry& e) const {
    const KnownDownlink* kd = _lookupSat(e);
    return kd ? kd->dlMHz : 437.0f;
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
    drawHints(&gfx, "HOLD=Back",
              "W=WiFi TLE",
              _view==VIEW_LIST ? "ENTER=Detail" : hint3);
}

void SatelliteScreen::_drawList() {
    auto& gfx = _disp->gfx();
    constexpr int Y0 = 44;
    constexpr int H  = 28;
    constexpr int ROWS = 6;

    int maxOffset = _tle.count() - ROWS;
    if (maxOffset < 0) maxOffset = 0;
    if (_viewOffset > maxOffset) _viewOffset = maxOffset;
    if (_viewOffset < 0) _viewOffset = 0;

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

    // Compass (right side, cx=256 cy=120 r=48 avoids overlap with 10 data rows)
    _drawAzimuthCompass(256, 120, 48);

    // Data (left side, max x ~ 195 to stay clear of compass)
    gfx.setTextSize(FONT_TINY);
    int y = Y + 20;
    char buf[28];

    // NORAD + mode on one row
    const KnownDownlink* kd = _lookupSat(*e);
    char noradBuf[24];
    snprintf(noradBuf, sizeof(noradBuf), "#%lu  %s",
             (unsigned long)e->noradId,
             kd ? kd->mode : "?");
    drawKV(&gfx, X, y, "", noradBuf, COL_TEXT_DIM, COL_TEXT); y += 10;

    snprintf(buf, sizeof(buf), "Inc %.2f°  Alt %.0f km",
             e->inclination, _pos.altKm);
    drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_TEXT); y += 10;

    float dlMHz = kd ? kd->dlMHz : 437.0f;
    float ulMHz = kd ? kd->ulMHz : 0.0f;

    snprintf(buf, sizeof(buf), "DL  %.3f MHz", (double)dlMHz);
    drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_CYAN); y += 10;

    if (ulMHz > 0.f) {
        snprintf(buf, sizeof(buf), "UL  %.3f MHz", (double)ulMHz);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_YELLOW); y += 10;
    } else {
        drawKV(&gfx, X, y, "", "UL  --  beacon only", COL_TEXT_DIM, COL_TEXT_DIM); y += 10;
    }

    if (_gps && _gps->hasFix()) {
        snprintf(buf, sizeof(buf), "El %.0f°  Az %.0f°",
                 _pos.elDeg, _pos.azDeg);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM,
               _pos.visible ? COL_GREEN : COL_TEXT_DIM); y += 10;

        // Doppler-corrected RX frequency (DL: tune lower when moving away)
        float corrDL = dlMHz + _pos.dopplerHz / 1e6f;
        snprintf(buf, sizeof(buf), "RX  %.4f MHz  %+.0fHz",
                 (double)corrDL, (double)_pos.dopplerHz);
        drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM,
               _rxActive ? COL_GREEN : COL_CYAN); y += 10;

        // Doppler-corrected TX frequency (UL: transmit higher when moving away)
        if (ulMHz > 0.f) {
            float corrUL = ulMHz - _pos.dopplerUlHz / 1e6f;
            snprintf(buf, sizeof(buf), "TX  %.4f MHz  %+.0fHz",
                     (double)corrUL, -(double)_pos.dopplerUlHz);
            drawKV(&gfx, X, y, "", buf, COL_TEXT_DIM, COL_ORANGE); y += 10;
        } else {
            y += 10;
        }
    } else {
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(X, y);
        gfx.print("Need GPS fix for Az/El/Doppler");
        y += 10;
    }

    if (_rxActive) {
        gfx.fillRoundRect(X, y + 2, 76, 10, 2, COL_GREEN);
        gfx.setTextColor(COL_BG, COL_GREEN);
        gfx.setCursor(X + 4, y + 4);
        gfx.print("RX ACTIVE");
    }

    // TLE source / age
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(X, 208);
    if (_loadedFallback) {
        gfx.print("Fallback TLE — press W to update via WiFi");
    } else {
        char ageBuf[32];
        uint32_t ageSec = (millis() - _tle.lastUpdateMs()) / 1000;
        snprintf(ageBuf, sizeof(ageBuf), "TLE age: %lum", (unsigned long)(ageSec / 60));
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
        if (_selIdx != prev) {
            if (_view == VIEW_LIST && _selIdx < _viewOffset)
                _viewOffset = _selIdx;
            if (_view == VIEW_LIST && _selIdx >= _viewOffset + 6)
                _viewOffset = _selIdx - 5;
            _dirty = true;
        }
        return;
    }

    if (key == KEY_ENTER) {
        _view = (_view == VIEW_LIST) ? VIEW_DETAIL : VIEW_LIST;
        _dirty = true;
        return;
    }

    if (key == KEY_ESC) {
        if (_view == VIEW_DETAIL) {
            _view = VIEW_LIST;
            _dirty = true;
        } else if (_view != VIEW_FETCH) {
            _ui->pop();
        }
        return;
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
