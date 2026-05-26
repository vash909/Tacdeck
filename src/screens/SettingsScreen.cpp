#include "SettingsScreen.h"
#include "../hardware/Display.h"
#include "../hardware/Radio.h"
#include "../hardware/GPS.h"
#include "../hardware/Keyboard.h"
#include "../ui/UIManager.h"
#include "../utils/Storage.h"
#include <Arduino.h>

namespace {
constexpr int SETTINGS_Y0 = 44;
constexpr int SETTINGS_ROW_H = 18;
constexpr int SETTINGS_LIST_BOTTOM = CONTENT_BOTTOM - 8;

int visibleRows() {
    const int rows = (SETTINGS_LIST_BOTTOM - SETTINGS_Y0) / SETTINGS_ROW_H;
    return (rows < 1) ? 1 : rows;
}
}

SettingsScreen::SettingsScreen(Display* d, Radio* r, GPS* g, UIManager* ui)
  : _disp(d), _radio(r), _gps(g), _ui(ui) {}

void SettingsScreen::onEnter() {
    _loadSettings();
    _editing = false;
    _viewOffset = 0;
    _ensureSelectionVisible();
    _saved   = false;
    _saveOk  = true;
    _dirty   = true;
}

void SettingsScreen::onExit() {
    if (_editing) _commitEdit(_selIdx);
}

void SettingsScreen::_loadSettings() {
    const struct { const char* label; const char* key; const char* def; } defs[] = {
        // General
        { "Callsign",        NVS_KEY_CALLSIGN,     "N0CALL-9"        },
        { "Brightness",      NVS_KEY_BRIGHTNESS,   "180"             },
        // LoRa Chat
        { "LoRa Freq(MHz)",  NVS_KEY_LORA_FREQ,    "433.000"         },
        { "LoRa SF",         NVS_KEY_LORA_SF,      "9"               },
        { "LoRa BW(kHz)",    NVS_KEY_LORA_BW,      "125"             },
        { "LoRa CR",         NVS_KEY_LORA_CR,      "7"               },
        { "LoRa Power(dBm)", NVS_KEY_LORA_POWER,   "22"              },
        { "LoRa Sync",       NVS_KEY_LORA_SYNC,    "18"              },
        // APRS
        { "APRS Comment",    NVS_KEY_APRS_COMMENT, "TDeck-RFMaster"  },
        { "APRS Symbol",     NVS_KEY_APRS_SYMBOL,  ">"               },
        { "APRS Int.(s)",    NVS_KEY_APRS_INTERVAL,"120"             },
        // WSPR
        { "WSPR Call",       NVS_KEY_WSPR_CALL,    "N0CALL"          },
        { "WSPR Grid",       NVS_KEY_WSPR_GRID,    "AA00"            },
        { "WSPR Power(dBm)", NVS_KEY_WSPR_PWR,     "10"              },
        { "WSPR Freq(MHz)",  NVS_KEY_WSPR_FREQ,    "433.920"         },
        // CW Beacon
        { "CW Text",         NVS_KEY_CW_TEXT,      "CQ DE TACDECK K" },
        { "CW Freq(MHz)",    NVS_KEY_CW_FREQ,      "433.500"         },
        { "CW Speed(WPM)",   NVS_KEY_CW_WPM,       "20"              },
        // Mesh
        { "Mesh NodeName",   NVS_KEY_MESH_NODE_ID, "TACDECK-1"       },
        { "Mesh Freq(MHz)",  NVS_KEY_MESH_FREQ,    "433.175"         },
        // RTTY
        { "RTTY Freq(MHz)",  NVS_KEY_RTTY_FREQ,    "434.000"         },
        { "RTTY Baud",       NVS_KEY_RTTY_BAUD,    "45.45"           },
        { "RTTY Shift(Hz)",  NVS_KEY_RTTY_SHIFT,   "450"             },
        // Scanner
        { "Scan Start(MHz)", NVS_KEY_SCAN_START,   "430.000"         },
        { "Scan End(MHz)",   NVS_KEY_SCAN_END,      "440.000"         },
        { "Scan Step(kHz)",  NVS_KEY_SCAN_STEP,    "12.5"            },
        { "Scan Squelch(dB)",NVS_KEY_SCAN_SQUELCH, "-90"             },
        // POCSAG
        { "POCSAG Freq(MHz)",NVS_KEY_POCSAG_FREQ,  "433.000"         },
        // WiFi
        { "WiFi SSID",       NVS_KEY_WIFI_SSID,    ""                },
        { "WiFi Password",   NVS_KEY_WIFI_PASS,    ""                },
    };
    static_assert(sizeof(defs)/sizeof(defs[0]) == NUM_ITEMS,
                  "NUM_ITEMS mismatch in SettingsScreen");

    for (int i = 0; i < NUM_ITEMS; i++) {
        _items[i].label   = defs[i].label;
        _items[i].key     = defs[i].key;
        _items[i].editing = false;
        Storage::getString(defs[i].key, _items[i].value,
                           sizeof(_items[i].value), defs[i].def);
    }
}

