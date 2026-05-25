#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ================================================================
// WSPR Type 1 Message Encoder — standalone, no RadioLib dependency
// Ref: K1JT WSPR 2.0 User Guide + wsjt-x source
//
// Encodes callsign (≤6 chars) + 4-char Maidenhead grid + power (dBm)
// → 162 channel symbols (0–3), direct input to FSK4Client
//
// Transmission parameters:
//   Tone spacing : 1.4648 Hz
//   Symbol rate  : 1.4648 Bd  (8192/12000 s per symbol)
//   Total time   : 162 symbols ≈ 110.6 s
// ================================================================

namespace WSPREncoder {

static constexpr int NUM_SYMBOLS = 162;

// ---- Sync vector (hard-coded, from WSPR spec) ----
static const uint8_t SYNC[NUM_SYMBOLS] = {
    1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,1,1,1,1,0,0,0,
    0,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,0,0,1,0,1,1,0,0,0,1,
    1,0,1,0,1,0,0,0,1,0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,0,
    1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,0,0,0,0,0,0,0,1,1,0,1,0,
    1,1,0,0,0,1,0,0,0,1
};

// ---- Char → WSPR code ----
// A-Z=0-25, 0-9=26-35, space=36
static inline uint8_t encChar(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);  // to upper
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 26);
    return 36; // space
}

// ---- Normalize callsign so the single digit is at index 2 (0-based) ----
// E.g.  "N0CALL"  → " N0CAL"
//       "WA2ZKD"  → "WA2ZKD"
//       "VK2ABC"  → "VK2ABC"
//       "G4FON"   → " G4FON"
static inline void normalizeCallsign(const char* in, char out[7]) {
    char tmp[8] = "       "; tmp[7] = '\0';
    int len = (int)strlen(in);
    if (len > 6) len = 6;
    for (int i = 0; i < len; i++)
        tmp[i] = (char)toupper((unsigned char)in[i]);

    // Find first digit position
    int dpos = -1;
    for (int i = 0; i < 6; i++) {
        if (tmp[i] >= '0' && tmp[i] <= '9') { dpos = i; break; }
    }

    // We want digit at position 2; shift if needed
    int shift = 2 - dpos;
    memset(out, ' ', 6); out[6] = '\0';
    for (int i = 0; i < 6; i++) {
        int j = i + shift;
        if (j >= 0 && j < 6) out[j] = tmp[i];
    }
}

// ---- Pack 50 data bits ----
// 28 bits: callsign (n1)
// 22 bits: grid15 * 128 + powerCode  (m1)
static inline void packBits(const char* callsign, const char* grid,
                              int8_t powerDbm, uint8_t bits[7]) {
    char call[7];
    normalizeCallsign(callsign, call);

    // n1 — 28 bits
    // Position semantics: [letter][letter/digit][digit][letter/space][letter/space][letter/space]
    uint32_t n1;
    n1  = encChar(call[0]);            // 0-36 → needs ≤36
    n1  = n1 * 36 + encChar(call[1]);  // 0-36
    // position 2 must be digit → code is 26..35, subtract 26 → 0..9
    uint8_t dcode = encChar(call[2]);
    if (dcode >= 26 && dcode <= 35) dcode -= 26; else dcode = 0;
    n1  = n1 * 10 + dcode;             // 0-9
    n1  = n1 * 27 + encChar(call[3]);  // 0-26 (A-Z + space)
    n1  = n1 * 27 + encChar(call[4]);
    n1  = n1 * 27 + encChar(call[5]);
    // n1 ≤ 36*36*10*27*27*27 = 262,177,560 < 2^28 ✓

    // m1 — grid 15 bits
    char g[5] = "AA00";
    for (int i = 0; i < 4 && grid[i]; i++)
        g[i] = (char)toupper((unsigned char)grid[i]);
    // Clamp to valid range
    if (g[0] < 'A' || g[0] > 'R') g[0] = 'A';
    if (g[1] < 'A' || g[1] > 'R') g[1] = 'A';
    if (g[2] < '0' || g[2] > '9') g[2] = '0';
    if (g[3] < '0' || g[3] > '9') g[3] = '0';

    uint32_t ig = (uint32_t)(g[0] - 'A');  // 0-17
    uint32_t jg = (uint32_t)(g[1] - 'A');  // 0-17
    uint32_t kg = (uint32_t)(g[2] - '0');  // 0-9
    uint32_t lg = (uint32_t)(g[3] - '0');  // 0-9
    uint32_t gridCode = (179 - 10*ig - kg) * 180 + 10*jg + lg; // 0..32759

    // Map power to nearest valid WSPR level
    static const int8_t VALID_PWR[19] = {
        0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60
    };
    int8_t bestPwr = VALID_PWR[0];
    int    bestDiff = 999;
    for (int i = 0; i < 19; i++) {
        int d = abs((int)powerDbm - (int)VALID_PWR[i]);
        if (d < bestDiff) { bestDiff = d; bestPwr = VALID_PWR[i]; }
    }
    uint32_t pCode = 0;
    for (int i = 0; i < 19; i++) {
        if (VALID_PWR[i] == bestPwr) { pCode = (uint32_t)i; break; }
    }

    uint32_t m1 = gridCode * 128 + pCode; // ≤ 32759*128+18 ≈ 4,193,170 < 2^22 ✓

    // Pack into 50 bits (uint64, MSB first) → 7 bytes (6 bits of last byte unused)
    uint64_t packed = ((uint64_t)n1 << 22) | (uint64_t)m1;
    for (int i = 0; i < 7; i++) {
        bits[i] = (uint8_t)((packed >> (48 - i * 8)) & 0xFF);
    }
}

