#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>

// ================================================================
// APRS utility functions — encoding, decoding, MIC-E
// ================================================================

struct APRSPacket {
    char  src[12];
    char  dst[10];
    char  path[30];
    char  info[100];

    // Decoded fields (if position packet)
    bool  hasPos;
    double lat, lon;
    float  altM;
    float  speedKph;
    float  courseDeg;
    char   symbol[3];  // table + code
    char   comment[64];
    char   objectName[10];

    // Packet type
    enum Type { UNKNOWN, POSITION, OBJECT, ITEM, STATUS,
                MSG, TELEMETRY, WEATHER, MIC_E } type;
};

class APRSUtils {
public:
    // Encode a position packet
    // Returns total length written to buf
    static size_t encodePosition(
        char* buf, size_t sz,
        const char* callsign,
        const char* dest,
        double lat, double lon,
        float altM, float speedKph, float courseDeg,
        char symbolTable, char symbolCode,
        const char* comment);

    // Decode a raw APRS info string
    static bool decode(const uint8_t* data, size_t len, APRSPacket& out);

    // Parse AX.25-style header "SRC>DST,PATH:"
    static bool parseHeader(const char* pkt, APRSPacket& out, const char** infoStart);

    // Encode latitude APRS style: "DDMM.mmN"
    static void encodeLat(double lat, char* out, size_t sz);

    // Encode longitude APRS style: "DDDMM.mmE"
    static void encodeLon(double lon, char* out, size_t sz);

    // APRS compressed position (optional, saves space)
    static size_t encodeCompressed(
        char* buf, size_t sz,
        double lat, double lon,
        float altM, char symbolTable, char symbolCode);

    // Decode APRS position from info string
    static bool decodePosition(const char* info, APRSPacket& out);

    // Human-readable packet summary
    static void summarize(const APRSPacket& pkt, char* out, size_t sz);
};
