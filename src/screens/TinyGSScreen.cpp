#include "TinyGSScreen.h"
#include "../hardware/Display.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>
#include <Preferences.h>

// ================================================================
// TinyGS-compatible satellite database — 430-440 MHz, LoRa only
//
// Sources: TinyGS open database (github.com/G4lile0/tinyGS)
//          SatNOGS DB (db.satnogs.org)
// NORAD IDs are best-guess from training data — update via WiFi TLE.
// All use sync word 0x12 (RadioLib private / TinyGS default).
// ================================================================
const TGSSat TinyGSScreen::SATS[] = {
    // norad    name             MHz       SF  BW      CR  SW    pre
    { 44829, "FossaSat-1",   436.703f,  11, 125.0f,  5, 0x12,  8 },  // Dec 2019
    { 47960, "FossaSat-2E1", 437.190f,  11, 125.0f,  5, 0x12,  8 },  // Jun 2021
    { 47961, "FossaSat-2E2", 437.196f,  11, 125.0f,  5, 0x12,  8 },  // Jun 2021
    { 47966, "FEES",         435.600f,  10, 250.0f,  5, 0x12,  8 },  // Jun 2021
    { 48903, "NORBI",        436.504f,  11, 125.0f,  5, 0x12,  8 },  // ~2021
    { 51019, "EASAT-2",      437.733f,  12, 125.0f,  5, 0x12,  8 },  // Jan 2022
    { 51044, "SATLLA-2B",    436.405f,  11, 125.0f,  5, 0x12,  8 },  // 2022
    { 52763, "MESAT-1",      437.100f,  10, 250.0f,  5, 0x12,  8 },  // 2022
    { 54873, "HADES-D",      436.888f,  12, 125.0f,  5, 0x12,  8 },  // ~2023
};

// ================================================================
TinyGSScreen::TinyGSScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
    : _disp(d), _radio(r), _gps(g), _ui(ui)
{
    for (int i = 0; i < N_SATS; i++) _sortedIdx[i] = i;
    memset(_state, 0, sizeof(_state));
    memset(_pkts,  0, sizeof(_pkts));
}

void TinyGSScreen::onEnter() {
    _rxActive  = false;
    _autoRx    = true;
    _activeSat = -1;
    _pktHead   = 0;
    _pktCount  = 0;
    _listSel   = 0;
    _listOff   = 0;
    _view      = VIEW_LIST;
    for (int i = 0; i < N_SATS; i++) _sortedIdx[i] = i;
    memset(_state, 0, sizeof(_state));
    _loadFromNVS();
    _dirty = true;
}

void TinyGSScreen::onExit() {
    _stopRx();
}

// ================================================================
// Julian date — needed for accurate SGP4 deltaT when GPS time is
// available (fixes the "millis() as epoch proxy" bug from SatelliteScreen).
// ================================================================
double TinyGSScreen::_jd(int y, int m, int d, int h, int min, int s) {
    int a  = (14 - m) / 12;
    int y2 = y + 4800 - a;
    int m2 = m + 12 * a - 3;
    long JDN = (long)d + (153L * m2 + 2) / 5
               + 365L * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045L;
    return (double)JDN - 0.5 + h / 24.0 + min / 1440.0 + s / 86400.0;
}

double TinyGSScreen::_jdFromTLEEpoch(const TLEEntry& tle) {
    int y = (int)tle.epochYear;
    // Jan 1.0 of year y is JD(y,1,1,0,0,0). TLE epochDay=1.0 means Jan 1.
    return _jd(y, 1, 1, 0, 0, 0) + tle.epochDay - 1.0;
}

double TinyGSScreen::_deltaTSec(const TLEEntry& tle) const {
    if (_gps && _gps->hasFix() && _gps->data().year >= 2024) {
        const GpsData& g = _gps->data();
        double jd_now   = _jd(g.year, g.month, g.day, g.hour, g.minute, g.second);
        double jd_epoch = _jdFromTLEEpoch(tle);
        return (jd_now - jd_epoch) * 86400.0;
    }
    // Fallback: seconds-since-boot (highly inaccurate without GPS time)
    return (double)(millis() / 1000UL);
}