// ---- XOR parity of all set bits ----
static inline uint8_t parity32(uint32_t x) {
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4;
    x ^= x >> 2;  x ^= x >> 1;
    return x & 1u;
}

// ---- Convolutional encoder: rate 1/2, K=32 ----
// Input: 50 data bits + 31 flush zeros = 81 bits → 162 output bits
// Polynomials: G1=0xF2D05351, G2=0xE4613C47 (WSPR standard)
static inline void convEncode(const uint8_t dataBits[7], uint8_t out[162]) {
    static const uint32_t G1 = 0xF2D05351u;
    static const uint32_t G2 = 0xE4613C47u;

    uint32_t reg = 0;
    for (int i = 0; i < 81; i++) {
        // Get data bit i (50 bits packed MSB-first in 7 bytes, then 31 zero pad)
        uint8_t bit = 0;
        if (i < 50) {
            int byteIdx = i >> 3;
            int bitIdx  = 7 - (i & 7); // MSB first
            bit = (dataBits[byteIdx] >> bitIdx) & 1u;
        }
        reg = (reg >> 1) | ((uint32_t)bit << 31);
        out[2*i]     = parity32(reg & G1);
        out[2*i + 1] = parity32(reg & G2);
    }
}

// ---- Bit-reversal of 8-bit value ----
static inline uint8_t revByte(uint8_t b) {
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

// ================================================================
// encode() — the public entry point
// ================================================================
// Fills symbols[162] with 0–3 values ready for FSK4Client::write()
// Each symbol is transmitted at tone_base + symbol * tone_spacing
//
// Usage:
//   uint8_t sym[WSPREncoder::NUM_SYMBOLS];
//   WSPREncoder::encode("N0CALL", "JN45", 20, sym);
//   fsk4Client->write(sym, WSPREncoder::NUM_SYMBOLS);
// ================================================================
static inline void encode(const char* callsign, const char* grid,
                           int8_t powerDbm, uint8_t symbols[NUM_SYMBOLS]) {
    // 1 — Pack 50 data bits
    uint8_t data[7] = {0};
    packBits(callsign, grid, powerDbm, data);

    // 2 — Convolutional encode → 162 bits
    uint8_t enc[162];
    convEncode(data, enc);

    // 3 — Interleave (bit-reversal permutation on 8-bit index)
    uint8_t intlv[162];
    int j = 0;
    for (int i = 0; i < 256 && j < 162; i++) {
        int idx = revByte((uint8_t)i);
        if (idx < 162) intlv[j++] = enc[idx];
    }

    // 4 — Merge with sync vector → channel symbols in {0,1,2,3}
    //     symbol = 2 * data_bit + sync_bit  (Gray coded)
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        symbols[i] = (uint8_t)(2 * intlv[i] + SYNC[i]);
    }
}

} // namespace WSPREncoder
