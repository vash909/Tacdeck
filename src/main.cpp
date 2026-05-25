// ================================================================
// Tacdeck — T-Deck Plus 433 MHz All-in-One RF Firmware
// ================================================================
//  Features:
//   • LoRa P2P Chat          • LoRa APRS (EU 433.775 MHz)
//   • Radiosonde RX          • RTTY RX/TX
//   • WSPR Beacon            • Spectrum Analyzer
//   • Frequency Scanner      • 433 MHz Weather Sensors
//   • Satellite Tracker      • CW/Morse Beacon
//   • LoRaWAN OTAA           • LoRa Mesh
//   • POCSAG Pager RX        • GPS Information
// ================================================================

#include <Arduino.h>
#include "hardware/Display.h"
#include "hardware/Radio.h"
#include "hardware/GPS.h"
#include "hardware/Keyboard.h"
#include "ui/UIManager.h"
#include "ui/StatusBar.h"
#include "screens/MainMenu.h"
#include "utils/Storage.h"
#include "pins.h"
#include "config.h"

// ---- Global singletons ----
Display    display;
Radio      radio;
GPS        gps;
Keyboard   keyboard;
UIManager  ui;
StatusBar  statusBar;

// ---- Timing ----
static uint32_t _lastStatusMs   = 0;
static uint32_t _lastDisplayMs  = 0;
static constexpr uint32_t STATUS_INTERVAL_MS  = 1000;
static constexpr uint32_t DISPLAY_INTERVAL_MS =   50;   // ~20fps cap

// ================================================================
void drawBootScreen() {
    auto& gfx = display.gfx();
    gfx.fillScreen(COL_BG);

    // Logo
    gfx.setTextSize(3);
    gfx.setTextColor(COL_GREEN, COL_BG);
    gfx.setCursor(30, 50);
    gfx.print("Tac");
    gfx.setTextColor(COL_CYAN, COL_BG);
    gfx.print("deck");

    gfx.setTextSize(1);
    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setCursor(80, 95);
    gfx.print("433 MHz  All-in-One RF Firmware");

    gfx.setCursor(100, 108);
    gfx.print(FIRMWARE_VERSION);

    // Divider
    gfx.drawFastHLine(20, 120, 280, COL_DIVIDER);

    gfx.setTextColor(COL_TEXT_DIM, COL_BG);
    gfx.setTextSize(1);

    const char* features[] = {
        "LoRa Chat  |  LoRa APRS  |  Radiosonde",
        "RTTY  |  WSPR  |  Spectrum Analyzer",
        "Freq Scanner  |  Weather RX",
        "Satellite Tracker  |  CW Beacon",
        "LoRaWAN  |  Mesh Net  |  POCSAG",
        "GPS Info  |  Settings",
    };

    for (int i = 0; i < 6; i++) {
        gfx.setCursor(20, 130 + i * 12);
        gfx.print(features[i]);
    }

    // Loading bar
    gfx.drawRect(20, 210, 280, 12, COL_BORDER);
}

void updateBootProgress(const char* label, int pct) {
    auto& gfx = display.gfx();
    gfx.fillRect(21, 211, 278 * pct / 100, 10, COL_GREEN);
    gfx.setTextColor(COL_TEXT, COL_BG);
    gfx.setTextSize(1);
    gfx.fillRect(20, 225, 280, 10, COL_BG);
    gfx.setCursor(20, 225);
    gfx.print(label);
}

// ================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n========== " FIRMWARE_NAME " " FIRMWARE_VERSION " ==========");

    // ----- Display (must come first) -----
    display.begin();
    drawBootScreen();

    updateBootProgress("Storage...", 10);
    Storage::begin();

    updateBootProgress("Radio SX1262...", 30);
    if (!radio.begin()) {
        auto& gfx = display.gfx();
        gfx.setTextColor(COL_RED, COL_BG);
        gfx.setCursor(20, 200);
        gfx.print("ERROR: SX1262 not found! Check wiring.");
        Serial.println("[BOOT] Radio FAILED");
        delay(5000);
    } else {
        Serial.println("[BOOT] Radio OK");
    }

    updateBootProgress("GPS...", 55);
    gps.begin();

    updateBootProgress("Keyboard...", 70);
    keyboard.begin();

    updateBootProgress("UI...", 85);
    statusBar.begin(&display, &gps, &radio);
    ui.begin(&display, &statusBar);

    updateBootProgress("Ready!", 100);
    delay(800);

    // ----- Main menu -----
    ui.replace(new MainMenu(&display, &radio, &gps, &ui));
    statusBar.setModeName("Menu", COL_TEXT_DIM);
    statusBar.invalidate();

    Serial.println("[BOOT] Setup complete");
}

// ================================================================
void loop() {
    uint32_t now = millis();

    // ---- Read keyboard ----
    keyboard.update();
    char key = keyboard.getKey();
    int  dx = 0, dy = 0;
    bool click = false;
    keyboard.getTrackball(dx, dy, click);

    // ---- Dispatch input ----
    if (key != KEY_NONE) {
        ui.handleKey(key);
    }
    if (dx != 0 || dy != 0 || click) {
        ui.handleTrackball(dx, dy, click);
    }

    // ---- GPS update ----
    gps.update();

    // ---- Display update (capped at ~20fps) ----
    if (now - _lastDisplayMs >= DISPLAY_INTERVAL_MS) {
        _lastDisplayMs = now;
        ui.update();
    }

    // ---- Status bar update (1Hz) ----
    if (now - _lastStatusMs >= STATUS_INTERVAL_MS) {
        _lastStatusMs = now;
        statusBar.update();
    }

    // ---- Yield to RTOS ----
    yield();
}
