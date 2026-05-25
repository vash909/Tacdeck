#pragma once
#include "../ui/Screen.h"
#include "../ui/Theme.h"
#include "../ui/Widgets.h"
#include "../hardware/Radio.h"

class Display; class GPS; class UIManager;

// ================================================================
// 433 MHz Weather Station Receiver
// Decodes Oregon Scientific, Acurite, and generic OOK sensors
// ================================================================
struct WeatherSensor {
    char     id[12];
    char     type[16];
    float    tempC;
    float    humRH;
    float    pressHpa;
    float    windKph;
    uint8_t  battery;     // 0-100%
    uint32_t lastSeenMs;
    bool     valid;
};

class WeatherScreen : public Screen {
public:
    WeatherScreen(Display* d, Radio* r, GPS* g, UIManager* ui);
    void onEnter() override;
    void onExit()  override;
    void update()  override;
    void onKey(char key) override;
    void onTrackball(int dx, int dy, bool click) override;
    const char* name() const override { return "Weather RX"; }

private:
    Display*   _disp;
    Radio*     _radio;
    GPS*       _gps;
    UIManager* _ui;

    static constexpr int MAX_SENSORS = 6;
    WeatherSensor _sensors[MAX_SENSORS];
    int           _sensorCount = 0;
    int           _viewSel     = 0;

    TextLog  _rawLog;
    bool     _showRaw = false;
    float    _listenRSSI = -120.f;

    void _drawAll();
    void _drawStatusLine();
    void _drawSensorGrid();
    void _drawRawLog();
    void _pollRx();
    bool _decodeOregon(const uint8_t* data, size_t len, WeatherSensor& out);
    bool _decodeGenericOOK(const uint8_t* data, size_t len, WeatherSensor& out);
    void _addOrUpdateSensor(const WeatherSensor& s);
};
