#include "APRSUtils.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

// ================================================================
void APRSUtils::encodeLat(double lat, char* out, size_t sz) {
    char dir = lat >= 0 ? 'N' : 'S';
    if (lat < 0) lat = -lat;
    int deg = (int)lat;
    double min = (lat - deg) * 60.0;
    snprintf(out, sz, "%02d%05.2f%c", deg, min, dir);
}

void APRSUtils::encodeLon(double lon, char* out, size_t sz) {
    char dir = lon >= 0 ? 'E' : 'W';
    if (lon < 0) lon = -lon;
    int deg = (int)lon;
    double min = (lon - deg) * 60.0;
    snprintf(out, sz, "%03d%05.2f%c", deg, min, dir);
}

// ================================================================
size_t APRSUtils::encodePosition(
    char* buf, size_t sz,
    const char* callsign,
    const char* dest,
    double lat, double lon,
    float altM, float speedKph, float courseDeg,
    char symbolTable, char symbolCode,
    const char* comment)
{
    char latStr[10], lonStr[11];
    encodeLat(lat, latStr, sizeof(latStr));
    encodeLon(lon, lonStr, sizeof(lonStr));

    // Speed in knots, course in degrees
    int speedKnots = (int)(speedKph / 1.852f);
    int course     = (int)courseDeg;
    int altFt      = (int)(altM * 3.28084f);

    return snprintf(buf, sz,
        "%s>%s,WIDE1-1,WIDE2-1:!%c%s%c%s%c%03d/%03d/A=%06d %s",
        callsign, dest,
        symbolTable, latStr,
        symbolTable, lonStr,
        symbolCode,
        course, speedKnots, altFt,
        comment ? comment : "");
}

// ================================================================
bool APRSUtils::parseHeader(const char* pkt, APRSPacket& out,
                             const char** infoStart) {
    // Format: CALL>DEST,PATH,...:info
    const char* arrow = strchr(pkt, '>');
    if (!arrow) return false;

    size_t srcLen = arrow - pkt;
    if (srcLen >= sizeof(out.src)) return false;
    memcpy(out.src, pkt, srcLen);
    out.src[srcLen] = '\0';

    const char* colon = strchr(arrow, ':');
    if (!colon) return false;

    const char* comma = strchr(arrow + 1, ',');
    size_t dstLen;
    if (comma && comma < colon) {
        dstLen = comma - (arrow + 1);
        if (dstLen >= sizeof(out.dst)) dstLen = sizeof(out.dst) - 1;
        memcpy(out.dst, arrow + 1, dstLen);
        out.dst[dstLen] = '\0';

        size_t pathLen = colon - comma - 1;
        if (pathLen >= sizeof(out.path)) pathLen = sizeof(out.path) - 1;
        memcpy(out.path, comma + 1, pathLen);
        out.path[pathLen] = '\0';
    } else {
        dstLen = colon - (arrow + 1);
        if (dstLen >= sizeof(out.dst)) dstLen = sizeof(out.dst) - 1;
        memcpy(out.dst, arrow + 1, dstLen);
        out.dst[dstLen] = '\0';
        out.path[0] = '\0';
    }

    *infoStart = colon + 1;
    return true;
}

// ================================================================
bool APRSUtils::decodePosition(const char* info, APRSPacket& out) {
    if (!info || strlen(info) < 19) return false;

    // Uncompressed position: !DDMM.mmN/DDDMM.mmE>
    // or                     =DDMM.mmN/DDDMM.mmE>
    char type = info[0];
    if (type != '!' && type != '=' && type != '@' && type != '/') return false;

    const char* pos = info + 1;
    if (strlen(pos) < 19) return false;

    // Parse lat: DDMM.mmN (8 chars)
    char latStr[9]; memcpy(latStr, pos, 8); latStr[8] = '\0';
    char latDir = pos[7];
    double latDeg = atof(latStr) / 100.0;
    int latD = (int)latDeg;
    double latM = (latDeg - latD) * 100.0;
    out.lat = latD + latM / 60.0;
    if (latDir == 'S') out.lat = -out.lat;

    // Symbol table char
    out.symbol[0] = pos[8];

    // Parse lon: DDDMM.mmE (9 chars)
    char lonStr[10]; memcpy(lonStr, pos + 9, 9); lonStr[9] = '\0';
    char lonDir = pos[17];
    double lonDeg = atof(lonStr) / 100.0;
    int lonD = (int)lonDeg;
    double lonM = (lonDeg - lonD) * 100.0;
    out.lon = lonD + lonM / 60.0;
    if (lonDir == 'W') out.lon = -out.lon;

    // Symbol code
    out.symbol[1] = pos[18];
    out.symbol[2] = '\0';

    // Course/speed if available (CSE/SPD: "CCC/SSS")
    if (strlen(pos) > 25 && pos[19] >= '0' && pos[19] <= '3') {
        out.courseDeg = (float)atoi(pos + 19);
        if (pos[22] == '/') {
            float speedKnots = (float)atoi(pos + 23);
            out.speedKph  = speedKnots * 1.852f;
        }
    }

    // Altitude: /A=FFFFFF
    const char* alt = strstr(pos, "/A=");
    if (alt) {
        float altFt = (float)atoi(alt + 3);
        out.altM = altFt / 3.28084f;
    }

    out.hasPos = true;
    out.type   = APRSPacket::POSITION;
    return true;
}

// ================================================================
bool APRSUtils::decode(const uint8_t* data, size_t len, APRSPacket& out) {
    if (!data || len == 0) return false;

    memset(&out, 0, sizeof(out));
    out.type = APRSPacket::UNKNOWN;

    char pkt[256];
    size_t copyLen = len < sizeof(pkt) - 1 ? len : sizeof(pkt) - 1;
    memcpy(pkt, data, copyLen);
    pkt[copyLen] = '\0';

    const char* info;
    if (!parseHeader(pkt, out, &info)) {
        // No header — treat entire packet as info
        strncpy(out.info, pkt, sizeof(out.info) - 1);
        out.info[sizeof(out.info) - 1] = '\0';
        return true;
    }

    strncpy(out.info, info, sizeof(out.info) - 1);
    out.info[sizeof(out.info) - 1] = '\0';

    // Decode info field
    decodePosition(info, out);

    return true;
}

// ================================================================
void APRSUtils::summarize(const APRSPacket& pkt, char* out, size_t sz) {
    if (pkt.hasPos) {
        snprintf(out, sz, "%s %.4f,%.4f alt%.0fm %.0fkm/h",
                 pkt.src, pkt.lat, pkt.lon, pkt.altM, pkt.speedKph);
    } else {
        snprintf(out, sz, "%s>%s: %s", pkt.src, pkt.dst, pkt.info);
    }
}
