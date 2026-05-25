#pragma once
#include <LovyanGFX.hpp>
#include "../ui/Screen.h"
#include "../ui/Theme.h"

class Display;
class Radio;
class GPS;
class UIManager;

// ================================================================
// Main Menu — 4×4 grid of RF mode tiles
// ================================================================
struct MenuTile {
    const char* label;
    const char* sublabel;   // e.g. "433 MHz"
    uint16_t    color;
    uint8_t     icon;       // index into icon set
};

class MainMenu : public Screen {
public:
    MainMenu(Display* disp, Radio* radio, GPS* gps, UIManager* ui);

    void onEnter() override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Menu"; }
    bool handlesEsc() const override { return true; }   // ESC in main = no-op

private:
    Display*    _disp;
    Radio*      _radio;
    GPS*        _gps;
    UIManager*  _ui;

    int _sel = 0;    // selected tile index (0-15)

    void _drawGrid();
    void _drawTile(int idx, bool selected);
    void _activateTile(int idx);
    void _drawIcon(lgfx::LovyanGFX* gfx, int x, int y, int idx, uint16_t col);

    static const MenuTile TILES[16];
};
