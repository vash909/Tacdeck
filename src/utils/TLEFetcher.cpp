#include "TLEFetcher.h"
#include "Storage.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ================================================================
// WiFi management
// ================================================================
bool TLEFetcher::connectWiFi(const char* ssid, const char* pass,
                               uint32_t timeoutMs, TLEProgressCb cb) {
    if (!ssid || strlen(ssid) == 0) {
        if (cb) cb("No SSID configured", 0);
        return false;
    }

    if (cb) cb("Connecting to WiFi...", 5);
    Serial.printf("[WiFi] Connecting to: %s\n", ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    int      pct   = 5;
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
        pct = min(pct + 2, 45);
        if (cb) cb("Waiting for WiFi...", pct);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        if (cb) cb("WiFi failed!", 0);
        Serial.println("[WiFi] Connection FAILED");
        return false;
    }

    if (cb) cb("WiFi connected!", 50);
    Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void TLEFetcher::disconnectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

const char* TLEFetcher::ipAddress() const {
    static char buf[20];
    strncpy(buf, WiFi.localIP().toString().c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

// ================================================================
// Download + parse TLE from CelesTrak
// ================================================================
int TLEFetcher::fetchTLE(const char* url, TLEProgressCb cb,
                          const uint32_t* filterIds, int filterCount) {
    if (!isConnected()) {
        if (cb) cb("Not connected to WiFi", 0);
        return 0;
    }

    if (cb) cb("Connecting to CelesTrak...", 52);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(TLE_TIMEOUT_MS);
    http.addHeader("User-Agent", "Tacdeck/1.0 (ESP32-S3)");

    int code = http.GET();
    if (code != 200) {
        char msg[40];
        snprintf(msg, sizeof(msg), "HTTP error: %d", code);
        if (cb) cb(msg, 0);
        Serial.printf("[TLE] HTTP GET failed: %d\n", code);
        http.end();
        return 0;
    }

    if (cb) cb("Downloading TLE data...", 60);

    // Stream parsing: read line by line, look for 3-line TLE sets
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) { http.end(); return 0; }

    _count = 0;
    char line0[25] = "", line1[70] = "", line2[70] = "";
    int  lineState = 0;
    char lineBuf[80];
    int  bIdx = 0;
    int  totalSeen = 0;  // total TLE sets parsed (for progress, ignoring filter)
    uint32_t timeout = millis() + TLE_TIMEOUT_MS;

    while ((http.connected() || stream->available()) && millis() < timeout && _count < MAX_ENTRIES) {
        if (stream->available()) {
            char c = stream->read();
            if (c == '\n' || c == '\r') {
                if (bIdx == 0) continue;
                lineBuf[bIdx] = '\0';
                bIdx = 0;

                // Trim trailing whitespace / CR
                int tlen = strlen(lineBuf);
                while (tlen > 0 && (lineBuf[tlen-1] == ' ' || lineBuf[tlen-1] == '\r'))
                    lineBuf[--tlen] = '\0';

                if (strlen(lineBuf) < 2) continue;

                if (lineBuf[0] == '1' && lineBuf[1] == ' ' && strlen(lineBuf) >= 69) {
                    strncpy(line1, lineBuf, sizeof(line1) - 1);
                    line1[sizeof(line1) - 1] = '\0';
                    lineState = 2;
                } else if (lineBuf[0] == '2' && lineBuf[1] == ' ' && strlen(lineBuf) >= 69) {
                    strncpy(line2, lineBuf, sizeof(line2) - 1);
                    line2[sizeof(line2) - 1] = '\0';

                    TLEEntry entry;
                    if (parseTLE(line0, line1, line2, entry)) {
                        totalSeen++;
                        // Apply NORAD ID filter if provided
                        bool accept = true;
                        if (filterIds && filterCount > 0) {
                            accept = false;
                            for (int f = 0; f < filterCount; f++) {
                                if (filterIds[f] == entry.noradId) { accept = true; break; }
                            }
                        }
                        if (accept) {
                            _entries[_count++] = entry;
                            Serial.printf("[TLE] +%s (NORAD %lu)\n",
                                          entry.name, (unsigned long)entry.noradId);
                        }
                        // Progress based on total seen (not filtered count)
                        int pct = 60 + min(totalSeen * 35 / 150, 35);
                        if (cb && (totalSeen % 5 == 0)) {
                            char msg[36];
                            snprintf(msg, sizeof(msg), "Scanning... %d found", _count);
                            cb(msg, pct);
                        }
                    }
                    line0[0] = '\0'; line1[0] = '\0'; line2[0] = '\0';
                    lineState = 0;
                } else {
                    // Name line
                    strncpy(line0, lineBuf, sizeof(line0) - 1);
                    line0[sizeof(line0) - 1] = '\0';
                    lineState = 1;
                }
            } else {
                if (bIdx < (int)sizeof(lineBuf) - 1)
                    lineBuf[bIdx++] = c;
            }
        } else {
            delay(1);
        }
    }

    http.end();

    if (_count > 0) {
        _lastUpdateMs = millis();
        saveToNVS();
        char msg[32];
        snprintf(msg, sizeof(msg), "Done! %d satellites", _count);
        if (cb) cb(msg, 100);
        Serial.printf("[TLE] Fetched %d satellites\n", _count);
    } else {
        if (cb) cb("No TLEs parsed", 0);
    }

    return _count;
}

// ================================================================
// TLE parsing
// ================================================================
bool TLEFetcher::parseTLE(const char* name, const char* l1,
                            const char* l2, TLEEntry& out) {
    memset(&out, 0, sizeof(out));
    if (!l1 || !l2 || strlen(l1) < 69 || strlen(l2) < 69) return false;
    if (l1[0] != '1' || l2[0] != '2') return false;

    strncpy(out.name, name ? name : "UNKNOWN", sizeof(out.name) - 1);
    strncpy(out.line1, l1, sizeof(out.line1) - 1);
    strncpy(out.line2, l2, sizeof(out.line2) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    out.line1[sizeof(out.line1) - 1] = '\0';
    out.line2[sizeof(out.line2) - 1] = '\0';

    // ---- Line 1 parsing ----
    // NORAD catalog number: cols 3-7
    char tmp[12];
    strncpy(tmp, l1 + 2, 5); tmp[5] = '\0';
    out.noradId = (uint32_t)atoi(tmp);

    // Epoch: cols 19-32  (YY + DDD.dddddddd)
    char epochStr[15];
    strncpy(epochStr, l1 + 18, 14); epochStr[14] = '\0';
    double epochRaw = atof(epochStr);       // YYDDD.dddddddd
    int yy = (int)(epochRaw / 1000.0);
    out.epochDay = epochRaw - (yy * 1000.0);
    out.epochYear = (yy >= 57) ? (1900 + yy) : (2000 + yy);

    // BSTAR drag: cols 54-61 (decimal point implied: +12345-3 = 0.12345e-3)
    {
        char bs[9]; strncpy(bs, l1 + 53, 8); bs[8] = '\0';
        char sign = bs[0];
        char mantissa[6]; strncpy(mantissa, bs + 1, 5); mantissa[5] = '\0';
        char expSign = bs[6];
        char expStr[2]; expStr[0] = bs[7]; expStr[1] = '\0';
        double m = atof(mantissa) * 1e-5;
        int e = atoi(expStr);
        if (expSign == '-') e = -e;
        out.bstarDrag = (sign == '-' ? -1 : 1) * m * pow(10.0, e);
    }

    // ---- Line 2 parsing ----
    // Inclination: cols 9-16
    out.inclination = _parseDouble(l2, 8, 8);
    // RAAN: cols 18-25
    out.raan = _parseDouble(l2, 17, 8);
    // Eccentricity: cols 27-33 (implied decimal point: 1234567 = 0.1234567)
    {
        char es[8]; strncpy(es, l2 + 26, 7); es[7] = '\0';
        out.eccentricity = atof(es) * 1e-7;
    }
    // Argument of perigee: cols 35-42
    out.argPerigee = _parseDouble(l2, 34, 8);
    // Mean anomaly: cols 44-51
    out.meanAnomaly = _parseDouble(l2, 43, 8);
    // Mean motion: cols 53-63 (rev/day)
    out.meanMotion = _parseDouble(l2, 52, 11);

    out.valid = (out.meanMotion > 0);
    return out.valid;
}

double TLEFetcher::_parseDouble(const char* s, int start, int len) {
    char buf[16];
    int l = len < 15 ? len : 15;
    strncpy(buf, s + start, l);
    buf[l] = '\0';
    return atof(buf);
}

// ================================================================
// Simplified SGP4-lite propagation
// Accuracy: ~10–50 km. Good enough for pass prediction & Az/El.
// For mission-critical use, integrate a full SGP4 library.
// ================================================================
bool TLEFetcher::propagate(const TLEEntry& tle, double deltaT,
                             double& latDeg, double& lonDeg, double& altKm) {
    if (!tle.valid || tle.meanMotion <= 0) return false;

    const double GM     = 3.986004418e14;   // m³/s²
    const double RE     = 6371.0;            // km
    const double TPI    = 2.0 * M_PI;       // avoid TPI clash with Arduino.h

    // Semi-major axis from mean motion (rev/day → rad/s)
    double n    = tle.meanMotion * TPI / 86400.0;
    double a    = cbrt(GM / (n * n)) / 1000.0;  // km

    // Mean anomaly at time T
    double M    = fmod(tle.meanAnomaly * M_PI / 180.0 + n * deltaT, TPI);

    // Eccentric anomaly (Newton–Raphson, 5 iterations)
    double e    = tle.eccentricity;
    double E    = M;
    for (int i = 0; i < 5; i++)
        E = M + e * sin(E);

    // True anomaly
    double nu   = 2.0 * atan2(sqrt(1 + e) * sin(E / 2.0),
                                sqrt(1 - e) * cos(E / 2.0));

    // Radius
    double r    = a * (1 - e * cos(E));   // km

    // Argument of latitude
    double u    = tle.argPerigee * M_PI / 180.0 + nu;

    // Inclination + RAAN
    double inc  = tle.inclination * M_PI / 180.0;
    double raan = tle.raan * M_PI / 180.0;

    // Earth-Centered Inertial (ECI) position
    double xECI = r * (cos(raan) * cos(u) - sin(raan) * sin(u) * cos(inc));
    double yECI = r * (sin(raan) * cos(u) + cos(raan) * sin(u) * cos(inc));
    double zECI = r * (sin(u) * sin(inc));

    altKm = r - RE;

    // ECI → ECEF: account for Earth rotation
    // Earth rotation rate: 7.2921150e-5 rad/s
    double GMST = 7.2921150e-5 * deltaT;   // simplified (not epoch-corrected)
    double xECEF =  xECI * cos(GMST) + yECI * sin(GMST);
    double yECEF = -xECI * sin(GMST) + yECI * cos(GMST);
    double zECEF =  zECI;

    // ECEF → Geodetic (spherical)
    latDeg = atan2(zECEF, sqrt(xECEF * xECEF + yECEF * yECEF)) * 180.0 / M_PI;
    lonDeg = atan2(yECEF, xECEF) * 180.0 / M_PI;

    return true;
}

// ================================================================
// Az/El from observer to satellite
// ================================================================
void TLEFetcher::azelFromLatLon(
    double obsLat, double obsLon, double obsAlt,
    double satLat, double satLon, double satAlt,
    double& elDeg, double& azDeg, double& rangeDotKmps)
{
    // Observer ECEF
    double RE   = 6371.0 + obsAlt / 1000.0;
    double oLat = obsLat * M_PI / 180.0;
    double oLon = obsLon * M_PI / 180.0;
    double oX   = RE * cos(oLat) * cos(oLon);
    double oY   = RE * cos(oLat) * sin(oLon);
    double oZ   = RE * sin(oLat);

    // Satellite ECEF
    double sRE  = 6371.0 + satAlt;
    double sLat = satLat * M_PI / 180.0;
    double sLon = satLon * M_PI / 180.0;
    double sX   = sRE * cos(sLat) * cos(sLon);
    double sY   = sRE * cos(sLat) * sin(sLon);
    double sZ   = sRE * sin(sLat);

    // Range vector
    double rx = sX - oX, ry = sY - oY, rz = sZ - oZ;
    double rng = sqrt(rx*rx + ry*ry + rz*rz);

    // Topocentric: rotate to South-East-Up (SEU) frame
    double sinLat = sin(oLat), cosLat = cos(oLat);
    double sinLon = sin(oLon), cosLon = cos(oLon);

    double s =  sinLat*cosLon*rx + sinLat*sinLon*ry - cosLat*rz;  // South
    double ee= -sinLon*rx        + cosLon*ry;                       // East
    double up=  cosLat*cosLon*rx + cosLat*sinLon*ry + sinLat*rz;   // Up

    elDeg = atan2(up, sqrt(s*s + ee*ee)) * 180.0 / M_PI;
    azDeg = fmod(atan2(ee, -s) * 180.0 / M_PI + 360.0, 360.0);

    // Range rate (approximate Doppler velocity component)
    double vOrbit = 7.5;  // km/s approx LEO orbital velocity
    rangeDotKmps  = vOrbit * cos(elDeg * M_PI / 180.0) *
                    (satLon > obsLon ? 1.0 : -1.0);
}

// ================================================================
// NVS persistence
// ================================================================
bool TLEFetcher::saveToNVS() {
    // Store each TLE as key "tle_N" (name+line1+line2 concatenated)
    // Limited by NVS value size (~4000 bytes per key)
    Preferences prefs;
    prefs.begin("tle_cache", false);
    prefs.putInt("tle_count", _count);
    prefs.putULong("tle_ts", millis());

    for (int i = 0; i < _count && i < MAX_ENTRIES; i++) {
        char key[10];
        snprintf(key, sizeof(key), "t%d_n", i);
        prefs.putString(key, _entries[i].name);
        snprintf(key, sizeof(key), "t%d_1", i);
        prefs.putString(key, _entries[i].line1);
        snprintf(key, sizeof(key), "t%d_2", i);
        prefs.putString(key, _entries[i].line2);
    }
    prefs.end();
    return true;
}

bool TLEFetcher::loadFromNVS() {
    Preferences prefs;
    prefs.begin("tle_cache", true);  // read-only
    int n = prefs.getInt("tle_count", 0);
    _lastUpdateMs = prefs.getULong("tle_ts", 0);

    _count = 0;
    for (int i = 0; i < n && i < MAX_ENTRIES; i++) {
        char key[10];
        char name[25] = "", l1[70] = "", l2[70] = "";

        snprintf(key, sizeof(key), "t%d_n", i);
        prefs.getString(key, name, sizeof(name));
        snprintf(key, sizeof(key), "t%d_1", i);
        prefs.getString(key, l1, sizeof(l1));
        snprintf(key, sizeof(key), "t%d_2", i);
        prefs.getString(key, l2, sizeof(l2));

        if (strlen(l1) >= 69 && strlen(l2) >= 69) {
            if (parseTLE(name, l1, l2, _entries[_count]))
                _count++;
        }
    }
    prefs.end();

    Serial.printf("[TLE] Loaded %d entries from NVS\n", _count);
    return _count > 0;
}

// ================================================================
const TLEEntry* TLEFetcher::findByName(const char* name) const {
    if (!name || name[0] == '\0') return nullptr;
    for (int i = 0; i < _count; i++) {
        if (strncasecmp(_entries[i].name, name, strlen(name)) == 0)
            return &_entries[i];
    }
    return nullptr;
}

const TLEEntry* TLEFetcher::findByNorad(uint32_t id) const {
    for (int i = 0; i < _count; i++) {
        if (_entries[i].noradId == id) return &_entries[i];
    }
    return nullptr;
}
