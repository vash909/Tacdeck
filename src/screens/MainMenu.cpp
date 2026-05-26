#include "MainMenu.h"
#include "../hardware/Display.h"
#include "../hardware/Radio.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../ui/Widgets.h"

// All mode screens
#include "LoraChat.h"
#include "LoraAPRS.h"
#include "Radiosonde.h"
#include "RTTYScreen.h"
#include "WSPRScreen.h"
#include "SpectrumScreen.h"
#include "FreqScanScreen.h"
#include "TinyGSScreen.h"
#include "SatelliteScreen.h"
#include "CWScreen.h"
#include "LoRaWANScreen.h"
#include "MeshScreen.h"
#include "POCSAGScreen.h"
#include "GPSScreen.h"
#include "SettingsScreen.h"

// ================================================================
const MenuTile MainMenu::TILES[16] = {
    { "LoRa Chat",   "P2P Msg",    COL_LORA,     0 },
    { "APRS",        "LoRa APRS",  COL_APRS,     1 },
    { "Radiosonde",  "RS41/DFM09", COL_SONDE,    2 },
    { "RTTY",        "RX/TX",      COL_RTTY,     3 },
    { "WSPR",        "Beacon",     COL_WSPR,     4 },
    { "Spectrum",    "Analyzer",   COL_SPECTRUM, 5 },
    { "Scanner",     "Freq Scan",  COL_SCANNER,  6 },
    { "TinyGS",      "Pkt RX",     COL_CYAN,     7 },
    { "Satellite",   "Tracker",    COL_SAT,      8 },
    { "CW Beacon",   "Morse TX",   COL_CW,       9 },
    { "LoRaWAN",     "OTAA/ABP",   COL_LORAWAN, 10 },
    { "Mesh",        "LoRa Mesh",  COL_MESH,    11 },
    { "POCSAG",      "Pager RX",   COL_POCSAG,  12 },
    { "GPS",         "Info/Map",   COL_GPS,     13 },
    { "Settings",    "Config",     COL_TEXT_DIM,14 },
    { "About",       "v1.0.0",     COL_TEXT_DIM,15 },
};

// ================================================================
MainMenu::MainMenu(Display* disp, Radio* radio, GPS* gps, UIManager* ui)
  : _disp(disp), _radio(radio), _gps(gps), _ui(ui) {}

void MainMenu::onEnter() {
    _dirty = true;
}

void MainMenu::update() {
    if (_dirty) {
        _drawGrid();
        drawHints(&_disp->gfx(),
                  "ENTER/Click=Open", nullptr, "TB=Navigate");
        _dirty = false;
    }
}

// ================================================================
void MainMenu::_drawGrid() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);

    for (int i = 0; i < 16; i++) _drawTile(i, i == _sel);
}

void MainMenu::_drawTile(int idx, bool selected) {
    auto& gfx = _disp->gfx();

    int col   = idx % MENU_COLS;
    int row   = idx / MENU_COLS;
    int x     = col * MENU_TILE_W;
    int y     = STATUS_BAR_H + row * MENU_TILE_H;
    int w     = MENU_TILE_W;
    int h     = MENU_TILE_H;

    const MenuTile& t = TILES[idx];
    uint16_t bg   = selected ? t.color         : COL_BG_PANEL;
    uint16_t fg   = selected ? COL_BG          : t.color;
    uint16_t dim  = selected ? COL_BG          : COL_TEXT_DIM;

    // Background
    gfx.fillRect(x + 1, y + 1, w - 2, h - 2, bg);

    // Border — colored when selected, subtle otherwise
    gfx.drawRect(x, y, w, h, selected ? t.color : COL_BORDER);

    // Icon (simple geometric shape per mode)
    _drawIcon(&gfx, x + 6, y + 5, TILES[idx].icon, fg);

    // Labels
    gfx.setTextSize(FONT_TINY);
    gfx.setTextColor(fg, bg);
    gfx.setCursor(x + 4, y + h - 20);
    gfx.print(t.label);

    gfx.setTextColor(dim, bg);
    gfx.setCursor(x + 4, y + h - 10);
    gfx.print(t.sublabel);
}

