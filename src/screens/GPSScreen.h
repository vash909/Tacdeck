#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../hardware/GPS.h"

class Display; class Radio; class UIManager;

// ================================================================
// GPS Info — position, speed, altitude, satellite skyplot
// ================================================================
class GPSScreen : public Screen {
public:
    GPSScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "GPS Info"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    int     _tabSel = 0;        // 0=Data, 1=Grid/APRS
    uint8_t _lastSecondDrawn = 255;
    uint32_t _lastGridRefreshMs = 0;

    void _drawAll();
    void _drawDataTab();
    void _drawGridTab();
    void _drawCompass(float courseDeg, float speedKph);
};
