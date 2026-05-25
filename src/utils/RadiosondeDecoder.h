#pragma once
#include <stdint.h>
#include <stddef.h>

// ================================================================
// Radiosonde frame decoder — RS41, DFM09, M10
// Input: raw FSK bytes from SX1262
// Output: SondeFrame with decoded telemetry
// ================================================================

struct SondeFrame {
    char    serial[16];    // Radiosonde serial number
    char    typeStr[8];    // "RS41", "DFM09", "M10"
    float   altKm;         // Altitude km
    float   tempC;         // Temperature °C
    float   humRH;         // Humidity %RH
    float   pressHpa;      // Pressure hPa
    float   lat, lon;      // GPS position (if available)
    float   speedMs;       // Ascent rate m/s
    bool    hasGPS;
    uint16_t frame;        // Frame counter
    uint32_t ts;           // millis() timestamp
};

class RadiosondeDecoder {
public:
    enum Type { UNKNOWN, RS41, DFM09, M10, M20 };

    // Main decode entry point — tries all known formats
    static Type decode(const uint8_t* data, size_t len, SondeFrame& out);

    // Individual decoders
    static bool decodeRS41 (const uint8_t* data, size_t len, SondeFrame& out);
    static bool decodeDFM09(const uint8_t* data, size_t len, SondeFrame& out);
    static bool decodeM10  (const uint8_t* data, size_t len, SondeFrame& out);

private:
    // RS41 sync word: 0x86 35 F4 40 93 DF 1A 60
    static constexpr uint8_t RS41_SYNC[] = {0x86,0x35,0xF4,0x40,
                                             0x93,0xDF,0x1A,0x60};
    // DFM09 sync: 0x54 0x97
    static constexpr uint8_t DFM09_SYNC[]= {0x54,0x97};

    // XOR dewhitening table for RS41
    static const uint8_t RS41_MASK[64];

    static uint16_t crc16(const uint8_t* data, size_t len);
    static float    decodeFloat24(const uint8_t* b);

    // RS41 sub-frame parsers
    static bool rs41ParseGPS (const uint8_t* blk, SondeFrame& out);
    static bool rs41ParseMeas(const uint8_t* blk, SondeFrame& out);
};
