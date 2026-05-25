#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "config.h"

// ================================================================
// TLE Data — Two-Line Element set with parsed Keplerian elements
// ================================================================
struct TLEEntry {
    char name[25];      // Satellite name (line 0)
    char line1[70];     // TLE line 1 (raw)
    char line2[70];     // TLE line 2 (raw)

    // Parsed orbital elements (from line 2)
    double inclination;     // degrees
    double raan;            // Right Ascension of Ascending Node, deg
    double eccentricity;    // 0..1
    double argPerigee;      // degrees
    double meanAnomaly;     // degrees at epoch
    double meanMotion;      // revolutions/day
    double bstarDrag;       // drag coefficient

    // Epoch (Julian date)
    double epochYear;       // 2-digit year
    double epochDay;        // day of year + fractional

    // Derived
    uint32_t noradId;
    bool     valid;
};

// Callback for UI progress updates
using TLEProgressCb = std::function<void(const char* status, int pct)>;

// ================================================================
// TLEFetcher — WiFi connection + CelesTrak HTTP download + parse
// ================================================================
class TLEFetcher {
public:
    TLEFetcher() = default;

    // Connect to WiFi (blocking, with timeout)
    bool connectWiFi(const char* ssid, const char* pass,
                     uint32_t timeoutMs = 15000,
                     TLEProgressCb cb = nullptr);
    void disconnectWiFi();
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    const char* ipAddress() const;

    // Download TLE file from CelesTrak and parse
    // Returns number of entries fetched (0 = error)
    int fetchTLE(const char* url = TLE_SOURCE_URL,
                 TLEProgressCb cb = nullptr);

    // Access fetched data
    int         count()         const { return _count; }
    const TLEEntry* get(int i)  const { return (i >= 0 && i < _count) ? &_entries[i] : nullptr; }
    const TLEEntry* findByName(const char* name) const;
    const TLEEntry* findByNorad(uint32_t id)     const;

    // Persist TLE to NVS (stores each entry as JSON fragment)
    bool saveToNVS();
    bool loadFromNVS();

    // Get last update timestamp (millis at last successful fetch)
    uint32_t lastUpdateMs() const { return _lastUpdateMs; }
    bool     hasData()      const { return _count > 0; }

    // Parse a single TLE from three lines (name, line1, line2)
    static bool parseTLE(const char* name,
                         const char* line1,
                         const char* line2,
                         TLEEntry& out);

    // SGP4-lite: simplified position calculation
    // Returns geocentric lat/lon/alt (degrees, km) at given time
    // "deltaT" = seconds since TLE epoch
    static bool propagate(const TLEEntry& tle, double deltaT,
                           double& latDeg, double& lonDeg, double& altKm);

    // Elevation/Azimuth from observer lat/lon to satellite
    static void azelFromLatLon(double obsLat, double obsLon, double obsAlt,
                                double satLat, double satLon, double satAlt,
                                double& elDeg, double& azDeg,
                                double& rangeDotKmps);

private:
    static constexpr int MAX_ENTRIES = TLE_MAX_SAT;
    TLEEntry  _entries[MAX_ENTRIES];
    int       _count        = 0;
    uint32_t  _lastUpdateMs = 0;

    static double _parseDouble(const char* s, int start, int len);
    static double _parseSignedDouble(const char* s, int start, int len);
};