// ================================================================
float TinyGSScreen::_dopplerMHz(int satIdx) const {
    if (satIdx < 0 || satIdx >= N_SATS || !_state[satIdx].hasTLE) return 0.f;
    // Doppler shift: positive rdot → satellite receding → lower received freq
    float rdot = (float)_state[satIdx].rangeDotKmps;
    return SATS[satIdx].freqMHz * rdot * 1000.f / 299792458.f;
}

// ================================================================
void TinyGSScreen::_calcAllPositions() {
    if (!_gps || !_gps->hasFix()) return;

    for (int i = 0; i < N_SATS; i++) {
        const TLEEntry* e = _tle.findByNorad(SATS[i].norad);
        if (!e || !e->valid) {
            _state[i] = { -90.0, 0.0, 0.0, false, false };
            continue;
        }
        _state[i].hasTLE = true;

        double satLat, satLon, satAlt;
        if (!TLEFetcher::propagate(*e, _deltaTSec(*e), satLat, satLon, satAlt)) {
            _state[i].visible = false;
            _state[i].elDeg   = -90.0;
            continue;
        }

        double el, az, rdot;
        TLEFetcher::azelFromLatLon(
            _gps->lat(), _gps->lon(), _gps->altM(),
            satLat, satLon, satAlt,
            el, az, rdot);

        _state[i].elDeg        = el;
        _state[i].azDeg        = az;
        _state[i].rangeDotKmps = rdot;
        _state[i].visible      = (el > TINYGS_EL_MIN);
    }
}

// Insertion sort: visible sats first, then by descending elevation.
void TinyGSScreen::_sortByElevation() {
    for (int i = 1; i < N_SATS; i++) {
        int key = _sortedIdx[i];
        int j   = i - 1;
        while (j >= 0) {
            int  s  = _sortedIdx[j];
            bool sV = _state[s].visible, kV = _state[key].visible;
            // key should go before s if: key visible & s not, or same visibility & key higher
            bool keyBetter = (!sV && kV) ||
                             (sV == kV && _state[s].elDeg < _state[key].elDeg);
            if (!keyBetter) break;
            _sortedIdx[j + 1] = _sortedIdx[j];
            j--;
        }
        _sortedIdx[j + 1] = key;
    }
}

int TinyGSScreen::_bestVisibleSat() const {
    for (int i = 0; i < N_SATS; i++) {
        int si = _sortedIdx[i];
        if (_state[si].hasTLE && _state[si].visible) return si;
    }
    return -1;
}

// ================================================================
void TinyGSScreen::_startRx(int satIdx) {
    if (satIdx < 0 || satIdx >= N_SATS) return;
    const TGSSat& s = SATS[satIdx];

    LoRaCfg cfg;
    cfg.freq     = s.freqMHz + _dopplerMHz(satIdx);
    cfg.sf       = s.sf;
    cfg.bw       = s.bwKHz;
    cfg.cr       = s.cr;
    cfg.syncWord = s.sw;
    cfg.preamble = s.preamble;
    cfg.power    = 22;
    cfg.crc      = true;

    if (_radio->loraBegin(cfg)) {
        _radio->loraStartRx();
        _activeSat = satIdx;
        _rxActive  = true;
    }
}

void TinyGSScreen::_stopRx() {
    if (_rxActive) {
        _radio->standby();
        _rxActive  = false;
        _activeSat = -1;
    }
}

// Update Doppler correction without full modem reconfiguration.
void TinyGSScreen::_updateDoppler() {
    if (!_rxActive || _activeSat < 0 || !_state[_activeSat].hasTLE) return;
    float corrFreq = SATS[_activeSat].freqMHz + _dopplerMHz(_activeSat);
    _radio->setFrequency(corrFreq);
    _radio->loraStartRx();
}

// ================================================================
void TinyGSScreen::_addPacket(const RxPacket& raw, int satIdx) {
    TGSPacket& p = _pkts[_pktHead % TINYGS_PKT_BUF];
    size_t n = raw.len < sizeof(p.data) ? raw.len : sizeof(p.data);
    memcpy(p.data, raw.data, n);
    p.len    = (uint8_t)n;
    p.rssi   = raw.rssi;
    p.snr    = raw.snr;
    p.tsMs   = millis();
    p.satIdx = (uint8_t)(satIdx >= 0 && satIdx < N_SATS ? satIdx : N_SATS);
    _pktHead++;
    if (_pktCount < TINYGS_PKT_BUF) _pktCount++;
}

