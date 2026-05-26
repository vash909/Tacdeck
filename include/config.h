#pragma once
#include <stdint.h>
// ================================================================
// TDeck-RFMaster — Application Configuration & Defaults
// ================================================================

// ---- Display ----
#define DISPLAY_BRIGHTNESS_DEFAULT  180   // 0-255
#define STATUS_BAR_H                 24   // pixels
#define HINT_BAR_H                   24   // pixels
#define CONTENT_Y    STATUS_BAR_H         // content starts here
#define CONTENT_H    (TDECK_LCD_HEIGHT - STATUS_BAR_H - HINT_BAR_H)  // 192px
#define CONTENT_BOTTOM  (TDECK_LCD_HEIGHT - HINT_BAR_H)

// ---- Main Menu ----
#define MENU_COLS                     4
#define MENU_ROWS                     4
#define MENU_TILE_W   (TDECK_LCD_WIDTH / MENU_COLS)   // 80
#define MENU_TILE_H   (CONTENT_H / MENU_ROWS)          // 48
#define MENU_ITEMS                   16

// ---- Radio — LoRa defaults (433 MHz EU) ----
#ifdef BAND_868
  #define LORA_DEFAULT_FREQ       868.0f
  #define APRS_LORA_FREQ          433.775f   // keep APRS always 433
  #define WSPR_FREQ               868.0f
  #define RADIOSONDE_BASE_FREQ    403.0f
#else
  #define LORA_DEFAULT_FREQ       433.0f
  #define APRS_LORA_FREQ          433.775f
  #define WSPR_FREQ               433.920f   // WSPR ISM 433
  #define RADIOSONDE_BASE_FREQ    402.0f
#endif

#define LORA_DEFAULT_BW           125.0f    // kHz
#define LORA_DEFAULT_SF               9
#define LORA_DEFAULT_CR               7     // 4/7
#define LORA_DEFAULT_SYNC          0x12     // Private LoRa
#define LORA_DEFAULT_POWER           22     // dBm (max for SX1262)
#define LORA_DEFAULT_PREAMBLE         8
#define LORA_TCXO_VOLTAGE           1.8f    // V (T-Deck uses 1.8V TCXO)

// LoRa APRS specific (standard EU settings)
#define APRS_LORA_BW              125.0f
#define APRS_LORA_SF                 12
#define APRS_LORA_CR                  5     // 4/5
#define APRS_LORA_SYNC             0x57    // APRS LoRa sync word
#define APRS_LORA_POWER              22

// ---- FSK defaults ----
#define FSK_DEFAULT_FREQ          434.0f
#define FSK_DEFAULT_BITRATE         4.8f   // kbps
#define FSK_DEFAULT_FREQDEV         9.6f   // kHz
#define FSK_DEFAULT_RXBW          156.2f   // kHz
#define FSK_DEFAULT_POWER            22
#define FSK_DEFAULT_PREAMBLE         16

// ---- Radiosonde scan range ----
#define RADIOSONDE_FREQ_MIN       400.0f   // MHz
#define RADIOSONDE_FREQ_MAX       406.5f   // MHz
#define RADIOSONDE_BITRATE          4.8f   // RS41: 4800 bps
#define RADIOSONDE_FREQDEV          2.4f   // RS41: ±2.4 kHz deviation
#define RADIOSONDE_RXBW            19.5f   // kHz

// ---- RTTY defaults ----
#define RTTY_DEFAULT_FREQ         434.0f
#define RTTY_DEFAULT_SHIFT          450    // Hz (170/425/450/850)
#define RTTY_DEFAULT_BAUD          45.45f  // or 50
#define RTTY_DEFAULT_ENCODING         RADIOLIB_ASCII

// ---- WSPR ----
#define WSPR_DEFAULT_POWER_DBM       10    // dBm EIRP to report
#define WSPR_TX_FREQ         WSPR_FREQ
#define WSPR_SLOT_SECONDS           120    // transmit every 2 min

// ---- CW Beacon ----
#define CW_DEFAULT_FREQ           433.5f
#define CW_DEFAULT_SPEED             20    // WPM
#define CW_DEFAULT_POWER             22

// ---- Spectrum Analyzer ----
#define SPECTRUM_START_FREQ       430.0f   // MHz
#define SPECTRUM_END_FREQ         440.0f   // MHz
#define SPECTRUM_STEP_KHZ           25.0f  // kHz per step
#define SPECTRUM_DWELL_MS             5    // ms per step

