#include "GPSScreen.h"
#include "../hardware/Display.h"
#include "../hardware/Radio.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"
#include <Arduino.h>
#include <cmath>

GPSScreen::GPSScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void GPSScreen::onEnter() {
    _lastSecondDrawn = 255;
    _lastGridRefreshMs = 0;
    _dirty = true;
}

void GPSScreen::update() {
    if (_dirty) {
        _drawAll();
        if (_gps) _lastSecondDrawn = _gps->data().second;
        _lastGridRefreshMs = millis();
        _dirty = false;
        return;
    }

    // Partial updates only: avoid full-screen redraw blink.
    if (_tabSel == 0) {
        if (_gps && _gps->hasFix()) {
            const GpsData& d = _gps->data();
            if (d.second != _lastSecondDrawn) {
                _drawDataTab();
                _lastSecondDrawn = d.second;
            }
        } else if (millis() - _lastGridRefreshMs > 1000) {
            _drawDataTab();
            _lastGridRefreshMs = millis();
        }
    } else if (millis() - _lastGridRefreshMs > 1000) {
        _drawGridTab();
        _lastGridRefreshMs = millis();
    }
}

void GPSScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "GPS Information", COL_GPS);

    drawButton(&gfx,   0, 44, 100, 12, "Data",  _tabSel==0, COL_GPS);
    drawButton(&gfx, 102, 44, 100, 12, "Grid",  _tabSel==1, COL_GPS);
    gfx.drawFastHLine(0, 58, 320, COL_DIVIDER);

    if (_tabSel == 0) _drawDataTab();
    else              _drawGridTab();

    drawHints(&gfx, "HOLD=Back", "TAB=Switch", nullptr);
}

void GPSScreen::_drawDataTab() {
    auto& gfx = _disp->gfx();
    // Do NOT use a single fillRect(0,60,320,156) here — it clears 50 kpx and
    // causes a visible flash at 1 Hz.  All text is drawn with COL_BG as the
    // text background so it overwrites itself.  Only clear the compass widget
    // area (70×70 px) because it is drawn conditionally.
    constexpr int COMPASS_CX = 270, COMPASS_CY = 140, COMPASS_R = 30;
    gfx.fillRect(COMPASS_CX - COMPASS_R - 6, COMPASS_CY - COMPASS_R - 6,
                 2*(COMPASS_R + 6), 2*(COMPASS_R + 6), COL_BG);
    int y = 60;

    if (!_gps) {
        gfx.setTextColor(COL_RED, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, 100); gfx.print("GPS not initialized");
        return;
    }

    const GpsData& d = _gps->data();
    bool fix = d.fix;

    // Fix status badge
    if (fix) {
        gfx.fillRoundRect(4, y, 60, 12, 3, COL_GREEN);
        gfx.setTextColor(COL_BG, COL_GREEN);
    } else {
        gfx.fillRoundRect(4, y, 60, 12, 3, COL_RED);
        gfx.setTextColor(COL_BG, COL_RED);
    }
    gfx.setTextSize(FONT_TINY);
    gfx.setCursor(8, y + 2);
    gfx.print(fix ? "3D FIX" : "NO FIX");

    char satBuf[12];
    snprintf(satBuf, sizeof(satBuf), "%u sats", d.satellites);
    gfx.setTextColor(fix ? COL_GREEN : COL_TEXT_DIM, COL_BG);
    gfx.setCursor(72, y + 2);
    gfx.print(satBuf);
    y += 16;

    // Large lat / lon
    char latBuf[16], lonBuf[16];
    snprintf(latBuf, sizeof(latBuf), "%.6f %c",
             fabs(d.lat), d.lat >= 0 ? 'N' : 'S');
    snprintf(lonBuf, sizeof(lonBuf), "%.6f %c",
             fabs(d.lon), d.lon >= 0 ? 'E' : 'W');

    gfx.setTextSize(FONT_SMALL);
    gfx.setTextColor(fix ? COL_GPS : COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y);
    gfx.print(latBuf);
    y += 18;
    gfx.setCursor(4, y);
    gfx.print(lonBuf);
    y += 18;
    gfx.drawFastHLine(0, y, 320, COL_DIVIDER);
    y += 4;

    // Other data
    gfx.setTextSize(FONT_TINY);
    char buf[20];

    snprintf(buf, sizeof(buf), "%.1f m", d.altM);
    drawKV(&gfx, 4, y,    "Altitude : ", buf, COL_TEXT_DIM,
           fix ? COL_TEXT : COL_TEXT_DIM);

    snprintf(buf, sizeof(buf), "%.1f km/h", d.speedKph);
    drawKV(&gfx, 164, y, "Speed: ", buf, COL_TEXT_DIM,
           fix ? COL_TEXT : COL_TEXT_DIM);
    y += 12;

    snprintf(buf, sizeof(buf), "%.1f°", d.courseDeg);
    drawKV(&gfx, 4, y,   "Course   : ", buf, COL_TEXT_DIM,
           fix ? COL_TEXT : COL_TEXT_DIM);

    snprintf(buf, sizeof(buf), "%.1f", d.hdop);
    drawKV(&gfx, 164, y, "HDOP : ", buf, COL_TEXT_DIM,
           d.hdop < 2.0f ? COL_GREEN : d.hdop < 5.0f ? COL_YELLOW : COL_RED);
    y += 12;

    // Time / date
    char timeBuf[24];
    snprintf(timeBuf, sizeof(timeBuf),
             "%04u-%02u-%02u  %02u:%02u:%02u UTC",
             d.year, d.month, d.day, d.hour, d.minute, d.second);
    gfx.setTextColor(fix ? COL_CYAN : COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y);
    gfx.print(timeBuf);
    y += 14;

    // Compass widget
    if (fix && d.speedKph > 0.5f) {
        _drawCompass(d.courseDeg, d.speedKph);
    }
}