// ---- Simple per-mode icons drawn with primitives ----
void MainMenu::_drawIcon(lgfx::LovyanGFX* gfx, int x, int y,
                          int idx, uint16_t col) {
    switch (idx % 16) {
        case 0:  // LoRa Chat — speech bubble
            gfx->fillRoundRect(x, y, 16, 10, 2, col);
            gfx->fillTriangle(x+3, y+9, x+3, y+13, x+8, y+9, col);
            break;
        case 1:  // APRS — diamond + cross
            gfx->drawLine(x+8, y, x+16, y+6, col);
            gfx->drawLine(x+16, y+6, x+8, y+12, col);
            gfx->drawLine(x+8, y+12, x, y+6, col);
            gfx->drawLine(x, y+6, x+8, y, col);
            gfx->drawFastHLine(x+4, y+6, 9, col);
            gfx->drawFastVLine(x+8, y+2, 9, col);
            break;
        case 2:  // Radiosonde — balloon
            gfx->fillCircle(x+8, y+5, 5, col);
            gfx->drawFastVLine(x+8, y+10, 5, col);
            break;
        case 3:  // RTTY — two lines (tones)
            for (int i = 0; i < 4; i++) {
                gfx->drawFastHLine(x, y + i*3, 16, col);
            }
            break;
        case 4:  // WSPR — sine wave
            for (int i = 0; i < 15; i++) {
                int y1 = y + 6 + (int)(4 * sin(i * 0.4));
                int y2 = y + 6 + (int)(4 * sin((i+1) * 0.4));
                gfx->drawLine(x+i, y1, x+i+1, y2, col);
            }
            break;
        case 5:  // Spectrum — bar chart
            for (int i = 0; i < 8; i++) {
                int bh = 2 + (i * 13 / 7) % 12;
                gfx->fillRect(x + i*2, y + 12 - bh, 1, bh, col);
            }
            break;
        case 6:  // Scanner — magnifier
            gfx->drawCircle(x+7, y+6, 5, col);
            gfx->drawLine(x+11, y+10, x+15, y+14, col);
            break;
        case 7:  // TinyGS — cubesat body + downlink arrow
            gfx->drawRect(x+5, y+1, 7, 7, col);           // sat body
            gfx->drawFastHLine(x, y+4, 5, col);            // solar panel L
            gfx->drawFastHLine(x+12, y+4, 4, col);         // solar panel R
            gfx->drawFastVLine(x+8, y+8, 3, col);          // downlink beam
            gfx->fillTriangle(x+5, y+11, x+11, y+11,      // downlink arrow
                              x+8, y+14, col);
            break;
        case 8:  // Satellite — cross + circle
            gfx->drawCircle(x+8, y+6, 4, col);
            gfx->drawLine(x, y, x+4, y+4, col);
            gfx->drawLine(x+12, y+4, x+16, y, col);
            gfx->drawLine(x, y+12, x+4, y+8, col);
            gfx->drawLine(x+12, y+8, x+16, y+12, col);
            break;
        case 9:  // CW — dot dash
            gfx->fillCircle(x+3, y+6, 2, col);
            gfx->fillRect(x+8, y+4, 8, 4, col);
            break;
        case 10: // LoRaWAN — 3 concentric arcs
            for (int r = 3; r <= 9; r += 3) {
                gfx->drawArc(x+4, y+13, r, r-1, 300, 60, col);
            }
            gfx->fillCircle(x+4, y+13, 2, col);
            break;
        case 11: // Mesh — nodes + lines
            gfx->fillCircle(x+2, y+2, 2, col);
            gfx->fillCircle(x+14, y+2, 2, col);
            gfx->fillCircle(x+8, y+12, 2, col);
            gfx->drawLine(x+2, y+2, x+14, y+2, col);
            gfx->drawLine(x+2, y+2, x+8, y+12, col);
            gfx->drawLine(x+14, y+2, x+8, y+12, col);
            break;
        case 12: // POCSAG — envelope
            gfx->drawRect(x+1, y+3, 14, 9, col);
            gfx->drawLine(x+1, y+3, x+8, y+8, col);
            gfx->drawLine(x+8, y+8, x+15, y+3, col);
            break;
        case 13: // GPS — pin
            gfx->fillCircle(x+8, y+4, 4, col);
            gfx->fillTriangle(x+5, y+6, x+11, y+6, x+8, y+14, col);
            gfx->fillCircle(x+8, y+4, 2, COL_BG);
            break;
        case 14: // Settings — gear
            gfx->drawCircle(x+8, y+7, 4, col);
            for (int a = 0; a < 360; a += 45) {
                float rad = a * 3.14159f / 180.f;
                int x1 = x+8 + (int)(4 * cos(rad));
                int y1 = y+7 + (int)(4 * sin(rad));
                int x2 = x+8 + (int)(7 * cos(rad));
                int y2 = y+7 + (int)(7 * sin(rad));
                gfx->drawLine(x1, y1, x2, y2, col);
            }
            break;
        default: // About — info "i"
            gfx->fillCircle(x+8, y+4, 2, col);
            gfx->fillRect(x+7, y+7, 2, 7, col);
            break;
    }
}