void TinyGSScreen::_pollRx() {
    if (!_rxActive) return;
    while (_radio->loraAvailable()) {
        RxPacket raw;
        if (!_radio->loraRead(raw) || !raw.valid) break;
        _addPacket(raw, _activeSat);
        _dirty = true;
    }
}

// ================================================================
void TinyGSScreen::_loadFromNVS() {
    // TLE cache is shared with SatelliteScreen (same NVS namespace).
    // Press W here to fetch a targeted TLE set for these 9 satellites.
    _loadedFallback = (!_tle.loadFromNVS() || _tle.count() == 0);
}

void TinyGSScreen::_doWiFiFetch() {
    char ssid[33] = "", pass[65] = "";
    Storage::getString(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    Storage::getString(NVS_KEY_WIFI_PASS, pass, sizeof(pass), "");

    if (strlen(ssid) == 0) {
        strlcpy(_fetchStatus, "No WiFi SSID — set in Settings", sizeof(_fetchStatus));
        _fetchPct = 0;
        _view = VIEW_FETCH;
        _dirty = true;
        _drawFetchProgress();
        delay(2500);
        _view  = VIEW_LIST;
        _dirty = true;
        return;
    }

    _view     = VIEW_FETCH;
    _fetching = true;
    _dirty    = true;
    _drawFetchProgress();

    auto cb = [this](const char* status, int pct) {
        strlcpy(_fetchStatus, status, sizeof(_fetchStatus));
        _fetchPct = pct;
        _drawFetchProgress();
        yield();
    };

    // Use targeted CATNR URL so all 9 TinyGS sats are fetched
    // (overrides shared TLE cache; re-fetch in Satellite Tracker
    //  to restore full amateur-satellite list there).
    bool ok = _tle.connectWiFi(ssid, pass, 15000, cb);
    if (ok) {
        int n = _tle.fetchTLE(TINYGS_TLE_URL, cb);
        _loadedFallback = (n <= 0);
        _tle.disconnectWiFi();
    }

    _fetching = false;
    _view  = VIEW_LIST;
    _dirty = true;
}

// ================================================================
// Main update loop
// ================================================================
void TinyGSScreen::update() {
    uint32_t now = millis();

    bool needCalc = (now - _lastCalcMs > 500);
    if (needCalc) {
        _lastCalcMs = now;
        _calcAllPositions();
        _sortByElevation();

        if (_autoRx) {
            int best = _bestVisibleSat();
            if (best != _activeSat) {
                _stopRx();
                if (best >= 0) _startRx(best);
                _dirty = true;  // header active-sat chip must update
            } else if (_rxActive && now - _lastDopplerMs > 500) {
                _lastDopplerMs = now;
                _updateDoppler();
            }
        }
    }

    _pollRx();

    if (_dirty) {
        _drawAll();
        _dirty = false;
    } else if (needCalc && _view == VIEW_LIST) {
        _drawList();   // partial refresh — no full-screen clear
    }
}

// ================================================================
// Key handling
// ================================================================
void TinyGSScreen::onKey(char key) {
    if (key == KEY_ESC) {
        if (_view != VIEW_LIST) { _view = VIEW_LIST; _dirty = true; }
        else _ui->pop();
        return;
    }

    if (key == KEY_UP || key == KEY_DOWN) {
        int prev = _listSel;
        if (key == KEY_UP   && _listSel > 0)           _listSel--;
        if (key == KEY_DOWN && _listSel < N_SATS - 1)  _listSel++;
        if (_listSel != prev) {
            if (_listSel < _listOff)       _listOff = _listSel;
            if (_listSel >= _listOff + 5)  _listOff = _listSel - 4;
            _dirty = true;
        }
        return;
    }

    if (key == KEY_TAB || key == '\t') {
        _view = (_view == VIEW_LIST) ? VIEW_PKT : VIEW_LIST;
        _dirty = true;
        return;
    }

    if (key == 'r' || key == 'R') {
        if (_rxActive) {
            _autoRx = false;
            _stopRx();
        } else {
            int si = _sortedIdx[_listSel];
            _autoRx = false;
            _startRx(si);
        }
        _dirty = true;
        return;
    }

    if (key == 'a' || key == 'A') {
        // Re-enable auto-RX
        _autoRx = true;
        _dirty  = true;
        return;
    }

    if (key == 'w' || key == 'W') {
        _doWiFiFetch();
        return;
    }

    if (key == 'c' || key == 'C') {
        _pktCount = 0;
        _pktHead  = 0;
        _dirty    = true;
        return;
    }
}

void TinyGSScreen::onTrackball(int dx, int dy, bool click) {
    if (_view == VIEW_PKT) {
        _view  = VIEW_LIST;
        _dirty = true;
        return;
    }

    if (dy < 0 && _listSel > 0)           { _listSel--; _dirty = true; }
    if (dy > 0 && _listSel < N_SATS - 1)  { _listSel++; _dirty = true; }

    if (_listSel < _listOff)       _listOff = _listSel;
    if (_listSel >= _listOff + 5)  _listOff = _listSel - 4;

    if (click) {
        int si = _sortedIdx[_listSel];
        _autoRx = false;
        if (_rxActive && _activeSat == si) {
            _stopRx();
        } else {
            _stopRx();
            _startRx(si);
        }
        _dirty = true;
    }
}

// ================================================================
// Drawing
// ================================================================

void TinyGSScreen::_drawAll() {
    if (_view == VIEW_FETCH) { _drawFetchProgress(); return; }
    if (_view == VIEW_PKT)   { _drawFullPkt();       return; }

    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);

    _drawHeader();
    _drawList();
    _drawPktMini(190, 25);

    const char* rHint = _rxActive ? "R=StopRX  A=Auto" : "R=ManualRX  A=Auto";
    drawHints(&gfx, "HOLD=Back", "W=WiFi TLE  TAB=Log", rHint);
}

