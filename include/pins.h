#pragma once
// ================================================================
// T-Deck Plus GPIO Pin Definitions
// MCU: ESP32-S3FN16R8 (240 MHz dual-core, 16MB Flash, 8MB PSRAM)
// Source: https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/
// ================================================================

// ---------- Power ----------
#define TDECK_POWERON       10   // HIGH = power on (hold on boot)
#define TDECK_BATTERY_ADC    4   // ADC1_CH3 — battery voltage divider
#define TDECK_BOOT_PIN       0   // BOOT button

// ---------- Shared SPI Bus (LCD + LoRa + SD) ----------
#define TDECK_SPI_SCK       40
#define TDECK_SPI_MOSI      41
#define TDECK_SPI_MISO      38

// ---------- ST7789 LCD (320x240, SPI) ----------
#define TDECK_LCD_CS        12
#define TDECK_LCD_DC        11
#define TDECK_LCD_RST       -1   // No reset pin
#define TDECK_LCD_BL        42   // PWM backlight
#define TDECK_LCD_WIDTH    320
#define TDECK_LCD_HEIGHT   240

// ---------- SX1262 LoRa (shares SPI) ----------
#define TDECK_LORA_CS        9
#define TDECK_LORA_RST      17
#define TDECK_LORA_BUSY     13
#define TDECK_LORA_DIO1     45
// DIO2 is internally connected to RF switch on T-Deck
// Call radio.setDio2AsRfSwitch(true) in firmware

// ---------- GPS — u-blox MIA-M10Q (UART) ----------
#define TDECK_GPS_RX        44   // ESP32 RX ← GPS TX
#define TDECK_GPS_TX        43   // ESP32 TX → GPS RX
#define GPS_BAUD          9600

// ---------- I2C Bus (Keyboard + ES7210) ----------
#define TDECK_I2C_SDA       18
#define TDECK_I2C_SCL        8
#define TDECK_I2C_FREQ   400000  // 400 kHz fast mode

// ---------- BB Q10 Keyboard (I2C 0x55) ----------
#define TDECK_KB_INT        46   // Active LOW interrupt
#define TDECK_KB_ADDR     0x55

// ---------- Trackball (GPIO inputs, active LOW) ----------
#define TDECK_TB_UP          3   // G01
#define TDECK_TB_RIGHT       2   // G02
#define TDECK_TB_DOWN       15   // G03
#define TDECK_TB_LEFT        1   // G04
#define TDECK_TB_CLICK       0   // GPIO0 = BOOT button = trackball click (active LOW)

// ---------- Touch Controller (GT911, I2C 0x5D or 0x14) ----------
#define TDECK_TOUCH_INT     16

// ---------- MicroSD (shares SPI) ----------
#define TDECK_SD_CS         39

// ---------- Audio I2S ----------
#define TDECK_I2S_MCLK      48   // Master clock (shared ADC/DAC)
#define TDECK_I2S_LRCK      21   // Left/Right clock (WS)
#define TDECK_I2S_BCLK      47   // Bit clock
#define TDECK_I2S_DOUT       7   // ESP32 → MAX98357A (speaker)
#define TDECK_I2S_DIN       14   // ES7210 → ESP32 (microphone)
// ES7210 I2C address: 0x40
// MAX98357A gain select: pulled to GND (12dB)
