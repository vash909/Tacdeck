#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"

class Display; class Radio; class GPS; class UIManager;

// ================================================================
// Settings — callsign, APRS, frequency defaults, brightness
// ================================================================
struct SettingItem {
    const char* label;
    char        value[32];
    const char* key;
    bool        editing;
};

class SettingsScreen : public Screen {
public:
    SettingsScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Settings"; }
    bool handlesEsc() const override { return true; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    static constexpr int NUM_ITEMS = 12;
    SettingItem _items[NUM_ITEMS];
    int         _selIdx   = 0;
    InputBox    _editBox;
    bool        _editing  = false;
    bool        _saved    = false;
    uint32_t    _savedMs  = 0;

    void _loadSettings();
    void _saveSettings();
    void _drawAll();
    void _startEdit(int idx);
    void _commitEdit(int idx);
};