void GPSScreen::_drawGridTab() {
    auto& gfx = _disp->gfx();
    // No large fillRect — all text uses COL_BG as background and overwrites itself.
    int y = 62;

    if (!_gps || !_gps->hasFix()) {
        gfx.setTextColor(COL_TEXT_DIM, COL_BG);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(60, 100);
        gfx.print("Waiting for GPS fix...");
        return;
    }

    double lat = _gps->lat(), lon = _gps->lon();

    // Maidenhead grid (6 char)
    char grid6[8];
    GPS::latLonToGrid(lat, lon, grid6, sizeof(grid6));

    gfx.setTextSize(FONT_LARGE);
    gfx.setTextColor(COL_GPS, COL_BG);
    gfx.setCursor(60, y);
    gfx.print(grid6);
    y += 40;

    gfx.drawFastHLine(0, y, 320, COL_DIVIDER);
    y += 4;

    gfx.setTextSize(FONT_TINY);

    // APRS position
    char aprsPos[32];
    GPS::encodeAPRSPos(lat, lon, '>', aprsPos, sizeof(aprsPos));
    drawKV(&gfx, 4, y,    "APRS pos : ", aprsPos, COL_TEXT_DIM, COL_GREEN);
    y += 12;

    // Decimal degrees
    char ddBuf[24];
    snprintf(ddBuf, sizeof(ddBuf), "%.6f, %.6f", lat, lon);
    drawKV(&gfx, 4, y, "Dec deg  : ", ddBuf, COL_TEXT_DIM, COL_TEXT);
    y += 12;

    // Degrees Minutes Seconds
    auto toDMS = [](double dd, char dir_pos, char dir_neg, char* out, int sz) {
        char dir = dd >= 0 ? dir_pos : dir_neg;
        if (dd < 0) dd = -dd;
        int d = (int)dd;
        int m = (int)((dd - d) * 60);
        float s = (float)((dd - d - m/60.0) * 3600);
        snprintf(out, sz, "%d°%d'%.1f\"%c", d, m, s, dir);
    };
    char dmsBuf[20];
    toDMS(lat, 'N', 'S', dmsBuf, sizeof(dmsBuf));
    drawKV(&gfx, 4, y, "DMS lat  : ", dmsBuf, COL_TEXT_DIM, COL_CYAN);
    y += 12;
    toDMS(lon, 'E', 'W', dmsBuf, sizeof(dmsBuf));
    drawKV(&gfx, 4, y, "DMS lon  : ", dmsBuf, COL_TEXT_DIM, COL_CYAN);
    y += 12;

    // UTM (simplified placeholder)
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(4, y);
    gfx.print("UTM      : (use full GNSS lib)");
}

void GPSScreen::_drawCompass(float courseDeg, float speedKph) {
    auto& gfx = _disp->gfx();
    constexpr int CX = 270, CY = 140, R = 30;

    gfx.drawCircle(CX, CY, R, COL_BORDER);
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(CX - 3, CY - R - 1); gfx.print("N");
    gfx.setCursor(CX - 3, CY + R - 6); gfx.print("S");
    gfx.setCursor(CX + R - 5, CY - 4); gfx.print("E");
    gfx.setCursor(CX - R, CY - 4);     gfx.print("W");

    float rad = courseDeg * M_PI / 180.f;
    int ex = CX + (int)((R - 4) * sinf(rad));
    int ey = CY - (int)((R - 4) * cosf(rad));
    gfx.drawLine(CX, CY, ex, ey, COL_GPS);
    gfx.fillCircle(ex, ey, 3, COL_GPS);

    char spdBuf[8];
    snprintf(spdBuf, sizeof(spdBuf), "%.0f", speedKph);
    gfx.setTextColor(COL_TEXT, COL_BG);
    gfx.setCursor(CX - 8, CY - 4);
    gfx.print(spdBuf);
}

void GPSScreen::onKey(char key) {
    if (key == KEY_TAB || key == '\t') {
        _tabSel = (_tabSel + 1) % 2; _dirty = true;
    }
}

void GPSScreen::onTrackball(int dx, int dy, bool click) {
    if (dx != 0 || click) { _tabSel = (_tabSel + 1) % 2; _dirty = true; }
}
