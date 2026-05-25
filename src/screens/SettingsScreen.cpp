#include "SettingsScreen.h"
#include "../hardware/Display.h"
#include "../hardware/Radio.h"
#include "../hardware/GPS.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>

SettingsScreen::SettingsScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void SettingsScreen::onEnter() {
    _loadSettings();
    _editing = false;
    _saved   = false;
    _dirty   = true;
}

void SettingsScreen::onExit() {
    if (_editing) _commitEdit(_selIdx);
}

void SettingsScreen::_loadSettings() {
    const struct { const char* label; const char* key; const char* def; } defs[] = {
        { "Callsign",      NVS_KEY_CALLSIGN,    "N0CALL-9"       },
        { "APRS Comment",  NVS_KEY_APRS_COMMENT,"TDeck-RFMaster" },
        { "APRS Symbol",   NVS_KEY_APRS_SYMBOL, ">"              },
        { "APRS Int.(s)",  NVS_KEY_APRS_INTERVAL,"120"           },
        { "LoRa Freq(MHz)",NVS_KEY_LORA_FREQ,   "433.000"        },
        { "LoRa Power(dBm)",NVS_KEY_LORA_POWER, "22"             },
        { "WSPR Call",     NVS_KEY_WSPR_CALL,   "N0CALL"         },
        { "WSPR Grid",     NVS_KEY_WSPR_GRID,   "AA00"           },
        { "CW Text",       NVS_KEY_CW_TEXT,     "CQ DE TACDECK K"},
        { "Mesh NodeName", NVS_KEY_MESH_NODE_ID,"TACDECK-1"      },
        // WiFi (for TLE fetch)   — shown as last 2 items
        // NOTE: NUM_ITEMS must be 12 in header
        { "WiFi SSID",     NVS_KEY_WIFI_SSID,   ""               },
        { "WiFi Password", NVS_KEY_WIFI_PASS,   ""               },
    };

    for (int i = 0; i < NUM_ITEMS; i++) {
        _items[i].label   = defs[i].label;
        _items[i].key     = defs[i].key;
        _items[i].editing = false;
        Storage::getString(defs[i].key, _items[i].value,
                           sizeof(_items[i].value), defs[i].def);
    }
}

void SettingsScreen::_saveSettings() {
    for (int i = 0; i < NUM_ITEMS; i++) {
        Storage::setString(_items[i].key, _items[i].value);
    }
    _saved   = true;
    _savedMs = millis();
}

// ================================================================
void SettingsScreen::update() {
    if (_saved && millis() - _savedMs > 2000) {
        _saved = false;
        _dirty = true;
    }
    if (_dirty) {
        _drawAll();
        _dirty = false;
    }
}

void SettingsScreen::_drawAll() {
    auto& gfx = _disp->gfx();
    gfx.fillRect(0, STATUS_BAR_H, 320, CONTENT_H + HINT_BAR_H, COL_BG);
    drawHeader(&gfx, "Settings", COL_TEXT);

    constexpr int Y0 = 44;
    constexpr int H  = 18;

    for (int i = 0; i < NUM_ITEMS; i++) {
        int y   = Y0 + i * H;
        bool sel = (i == _selIdx);

        if (y + H > CONTENT_BOTTOM - 8) break;   // clip

        uint16_t bg = sel ? COL_BG_PANEL : COL_BG;
        gfx.fillRect(0, y, 320, H, bg);
        if (sel) gfx.fillRect(0, y, 3, H, COL_TEXT);
        gfx.drawFastHLine(0, y + H - 1, 320, COL_BORDER);

        gfx.setTextSize(FONT_TINY);
        gfx.setTextColor(COL_TEXT_DIM, bg);
        gfx.setCursor(6, y + 5);
        gfx.print(_items[i].label);

        if (sel && _editing) {
            _editBox.active = true;
            _editBox.draw(&gfx, 140, y + 2, 176, COL_TEXT);
        } else {
            gfx.setTextColor(sel ? COL_TEXT : COL_CYAN, bg);
            gfx.setCursor(140, y + 5);
            gfx.print(_items[i].value);
        }
    }

    // Saved notification
    if (_saved) {
        gfx.fillRoundRect(80, 96, 160, 20, 4, COL_GREEN);
        gfx.setTextColor(COL_BG, COL_GREEN);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(100, 100);
        gfx.print("Saved!");
    }

    drawHints(&gfx,
              _editing ? "ESC=Cancel" : "ESC=Back",
              _editing ? "ENTER=OK"   : "ENTER=Edit",
              "S=Save All");
}

void SettingsScreen::_startEdit(int idx) {
    _editing = true;
    _editBox.clear();
    strncpy(_editBox.buf, _items[idx].value, sizeof(_editBox.buf) - 1);
    _editBox.cursor = strlen(_editBox.buf);
    _editBox.maxLen = 31;
    _dirty = true;
}

void SettingsScreen::_commitEdit(int idx) {
    strncpy(_items[idx].value, _editBox.buf, sizeof(_items[idx].value) - 1);
    _editing = false;
    _editBox.clear();
    _dirty = true;
}

// ================================================================
void SettingsScreen::onKey(char key) {
    if (key == KEY_ESC) {
        if (_editing) {
            _editing = false;
            _editBox.clear();
            _dirty = true;
        } else {
            _ui->pop();
        }
        return;
    }
    if (key == 's' || key == 'S') {
        if (_editing) _commitEdit(_selIdx);
        _saveSettings();
        _dirty = true;
        return;
    }
    if (!_editing) {
        if (key == KEY_UP   && _selIdx > 0)           { _selIdx--; _dirty = true; }
        if (key == KEY_DOWN && _selIdx < NUM_ITEMS - 1){ _selIdx++; _dirty = true; }
        if (key == KEY_ENTER) { _startEdit(_selIdx); }
        return;
    }

    // Editing mode
    if (key == KEY_ENTER) { _commitEdit(_selIdx); return; }
    _editBox.input(key);
    _drawAll();
}

void SettingsScreen::onTrackball(int dx, int dy, bool click) {
    if (!_editing) {
        if (dy < 0 && _selIdx > 0)            { _selIdx--; _dirty = true; }
        if (dy > 0 && _selIdx < NUM_ITEMS - 1) { _selIdx++; _dirty = true; }
        if (click) _startEdit(_selIdx);
    } else {
        if (click) _commitEdit(_selIdx);
    }
}