// Header occupies y=24..43 (drawHeader height=20).
// Additional chips drawn within the header bar at y=28.
void TinyGSScreen::_drawHeader() {
    auto& gfx = _disp->gfx();
    drawHeader(&gfx, "TinyGS RX", COL_CYAN);

    gfx.setTextSize(FONT_TINY);

    // TLE status
    uint16_t tleCol = _loadedFallback ? COL_YELLOW : COL_GREEN;
    gfx.setTextColor(tleCol, COL_BG_HEADER);
    gfx.setCursor(132, 29);
    gfx.print(_loadedFallback ? "No TLE" : "TLE OK");

    // Active sat / mode chip
    if (_rxActive && _activeSat >= 0) {
        gfx.fillRoundRect(180, 27, 96, 11, 2, COL_GREEN);
        gfx.setTextColor(COL_BG, COL_GREEN);
        gfx.setCursor(184, 29);
        char chip[18];
        snprintf(chip, sizeof(chip), "RX %.6s", SATS[_activeSat].name);
        gfx.print(chip);
    } else {
        gfx.setTextColor(_autoRx ? COL_CYAN_DIM : COL_TEXT_DARK, COL_BG_HEADER);
        gfx.setCursor(180, 29);
        gfx.print(_autoRx ? "AUTO" : "MANUAL");
    }

    // Packet count
    char pkBuf[12];
    snprintf(pkBuf, sizeof(pkBuf), "%dpkts", _pktCount);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
    gfx.setCursor(277, 29);
    gfx.print(pkBuf);
}

// ----------------------------------------------------------------
// Satellite list  y=44..188  (5 visible rows × 29 px each)
// Divider at y=189 drawn here.
// ----------------------------------------------------------------
void TinyGSScreen::_drawList() {
    auto& gfx = _disp->gfx();
    constexpr int Y0      = 44;
    constexpr int ROW_H   = 29;
    constexpr int VISIBLE = 5;

    int maxOff = N_SATS - VISIBLE;
    if (_listOff > maxOff) _listOff = maxOff;
    if (_listOff < 0)      _listOff = 0;

    for (int row = 0; row < VISIBLE; row++) {
        int sortPos = row + _listOff;
        if (sortPos >= N_SATS) {
            gfx.fillRect(0, Y0 + row * ROW_H, 320, ROW_H, COL_BG);
            continue;
        }
        int  satIdx = _sortedIdx[sortPos];
        bool sel    = (sortPos == _listSel);
        _drawSatRow(satIdx, sel, Y0 + row * ROW_H);
    }

    // Divider between list and packet mini
    gfx.drawFastHLine(0, Y0 + VISIBLE * ROW_H, 320, COL_DIVIDER);
}

