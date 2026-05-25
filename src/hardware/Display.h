#pragma once
#include <LovyanGFX.hpp>
#include "pins.h"
#include "config.h"

// ================================================================
// LovyanGFX board config for T-Deck Plus ST7789 320x240
// ================================================================
class LGFX_TDeck : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
public:
    LGFX_TDeck() {
        // SPI bus
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI3_HOST;
            cfg.spi_mode    = 2;
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 20000000;
            cfg.pin_sclk    = TDECK_SPI_SCK;
            cfg.pin_mosi    = TDECK_SPI_MOSI;
            cfg.pin_miso    = TDECK_SPI_MISO;
            cfg.pin_dc      = TDECK_LCD_DC;
            cfg.use_lock    = true;          // allow shared SPI
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        // Panel
        {
            auto cfg = _panel.config();
            cfg.pin_cs      = TDECK_LCD_CS;
            cfg.pin_rst     = TDECK_LCD_RST;
            cfg.panel_width = TDECK_LCD_WIDTH;
            cfg.panel_height= TDECK_LCD_HEIGHT;
            cfg.offset_x    = 0;
            cfg.offset_y    = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable    = false;
            cfg.invert      = true;          // ST7789 invert
            cfg.rgb_order   = false;
            cfg.dlen_16bit  = false;
            cfg.bus_shared  = true;          // shared SPI
            _panel.config(cfg);
        }
        // Backlight
        {
            auto cfg = _light.config();
            cfg.pin_bl      = TDECK_LCD_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

// ================================================================
// Display singleton wrapper
// ================================================================
class Display {
public:
    Display() = default;

    bool begin();
    void setBrightness(uint8_t value);
    uint8_t getBrightness() const { return _brightness; }

    void setRotation(uint8_t r) { _gfx.setRotation(r); }

    // Direct access to underlying LovyanGFX (for screens & widgets)
    LGFX_TDeck& gfx() { return _gfx; }

    // Convenient wrappers
    void fillScreen(uint32_t color)          { _gfx.fillScreen(color); }
    void drawPixel(int x, int y, uint32_t c) { _gfx.drawPixel(x, y, c); }
    int width()  const { return TDECK_LCD_WIDTH; }
    int height() const { return TDECK_LCD_HEIGHT; }

    // Double-buffered sprite (optional, used by some screens)
    lgfx::LGFX_Sprite& sprite() { return _sprite; }
    bool createSprite(int w, int h);
    void pushSprite(int x, int y);

private:
    LGFX_TDeck        _gfx;
    lgfx::LGFX_Sprite _sprite{&_gfx};
    uint8_t           _brightness = DISPLAY_BRIGHTNESS_DEFAULT;
    bool              _initialized = false;
};
