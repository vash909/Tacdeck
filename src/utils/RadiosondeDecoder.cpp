#include "RadiosondeDecoder.h"
#include <Arduino.h>
#include <cstring>
#include <cmath>

// Out-of-class definitions for constexpr arrays (required when ODR-used)
constexpr uint8_t RadiosondeDecoder::RS41_SYNC[];
constexpr uint8_t RadiosondeDecoder::DFM09_SYNC[];

// ---- RS41 de-whitening mask (first 64 bytes) ----
const uint8_t RadiosondeDecoder::RS41_MASK[64] = {
    0x96,0x83,0x3E,0x51,0xB1,0x49,0x08,0x98,
    0x32,0x05,0x59,0x0E,0xF9,0x44,0xC6,0x26,
    0x21,0x60,0xC2,0xEA,0x79,0x5D,0x6D,0xA1,
    0x54,0x69,0x47,0x0C,0xDC,0xE8,0x5C,0xF1,
    0xF7,0x76,0x82,0x7F,0x07,0x99,0xA2,0x2C,
    0x93,0x7C,0x30,0x63,0xF5,0x10,0x2E,0x61,
    0xD0,0xBC,0xB4,0xB6,0x06,0xAA,0xF4,0x23,
    0x78,0x6E,0x3B,0xAE,0xBF,0x7B,0x4C,0xC1,
};

// ================================================================
RadiosondeDecoder::Type RadiosondeDecoder::decode(
    const uint8_t* data, size_t len, SondeFrame& out)
{
    memset(&out, 0, sizeof(out));
    out.ts = millis();

    // Try RS41
    if (len >= 8 && decodeRS41(data, len, out)) {
        strlcpy(out.typeStr, "RS41", sizeof(out.typeStr));
        return RS41;
    }

    // Try DFM09
    if (len >= 4 && decodeDFM09(data, len, out)) {
        strlcpy(out.typeStr, "DFM09", sizeof(out.typeStr));
        return DFM09;
    }

    // Try M10
    if (len >= 6 && decodeM10(data, len, out)) {
        strlcpy(out.typeStr, "M10", sizeof(out.typeStr));
        return M10;
    }

    return UNKNOWN;
}

// ================================================================
// RS41 decoder
// Full frame: 8-byte sync + 312 bytes payload
// ================================================================
bool RadiosondeDecoder::decodeRS41(
    const uint8_t* data, size_t len, SondeFrame& out)
{
    if (len < 16) return false;

    // Find RS41 sync
    bool foundSync = false;
    size_t syncOff = 0;
    for (size_t i = 0; i + 8 <= len; i++) {
        if (memcmp(data + i, RS41_SYNC, 8) == 0) {
            foundSync = true;
            syncOff   = i + 8;
            break;
        }
    }
    if (!foundSync) return false;

    // De-whiten payload
    uint8_t payload[200];
    size_t  payLen = len - syncOff;
    if (payLen > sizeof(payload)) payLen = sizeof(payload);

    for (size_t i = 0; i < payLen; i++) {
        payload[i] = data[syncOff + i] ^ RS41_MASK[i % 64];
    }

    // Frame counter (bytes 1-2 of dewhitened payload)
    if (payLen >= 3) {
        out.frame = (uint16_t)(payload[1] | (payload[2] << 8));
    }

    // Parse sub-blocks
    // Each block: [type(1) | len(1) | CRC(2) | data(len)]
    size_t off = 3;  // skip frame header
    bool foundMeas = false, foundGPS = false;

    while (off + 4 < payLen) {
        uint8_t blkType = payload[off];
        uint8_t blkLen  = payload[off + 1];
        if (blkLen == 0 || off + 4 + blkLen > payLen) break;

        const uint8_t* blk = payload + off + 4;  // skip type+len+crc

        switch (blkType) {
            case 0x7A:  // STATUS block — contains serial
                if (blkLen >= 9) {
                    // Serial number starts at offset 3 in block
                    snprintf(out.serial, sizeof(out.serial),
                             "%c%c%c%07u",
                             blk[3], blk[4], blk[5],
                             (unsigned)((blk[6] << 16) | (blk[7] << 8) | blk[8]));
                }
                break;

            case 0x7B:  // MEAS block — temperature, humidity
                foundMeas = rs41ParseMeas(blk, out);
                break;

            case 0x7C:  // GPS POSITION block
                if (blkLen >= 18) {
                    foundGPS = rs41ParseGPS(blk, out);
                }
                break;

            default: break;
        }
        off += 4 + blkLen;
    }

    return foundMeas || foundGPS;
}