void TinyGSScreen::_drawSatRow(int satIdx, bool sel, int y) {
    auto& gfx = _disp->gfx();
    const TGSSat&   s  = SATS[satIdx];
    const SatState& st = _state[satIdx];
    bool isActive = (_rxActive && _activeSat == satIdx);

    uint16_t bg = sel ? COL_BG_PANEL : COL_BG;
    gfx.fillRect(0, y, 320, 28, bg);
    if (sel)     gfx.fillRect(0, y, 3, 28, COL_CYAN);
    if (isActive) gfx.fillRect(0, y, 3, 28, COL_GREEN);
    gfx.drawFastHLine(0, y + 28, 320, COL_BORDER);

    // Visibility dot
    uint16_t dotCol = !st.hasTLE   ? COL_TEXT_DARK :
                       st.visible  ? COL_GREEN : COL_TEXT_DARK;
    gfx.fillCircle(10, y + 9, 3, dotCol);
    if (isActive) gfx.drawCircle(10, y + 9, 5, COL_GREEN);

    // Satellite name (line 1)
    gfx.setTextSize(FONT_TINY);
    uint16_t nameCol = isActive ? COL_GREEN : sel ? COL_CYAN : COL_TEXT;
    gfx.setTextColor(nameCol, bg);
    gfx.setCursor(18, y + 3);
    gfx.print(s.name);

    // Elevation / azimuth (right side, line 1)
    if (st.hasTLE) {
        char elBuf[20];
        if (st.visible) {
            snprintf(elBuf, sizeof(elBuf), "El:%.0f\xb0 Az:%.0f\xb0",
                     st.elDeg, st.azDeg);
            gfx.setTextColor(COL_GREEN, bg);
        } else {
            snprintf(elBuf, sizeof(elBuf), "El:%.0f\xb0", st.elDeg);
            gfx.setTextColor(COL_TEXT_DARK, bg);
        }
        gfx.setCursor(178, y + 3);
        gfx.print(elBuf);
    } else {
        gfx.setTextColor(COL_TEXT_DARK, bg);
        gfx.setCursor(240, y + 3);
        gfx.print("No TLE");
    }

    // LoRa params + frequency (line 2, dimmed)
    char params[36];
    snprintf(params, sizeof(params), "SF%d BW%.0f CR4/%d | %.3f MHz",
             s.sf, s.bwKHz, s.cr, (double)s.freqMHz);
    gfx.setTextColor(COL_TEXT_DARK, bg);
    gfx.setCursor(18, y + 16);
    gfx.print(params);
}

// ----------------------------------------------------------------
// Packet mini-log at the bottom of LIST view
// ----------------------------------------------------------------
void TinyGSScreen::_drawPktMini(int y0, int h) {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, y0, 320, h, COL_BG);
    gfx.setTextSize(FONT_TINY);

    if (_pktCount == 0) {
        gfx.setTextColor(COL_TEXT_DARK, COL_BG);
        gfx.setCursor(4, y0 + 8);
        gfx.print("No packets  TAB=full log");
        return;
    }

    int maxLines = h / 8;
    if (maxLines < 1) maxLines = 1;

    for (int i = 0; i < maxLines && i < _pktCount; i++) {
        int idx = ((_pktHead - 1 - i) % TINYGS_PKT_BUF + TINYGS_PKT_BUF) % TINYGS_PKT_BUF;
        const TGSPacket& p = _pkts[idx];

        char hex[20] = "";
        for (int b = 0; b < p.len && b < 6; b++) {
            char h2[4];
            snprintf(h2, sizeof(h2), "%02X", p.data[b]);
            strncat(hex, h2, sizeof(hex) - strlen(hex) - 1);
        }
        if (p.len > 6) strncat(hex, "..", sizeof(hex) - strlen(hex) - 1);

        uint32_t ageSec = (millis() - p.tsMs) / 1000;
        const char* satName = (p.satIdx < N_SATS) ? SATS[p.satIdx].name : "?";

        char line[56];
        snprintf(line, sizeof(line), "T-%-4lus %-10s %4.0fdBm %4.1fdB | %s",
                 (unsigned long)ageSec, satName,
                 (double)p.rssi, (double)p.snr, hex);

        gfx.setTextColor(i == 0 ? COL_CYAN : COL_TEXT_DIM, COL_BG);
        gfx.setCursor(3, y0 + 1 + i * 8);

        // Clip to screen width (FONT_TINY: ~6px/char, 320/6 ≈ 53 chars)
        char clip[54];
        strlcpy(clip, line, sizeof(clip));
        gfx.print(clip);
    }
}