void SettingsScreen::_saveSettings() {
    bool ok = true;
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (!Storage::setString(_items[i].key, _items[i].value)) {
            // If Preferences got closed unexpectedly, reopen and retry once.
            Storage::begin();
            if (!Storage::setString(_items[i].key, _items[i].value)) {
                Serial.printf("[SETTINGS] Save failed key=%s value=%s\n",
                              _items[i].key, _items[i].value);
                ok = false;
            }
        }
    }

    // Reload from persisted values so UI mirrors what is actually in NVS.
    _loadSettings();
    _saveOk  = ok;
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

    const int rows = visibleRows();
    for (int row = 0; row < rows; row++) {
        const int i = _viewOffset + row;
        if (i >= NUM_ITEMS) break;
        const int y = SETTINGS_Y0 + row * SETTINGS_ROW_H;
        const bool sel = (i == _selIdx);

        uint16_t bg = sel ? COL_BG_PANEL : COL_BG;
        gfx.fillRect(0, y, 320, SETTINGS_ROW_H, bg);
        if (sel) gfx.fillRect(0, y, 3, SETTINGS_ROW_H, COL_TEXT);
        gfx.drawFastHLine(0, y + SETTINGS_ROW_H - 1, 320, COL_BORDER);

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

    if (_viewOffset > 0) {
        gfx.fillTriangle(308, SETTINGS_Y0 + 1, 316, SETTINGS_Y0 + 1, 312, SETTINGS_Y0 - 5, COL_TEXT_DIM);
    }
    if (_viewOffset + rows < NUM_ITEMS) {
        const int y = SETTINGS_Y0 + rows * SETTINGS_ROW_H - 2;
        gfx.fillTriangle(308, y - 6, 316, y - 6, 312, y, COL_TEXT_DIM);
    }

    // Saved notification
    if (_saved) {
        const uint16_t badgeCol = _saveOk ? COL_GREEN : COL_RED;
        gfx.fillRoundRect(80, 96, 160, 20, 4, badgeCol);
        gfx.setTextColor(COL_BG, badgeCol);
        gfx.setTextSize(FONT_SMALL);
        gfx.setCursor(_saveOk ? 100 : 86, 100);
        gfx.print(_saveOk ? "Saved!" : "Save FAILED");
    }

    drawHints(&gfx,
              _editing ? "HOLD=Cancel" : "HOLD=Back",
              _editing ? "ENTER=OK"   : "ENTER=Edit",
              "S=Save All");
}

void SettingsScreen::_startEdit(int idx) {
    _editing = true;
    _editBox.clear();
    strncpy(_editBox.buf, _items[idx].value, sizeof(_editBox.buf) - 1);
    _editBox.buf[sizeof(_editBox.buf) - 1] = '\0';
    _editBox.cursor = strlen(_editBox.buf);
    _editBox.maxLen = 31;
    _dirty = true;
}

void SettingsScreen::_commitEdit(int idx) {
    strncpy(_items[idx].value, _editBox.buf, sizeof(_items[idx].value) - 1);
    _items[idx].value[sizeof(_items[idx].value) - 1] = '\0';
    _editing = false;
    _editBox.clear();
    _dirty = true;
}

void SettingsScreen::_ensureSelectionVisible() {
    const int rows = visibleRows();
    if (_selIdx < _viewOffset) _viewOffset = _selIdx;
    if (_selIdx >= _viewOffset + rows) _viewOffset = _selIdx - rows + 1;
    const int maxOffset = (NUM_ITEMS > rows) ? (NUM_ITEMS - rows) : 0;
    if (_viewOffset < 0) _viewOffset = 0;
    if (_viewOffset > maxOffset) _viewOffset = maxOffset;
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
        if (_editing) {
            _editBox.input(key);
            _dirty = true;
        } else {
            _saveSettings();
            _dirty = true;
        }
        return;
    }
    if (!_editing) {
        if (key == KEY_UP   && _selIdx > 0) {
            _selIdx--;
            _ensureSelectionVisible();
            _dirty = true;
        }
        if (key == KEY_DOWN && _selIdx < NUM_ITEMS - 1){
            _selIdx++;
            _ensureSelectionVisible();
            _dirty = true;
        }
        if (key == KEY_ENTER) { _startEdit(_selIdx); }
        return;
    }

    // Editing mode
    if (key == KEY_ENTER) { _commitEdit(_selIdx); return; }
    _editBox.input(key);
    _dirty = true;
}

void SettingsScreen::onTrackball(int dx, int dy, bool click) {
    if (!_editing) {
        if (dy < 0 && _selIdx > 0) {
            _selIdx--;
            _ensureSelectionVisible();
            _dirty = true;
        }
        if (dy > 0 && _selIdx < NUM_ITEMS - 1) {
            _selIdx++;
            _ensureSelectionVisible();
            _dirty = true;
        }
        if (click) _startEdit(_selIdx);
    } else {
        if (click) _commitEdit(_selIdx);
    }
}