// ---- Frequency Scanner ----
#define SCANNER_START_FREQ        430.0f
#define SCANNER_END_FREQ          440.0f
#define SCANNER_STEP_KHZ          12.5f
#define SCANNER_SQUELCH_DBM       -90.0f

// ---- Weather RX (ISM 433 MHz OOK sensors) ----
#define WEATHER_FREQ              433.92f  // MHz (ISM center)
#define WEATHER_OOK_BITRATE         1.0f   // kbps (1000 bps OOK)
#define WEATHER_OOK_RXBW          156.2f   // kHz (wider to tolerate cheap sensor drift)

// ---- LoRaWAN ----
#define LORAWAN_REGION_EU433           1
#define LORAWAN_ADR_DEFAULT         true
#define LORAWAN_CONFIRMED_DEFAULT  false

// ---- Mesh Network ----
#define MESH_LORA_FREQ            433.175f
#define MESH_LORA_BW              125.0f
#define MESH_LORA_SF                  9
#define MESH_LORA_CR                  7
#define MESH_SYNC_WORD             0x2B    // Mesh private
#define MESH_MAX_HOPS                  3
#define MESH_BEACON_INTERVAL_S       300   // 5 min

// ---- POCSAG ----
#define POCSAG_FREQ               433.0f
#define POCSAG_BITRATE              1.2f   // 1200 bps
#define POCSAG_FREQDEV              4.5f   // kHz

// ---- Satellite + TLE ----
#define SAT_DOPPLER_UPDATE_MS       500    // Doppler correction interval
// CelesTrak TLE source (amateur satellites group)
#define TLE_SOURCE_URL  "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"
#define TLE_TIMEOUT_MS  15000              // HTTP fetch timeout
#define TLE_MAX_SAT     20                 // max satellites to store

// ---- GPS ----
#define GPS_TIMEOUT_MS            5000
#define GPS_UPDATE_MS              200

// ---- Storage keys ----
#define NVS_NAMESPACE          "rfmaster"
#define NVS_KEY_CALLSIGN       "callsign"
#define NVS_KEY_GRID           "grid"
// LoRa Chat
#define NVS_KEY_LORA_FREQ      "lora_freq"
#define NVS_KEY_LORA_SF        "lora_sf"
#define NVS_KEY_LORA_BW        "lora_bw"
#define NVS_KEY_LORA_CR        "lora_cr"
#define NVS_KEY_LORA_POWER     "lora_pwr"
#define NVS_KEY_LORA_SYNC      "lora_sync"
// APRS
#define NVS_KEY_APRS_COMMENT   "aprs_cmt"
#define NVS_KEY_APRS_SYMBOL    "aprs_sym"
#define NVS_KEY_APRS_INTERVAL  "aprs_int"
// Display
#define NVS_KEY_BRIGHTNESS     "bright"
// WSPR
#define NVS_KEY_WSPR_CALL      "wspr_call"
#define NVS_KEY_WSPR_GRID      "wspr_grid"
#define NVS_KEY_WSPR_PWR       "wspr_pwr"
#define NVS_KEY_WSPR_FREQ      "wspr_freq"
// CW
#define NVS_KEY_CW_TEXT        "cw_text"
#define NVS_KEY_CW_FREQ        "cw_freq"
#define NVS_KEY_CW_WPM         "cw_wpm"
// Mesh
#define NVS_KEY_MESH_NODE_ID   "mesh_id"
#define NVS_KEY_MESH_FREQ      "mesh_freq"
// RTTY
#define NVS_KEY_RTTY_FREQ      "rtty_freq"
#define NVS_KEY_RTTY_BAUD      "rtty_baud"
#define NVS_KEY_RTTY_SHIFT     "rtty_shift"
// Scanner
#define NVS_KEY_SCAN_START     "scan_start"
#define NVS_KEY_SCAN_END       "scan_end"
#define NVS_KEY_SCAN_STEP      "scan_step"
#define NVS_KEY_SCAN_SQUELCH   "scan_squelch"
// POCSAG
#define NVS_KEY_POCSAG_FREQ    "pocsag_freq"
// WiFi
#define NVS_KEY_WIFI_SSID      "wifi_ssid"
#define NVS_KEY_WIFI_PASS      "wifi_pass"
// TLE cache (stored in NVS, refreshed via WiFi)
#define NVS_KEY_TLE_UPDATED    "tle_ts"

// ---- Serial log ----
#define LOG_TAG "Tacdeck"
#define LOGI(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
