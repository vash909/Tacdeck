#pragma once
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "pins.h"
#include "config.h"

struct GpsData {
    double   lat       = 0.0;
    double   lon       = 0.0;
    double   altM      = 0.0;
    double   speedKph  = 0.0;
    double   courseDeg = 0.0;
    uint8_t  satellites= 0;
    float    hdop      = 99.9f;
    bool     fix       = false;
    bool     valid     = false;

    // Time
    uint16_t year   = 2000;
    uint8_t  month  = 1;
    uint8_t  day    = 1;
    uint8_t  hour   = 0;
    uint8_t  minute = 0;
    uint8_t  second = 0;

    uint32_t lastUpdateMs = 0;
};

class GPS {
public:
    GPS() : _serial(1) {}

    bool begin();
    void update();                   // call every loop iteration
    bool hasNewFix();

    const GpsData& data() const { return _data; }

    // Convenience
    bool     hasFix()    const { return _data.fix; }
    double   lat()       const { return _data.lat; }
    double   lon()       const { return _data.lon; }
    double   altM()      const { return _data.altM; }
    double   speedKph()  const { return _data.speedKph; }
    uint8_t  sats()      const { return _data.satellites; }

    // APRS grid square from lat/lon
    static void latLonToGrid(double lat, double lon,
                              char* grid6, size_t sz);

    // APRS position string e.g. "4807.38N/01131.00E>"
    static void encodeAPRSPos(double lat, double lon,
                               char symbol, char* buf, size_t sz);

    TinyGPSPlus& raw() { return _gps; }

private:
    HardwareSerial _serial;
    TinyGPSPlus    _gps;
    GpsData        _data;
    bool           _newFix = false;
    bool           _initialized = false;
};