bool RadiosondeDecoder::rs41ParseMeas(const uint8_t* blk, SondeFrame& out) {
    // RS41 measurement block layout (simplified):
    // [0-2]: Temp main (float24)
    // [3-5]: Temp ref1
    // [6-8]: Temp ref2
    // Actual RS41 uses calibration constants — simplified decode here

    float rawTemp = decodeFloat24(blk);
    // Rough calibration: raw ADC to temperature
    out.tempC  = rawTemp / 100.f - 273.15f;
    out.humRH  = ((blk[9] | (blk[10] << 8)) & 0x3FF) / 10.f;
    out.pressHpa = 0;  // RS41 doesn't have barometer in base model
    return true;
}

bool RadiosondeDecoder::rs41ParseGPS(const uint8_t* blk, SondeFrame& out) {
    // RS41 GPS block: ecef X,Y,Z velocities + altitude
    // Bytes 0-3: GPS week, 4-7: iTOW
    // 8-11: X ECEF cm, 12-15: Y ECEF cm, 16-19: Z ECEF cm
    // Full ECEF to lat/lon/alt is complex — provide altitude only

    if (blk[0] == 0 && blk[1] == 0) return false;

    // Altitude from pressure formula (if available)
    // For now, use a placeholder from velocity data
    int32_t velZ = (int32_t)((blk[18]) | (blk[19] << 8) |
                              (blk[20] << 16) | ((blk[21] & 0x7F) << 24));
    out.speedMs = velZ / 100.f;  // cm/s to m/s

    // Simple altitude estimate (placeholder until full ECEF decode)
    out.altKm   = 10.f;    // requires full ECEF decode
    out.hasGPS  = false;   // set false until proper decode
    return true;
}

// ================================================================
// DFM09 decoder
// Sync: 0x54 0x97, then 6-byte frames
// ================================================================
bool RadiosondeDecoder::decodeDFM09(
    const uint8_t* data, size_t len, SondeFrame& out)
{
    if (len < 6) return false;

    // Find DFM sync
    bool foundSync = false;
    size_t off = 0;
    for (size_t i = 0; i + 2 <= len; i++) {
        if (data[i] == 0x54 && data[i+1] == 0x97) {
            foundSync = true;
            off = i + 2;
            break;
        }
    }
    if (!foundSync || off + 4 > len) return false;

    // DFM09 6-bit encoded nibbles — minimal decode
    // Each "word" = 5 bytes: type(4bit) | data(32bit)
    uint8_t type = (data[off] >> 4) & 0x0F;
    uint32_t val = ((uint32_t)(data[off] & 0x0F) << 28) |
                   ((uint32_t)data[off+1] << 20) |
                   ((uint32_t)data[off+2] << 12) |
                   ((uint32_t)data[off+3] <<  4) |
                   ((uint32_t)data[off+4] >>  4);

    switch (type) {
        case 0:  // GPS altitude
            out.altKm  = val / 1000.f / 1000.f;
            break;
        case 1:  // Temperature (raw ADC)
            out.tempC  = (int32_t)val / 100.f;
            break;
        case 2:  // Humidity
            out.humRH  = val / 100.f;
            break;
        case 8:  // Serial number ASCII
            snprintf(out.serial, sizeof(out.serial), "DFM%08lX",
                     (unsigned long)val);
            break;
        default: break;
    }

    return true;
}

// ================================================================
// M10 decoder (simple)
// ================================================================
bool RadiosondeDecoder::decodeM10(
    const uint8_t* data, size_t len, SondeFrame& out)
{
    // M10 sync: 0xD6 0xE4 0x35 0x63 (or variants)
    if (len < 10) return false;

    uint8_t sync[4] = {0xD6, 0xE4, 0x35, 0x63};
    bool found = false;
    size_t off = 0;
    for (size_t i = 0; i + 4 <= len; i++) {
        if (memcmp(data + i, sync, 4) == 0) { found = true; off = i + 4; break; }
    }
    if (!found || off + 6 > len) return false;

    // M10 frame: pressure(3) | temp(2) | humidity(1) | ...
    out.pressHpa = ((data[off] << 8) | data[off+1]) / 10.f;
    out.tempC    = (int16_t)((data[off+2] << 8) | data[off+3]) / 100.f;
    out.humRH    = data[off+4] / 2.f;

    snprintf(out.serial, sizeof(out.serial), "M10-%04X",
             (unsigned)((data[off+5] << 8) | data[off+6]));

    out.altKm = (out.pressHpa > 0) ?
        (float)(44330.0 * (1.0 - pow(out.pressHpa / 1013.25, 0.1903))) / 1000.f
        : 0.f;

    return true;
}

// ================================================================
// Helpers
// ================================================================
uint16_t RadiosondeDecoder::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

float RadiosondeDecoder::decodeFloat24(const uint8_t* b) {
    uint32_t raw = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
    return (float)(int32_t)raw;
}