// ================================================================
void MainMenu::onTrackball(int dx, int dy, bool click) {
    int prev = _sel;

    if (dx > 0 && (_sel % MENU_COLS) < MENU_COLS - 1) _sel++;
    if (dx < 0 && (_sel % MENU_COLS) > 0)             _sel--;
    if (dy > 0 && _sel + MENU_COLS < 16)               _sel += MENU_COLS;
    if (dy < 0 && _sel - MENU_COLS >= 0)               _sel -= MENU_COLS;

    if (click) { _activateTile(_sel); return; }

    if (_sel != prev) {
        _drawTile(prev, false);
        _drawTile(_sel, true);
    }
}

void MainMenu::onKey(char key) {
    switch (key) {
        case KEY_UP:    onTrackball(0, -1, false); break;
        case KEY_DOWN:  onTrackball(0,  1, false); break;
        case KEY_LEFT:  onTrackball(-1, 0, false); break;
        case KEY_RIGHT: onTrackball( 1, 0, false); break;
        case KEY_ENTER: _activateTile(_sel); break;
        default: break;
    }
}

// ================================================================
void MainMenu::_activateTile(int idx) {
    switch (idx) {
        case  0: _ui->push(new LoraChat(_disp, _radio, _gps, _ui));     break;
        case  1: _ui->push(new LoraAPRS(_disp, _radio, _gps, _ui));     break;
        case  2: _ui->push(new Radiosonde(_disp, _radio, _gps, _ui));   break;
        case  3: _ui->push(new RTTYScreen(_disp, _radio, _gps, _ui));   break;
        case  4: _ui->push(new WSPRScreen(_disp, _radio, _gps, _ui));   break;
        case  5: _ui->push(new SpectrumScreen(_disp, _radio, _gps, _ui)); break;
        case  6: _ui->push(new FreqScanScreen(_disp, _radio, _gps, _ui)); break;
        case  7: _ui->push(new TinyGSScreen(_disp, _radio, _gps, _ui)); break;
        case  8: _ui->push(new SatelliteScreen(_disp, _radio, _gps, _ui)); break;
        case  9: _ui->push(new CWScreen(_disp, _radio, _gps, _ui));     break;
        case 10: _ui->push(new LoRaWANScreen(_disp, _radio, _gps, _ui)); break;
        case 11: _ui->push(new MeshScreen(_disp, _radio, _gps, _ui));   break;
        case 12: _ui->push(new POCSAGScreen(_disp, _radio, _gps, _ui)); break;
        case 13: _ui->push(new GPSScreen(_disp, _radio, _gps, _ui));    break;
        case 14: _ui->push(new SettingsScreen(_disp, _radio, _gps, _ui)); break;
        case 15: /* About — inline redraw */
            _ui->current()->invalidate();
            break;
        default: break;
    }
}