// ----------------------------------------------------------------
// Full packet log view (TAB)
// Shows newest packet at top, 2 lines per entry: header + hex dump
// ----------------------------------------------------------------
void TinyGSScreen::_drawFullPkt() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Packet Log", COL_CYAN);
    gfx.drawFastHLine(0, 44, 320, COL_DIVIDER);

    gfx.setTextSize(FONT_TINY);

    if (_pktCount == 0) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setCursor(60, 106);
        gfx.print("No packets received yet");
        drawHints(&gfx, "TAB=Back", nullptr, "C=Clear log");
        return;
    }

    constexpr int Y0     = 46;
    constexpr int PKT_H  = 19;  // 2×8px lines + 3px gap
    int maxPkts = (215 - Y0) / PKT_H;  // ≈ 8 packets visible

    for (int i = 0; i < maxPkts && i < _pktCount; i++) {
        int idx = ((_pktHead - 1 - i) % TINYGS_PKT_BUF + TINYGS_PKT_BUF) % TINYGS_PKT_BUF;
        const TGSPacket& p = _pkts[idx];
        int y = Y0 + i * PKT_H;

        uint32_t ageSec = (millis() - p.tsMs) / 1000;
        const char* satName = (p.satIdx < N_SATS) ? SATS[p.satIdx].name : "?";

        // Line 1: age | sat | RSSI | SNR | length
        char hdr[54];
        snprintf(hdr, sizeof(hdr), "T-%-4lus %-11s RSSI:%4.0f SNR:%-4.1f %dB",
                 (unsigned long)ageSec, satName,
                 (double)p.rssi, (double)p.snr, p.len);
        gfx.setTextColor(i == 0 ? COL_CYAN : COL_TEXT_DIM, COL_BG);
        gfx.setCursor(2, y);
        gfx.print(hdr);

        // Line 2: hex dump (up to 22 bytes shown, ~44 hex chars fit)
        char hex[54] = "  ";
        for (int b = 0; b < p.len && b < 22; b++) {
            char h2[4];
            snprintf(h2, sizeof(h2), "%02X", p.data[b]);
            strncat(hex, h2, sizeof(hex) - strlen(hex) - 1);
            if (b < p.len - 1 && b < 21)
                strncat(hex, " ", sizeof(hex) - strlen(hex) - 1);
        }
        if (p.len > 22) strncat(hex, "…", sizeof(hex) - strlen(hex) - 1);

        gfx.setTextColor(COL_TEXT, COL_BG);
        gfx.setCursor(2, y + 9);
        gfx.print(hex);

        gfx.drawFastHLine(0, y + PKT_H - 1, 320, COL_BORDER);
    }

    drawHints(&gfx, "TAB=Back", nullptr, "C=Clear log");
}

// ----------------------------------------------------------------
void TinyGSScreen::_drawFetchProgress() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Fetching TLEs via WiFi", COL_CYAN);

    gfx.setTextSize(FONT_SMALL);
    gfx.setTextColor(COL_TEXT, COL_BG);
    gfx.setCursor(20, 76);
    char status[49];
    strlcpy(status, _fetchStatus, sizeof(status));
    gfx.print(status);

    drawProgressBar(&gfx, 20, 106, 280, 14, _fetchPct / 100.f, COL_CYAN);

    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(20, 132);
    gfx.print("Source: celestrak.org (CATNR query)");
    gfx.setCursor(20, 142);
    gfx.print("Fetching 9 TinyGS satellites by NORAD");

    char satBuf[32];
    snprintf(satBuf, sizeof(satBuf), "Downloaded: %d entries", _tle.count());
    gfx.setTextColor(_tle.count() > 0 ? COL_GREEN : COL_TEXT_DIM, COL_BG);
    gfx.setCursor(20, 158);
    gfx.print(satBuf);

    if (!_loadedFallback && _tle.count() == 0) {
        gfx.setTextColor(COL_YELLOW, COL_BG);
        gfx.setCursor(20, 172);
        gfx.print("Check WiFi credentials in Settings");
    }
}
