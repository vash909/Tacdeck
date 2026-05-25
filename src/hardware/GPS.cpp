#include "GPS.h"
#include <Arduino.h>
#include <cstdio>
#include <cmath>

bool GPS::begin() {
    _serial.begin(GPS_BAUD, SERIAL_8N1, TDECK_GPS_RX, TDECK_GPS_TX);
    _initialized = true;
    Serial.println("[GPS] UART1 started");
    return true;
}

void GPS::update() {
    while (_serial.available()) {
        char c = _serial.read();
        if (_gps.encode(c)) {
            // New sentence parsed
            if (_gps.location.isUpdated() && _gps.location.isValid()) {
                _data.lat       = _gps.location.lat();
                _data.lon       = _gps.location.lng();
                _data.fix       = true;
                _data.valid     = true;
                _newFix         = true;
            }
            if (_gps.altitude.isUpdated())  _data.altM      = _gps.altitude.meters();
            if (_gps.speed.isUpdated())     _data.speedKph  = _gps.speed.kmph();
            if (_gps.course.isUpdated())    _data.courseDeg = _gps.course.deg();
            if (_gps.satellites.isUpdated())_data.satellites= _gps.satellites.value();
            if (_gps.hdop.isUpdated())      _data.hdop      = (float)_gps.hdop.value() / 100.f;
            if (_gps.date.isUpdated()) {
                _data.year  = _gps.date.year();
                _data.month = _gps.date.month();
                _data.day   = _gps.date.day();
            }
            if (_gps.time.isUpdated()) {
                _data.hour   = _gps.time.hour();
                _data.minute = _gps.time.minute();
                _data.second = _gps.time.second();
            }
            _data.lastUpdateMs = millis();
        }
    }

    // Invalidate fix after 5 seconds with no update
    if (_data.fix && (millis() - _data.lastUpdateMs) > GPS_TIMEOUT_MS) {
        _data.fix = false;
    }
}

bool GPS::hasNewFix() {
    if (_newFix) { _newFix = false; return true; }
    return false;
}

// ---- Maidenhead grid square ----
void GPS::latLonToGrid(double lat, double lon, char* grid6, size_t sz) {
    if (sz < 7) return;
    lon += 180.0;
    lat += 90.0;
    grid6[0] = 'A' + (int)(lon / 20.0);
    grid6[1] = 'A' + (int)(lat / 10.0);
    grid6[2] = '0' + (int)fmod(lon, 20.0) / 2;
    grid6[3] = '0' + (int)fmod(lat, 10.0);
    grid6[4] = 'a' + (int)(fmod(lon, 2.0) * 12);
    grid6[5] = 'a' + (int)(fmod(lat, 1.0) * 24);
    grid6[6] = '\0';
}

// ---- APRS position encoding ----
void GPS::encodeAPRSPos(double lat, double lon, char symbol,
                         char* buf, size_t sz) {
    // APRS format: DDMM.mmN/DDDMM.mmE>
    char latDir = lat >= 0 ? 'N' : 'S';
    char lonDir = lon >= 0 ? 'E' : 'W';
    if (lat < 0) lat = -lat;
    if (lon < 0) lon = -lon;

    int latDeg  = (int)lat;
    double latMin = (lat - latDeg) * 60.0;
    int lonDeg  = (int)lon;
    double lonMin = (lon - lonDeg) * 60.0;

    snprintf(buf, sz, "%02d%05.2f%c/%03d%05.2f%c%c",
             latDeg, latMin, latDir,
             lonDeg, lonMin, lonDir,
             symbol);
}
