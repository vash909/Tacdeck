#pragma once
#include <stdint.h>

// ================================================================
// TDeck-RFMaster UI Theme — dark "ham radio terminal" style
// All colors are RGB565 (16-bit)
// ================================================================

// -- Background layers --
constexpr uint16_t COL_BG          = 0x0841;  // #080820 very dark blue
constexpr uint16_t COL_BG_PANEL    = 0x1082;  // #101040 panel bg
constexpr uint16_t COL_BG_HEADER   = 0x0C41;  // header bg
constexpr uint16_t COL_BG_INPUT    = 0x1883;  // input field bg

// -- Accent / status --
constexpr uint16_t COL_GREEN       = 0x07E0;  // #00FF00 signal green
constexpr uint16_t COL_GREEN_DIM   = 0x0380;  // #007000 dim green
constexpr uint16_t COL_CYAN        = 0x07FF;  // #00FFFF
constexpr uint16_t COL_CYAN_DIM    = 0x0398;  // dim cyan
constexpr uint16_t COL_YELLOW      = 0xFFE0;  // #FFFF00 warning
constexpr uint16_t COL_ORANGE      = 0xFD20;  // #FFA800
constexpr uint16_t COL_RED         = 0xF800;  // #FF0000 error
constexpr uint16_t COL_RED_DIM     = 0x8000;  // dim red
constexpr uint16_t COL_MAGENTA     = 0xF81F;  // magenta
constexpr uint16_t COL_BLUE        = 0x001F;  // blue
constexpr uint16_t COL_BLUE_DIM    = 0x000F;  // dim blue

// -- Text --
constexpr uint16_t COL_TEXT        = 0xFFFF;  // white
constexpr uint16_t COL_TEXT_DIM    = 0x8410;  // #808080 gray
constexpr uint16_t COL_TEXT_DARK   = 0x4208;  // #404040 dark gray

// -- Borders / dividers --
constexpr uint16_t COL_BORDER      = 0x2965;  // subtle border
constexpr uint16_t COL_BORDER_LIT  = 0x4B6D;  // active border
constexpr uint16_t COL_DIVIDER     = 0x18C3;  // divider line

// -- Mode-specific accent colors (one per screen) --
constexpr uint16_t COL_LORA        = 0x07FF;  // cyan  — LoRa Chat
constexpr uint16_t COL_APRS        = 0x07E0;  // green — APRS
constexpr uint16_t COL_SONDE       = 0xFFE0;  // yellow — Radiosonde
constexpr uint16_t COL_RTTY        = 0xFD20;  // orange — RTTY
constexpr uint16_t COL_WSPR        = 0xF81F;  // magenta — WSPR
constexpr uint16_t COL_SPECTRUM    = 0x07E0;  // green — Spectrum
constexpr uint16_t COL_SCANNER     = 0x07FF;  // cyan — Scanner
constexpr uint16_t COL_WEATHER     = 0xFD20;  // orange — Weather
constexpr uint16_t COL_SAT         = 0x001F;  // blue — Satellite
constexpr uint16_t COL_CW          = 0xF800;  // red — CW
constexpr uint16_t COL_LORAWAN     = 0xFFE0;  // yellow — LoRaWAN
constexpr uint16_t COL_MESH        = 0xF81F;  // magenta — Mesh
constexpr uint16_t COL_POCSAG      = 0x07FF;  // cyan — POCSAG
constexpr uint16_t COL_GPS         = 0x07E0;  // green — GPS

// -- Signal strength bar colors --
inline uint16_t rssiToColor(float rssi) {
    if (rssi > -60)  return COL_GREEN;
    if (rssi > -75)  return COL_YELLOW;
    if (rssi > -90)  return COL_ORANGE;
    return COL_RED_DIM;
}

// -- RSSI bar width (0-W) --
inline int rssiToWidth(float rssi, int maxW) {
    // -120 dBm = 0, -30 dBm = maxW
    int w = (int)((rssi + 120.f) / 90.f * maxW);
    if (w < 0)    w = 0;
    if (w > maxW) w = maxW;
    return w;
}

// ================================================================
// Font size helpers (LovyanGFX sizes 1-7)
// ================================================================
constexpr uint8_t FONT_TINY   = 1;   //  6x 8
constexpr uint8_t FONT_SMALL  = 2;   // 12x16
constexpr uint8_t FONT_MEDIUM = 3;   // 18x24 (approx)
constexpr uint8_t FONT_LARGE  = 4;   // 24x32 (approx)
