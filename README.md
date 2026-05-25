# Tacdeck 🛰️

**All-in-One RF Firmware for the LILYGO T-Deck Plus 433 MHz**

A full-featured firmware that makes the most of the **SX1262** radio on the T-Deck Plus, offering **14 RF modes** in a graphical interface navigable via keyboard and trackball.

---

## Hardware Requirements

| Component | Details |
|-----------|---------|
| Board | LILYGO T-Deck Plus — **433 MHz variant** |
| MCU | ESP32-S3FN16R8 — 240 MHz dual-core |
| Radio | SX1262 — 150–960 MHz |
| Display | ST7789 320×240 TFT |
| GPS | u-blox MIA-M10Q |
| Keyboard | BB Q10 (I2C 0x55) |
| Flash / RAM | 16 MB Flash · 8 MB PSRAM |

---

## Available RF Modes

| # | Mode | Description | Frequency |
|---|------|-------------|-----------|
| 1 | **LoRa Chat** | P2P LoRa text messaging | 433.0 MHz |
| 2 | **LoRa APRS** | GPS beacon + APRS packet RX | 433.775 MHz (EU) |
| 3 | **Radiosonde RX** | Receive RS41 / DFM09 / M10 weather balloons | 400–406 MHz* |
| 4 | **RTTY** | RTTY receive + transmit | 434.0 MHz |
| 5 | **WSPR Beacon** | 4-FSK WSPR beacon | 433.920 MHz |
| 6 | **Spectrum Analyzer** | Graphical 320-pixel RSSI sweep | 430–440 MHz |
| 7 | **Freq Scanner** | Squelch scanner with channel lock | 430–440 MHz |
| 8 | **Weather Sensors** | ISM OOK weather sensor RX | 433.92 MHz |
| 9 | **Satellite Tracker** | Live Az/El + Doppler, **WiFi TLE update** | 435–437 MHz |
| 10 | **CW Beacon** | Morse code TX with visual display | 433.5 MHz |
| 11 | **LoRaWAN** | OTAA join + GPS uplink | EU433 |
| 12 | **LoRa Mesh** | Store-and-forward mesh network | 433.175 MHz |
| 13 | **POCSAG** | Legacy pager message RX | 433.0 MHz |
| 14 | **GPS Info** | Position, Maidenhead grid, DMS | — |
| 15 | **Settings** | Callsign, APRS, frequencies, WiFi | — |

*\* The 433 MHz antenna has reduced sensitivity below 433 MHz. For best radiosonde reception consider a wideband antenna.*

---

## Software Architecture

```
src/
├── main.cpp                    ← Entry point + boot screen
├── hardware/
│   ├── Display.h/cpp           ← LovyanGFX ST7789 driver
│   ├── Radio.h/cpp             ← SX1262 wrapper (RadioLib)
│   ├── GPS.h/cpp               ← TinyGPS++ on UART1
│   └── Keyboard.h/cpp          ← I2C keyboard + trackball GPIO ISRs
├── ui/
│   ├── Theme.h                 ← RGB565 colour palette + helpers
│   ├── Screen.h                ← Abstract base class for all screens
│   ├── UIManager.h/cpp         ← Screen-stack navigation
│   ├── Widgets.h               ← Header/hint bar, RSSI bar, InputBox, TextLog
│   └── StatusBar.h/cpp         ← Top bar: GPS, battery, time
├── screens/
│   ├── MainMenu.h/cpp          ← 4×4 icon grid
│   ├── LoraChat.h/cpp
│   ├── LoraAPRS.h/cpp
│   ├── Radiosonde.h/cpp
│   ├── RTTYScreen.h/cpp
│   ├── WSPRScreen.h/cpp
│   ├── SpectrumScreen.h/cpp
│   ├── FreqScanScreen.h/cpp
│   ├── WeatherScreen.h/cpp
│   ├── SatelliteScreen.h/cpp   ← Tracker + WiFi TLE download
│   ├── CWScreen.h/cpp
│   ├── LoRaWANScreen.h/cpp
│   ├── MeshScreen.h/cpp
│   ├── POCSAGScreen.h/cpp
│   ├── GPSScreen.h/cpp
│   └── SettingsScreen.h/cpp
└── utils/
    ├── TLEFetcher.h/cpp        ← WiFi connect + CelesTrak download + SGP4-lite
    ├── Storage.h/cpp           ← NVS persistent storage (ESP32 Preferences)
    ├── APRSUtils.h/cpp         ← APRS encode / decode
    ├── CWUtils.h/cpp           ← Morse table, text-to-dits, WPM timing
    └── RadiosondeDecoder.h/cpp ← RS41 / DFM09 / M10 frame decoder
include/
    ├── pins.h                  ← All GPIO pin definitions
    └── config.h                ← Frequency defaults, NVS keys, constants
```

---

## 🔨 Build and Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) — VS Code extension or standalone CLI
- No manual library downloads — all dependencies are fetched automatically

### Build and upload

```bash
# Build + flash (433 MHz — default)
pio run --environment tdeck_433 --target upload

# Build + flash (868 MHz variant)
pio run --environment tdeck_868 --target upload

# Serial monitor
pio device monitor
```

PlatformIO auto-detects the COM port. If needed, add `upload_port = COMx` (Windows) or `upload_port = /dev/ttyUSB0` (Linux/macOS) to `platformio.ini`.

### Manual flash with esptool

```bash
pip install esptool

esptool.py --chip esp32s3 --port COMx --baud 921600 \
  write_flash 0x10000 .pio/build/tdeck_433/firmware.bin
```

> **Bootloader tip:** If the auto-reset circuit does not trigger, hold the **trackball button** while pressing the reset button to enter download mode manually.

### USB CDC note

The T-Deck Plus exposes a native ESP32-S3 USB-CDC serial port.

- **Windows 10/11:** Detected automatically as a COM port (no driver needed on modern builds).
- **Linux:** Add your user to `dialout` — `sudo usermod -a -G dialout $USER` — then log out and back in.

---

## 🕹️ UI Navigation

| Input | Action |
|-------|--------|
| **Trackball ↑↓←→** | Move selection / scroll |
| **Trackball click** | Confirm / open |
| **ENTER** | Confirm |
| **ESC** | Back / cancel |
| **S** | Save settings / start scan |
| **TAB** | Switch tab (where available) |
| **W** | WiFi TLE update (Satellite Tracker) |

---

## 🛰️ Satellite Tracker — WiFi TLE Update

Fresh orbital elements (TLEs) are required for accurate tracking. Tacdeck can download them directly over WiFi:

1. Open **Settings** and fill in **WiFi SSID** and **WiFi Password**. Press `S` to save.
2. Open **Satellite Tracker** from the main menu.
3. Press **`W`** — the device connects to your WiFi and downloads the latest amateur-satellite TLEs from [CelesTrak](https://celestrak.org/).
4. A progress screen shows: *Connecting → Downloading → Parsing → Saving*.
5. The satellite list refreshes automatically on success.

> **Fallback:** If WiFi is unavailable or the download fails, the tracker uses built-in hardcoded TLEs (shown with a warning banner). Accuracy degrades after ~2 weeks without an update.

> **Security:** WiFi credentials are stored only in the ESP32's NVS flash. They are **never written to source files** and are excluded from git via `.gitignore`.

---

## 📡 GPIO Pin Map

```
SX1262:  CS=9   RST=17  BUSY=13  DIO1=45
SPI:     SCK=40  MOSI=41  MISO=38
LCD:     CS=12  DC=11   BL=42
GPS:     RX=44  TX=43   (9600 baud, UART1)
KB:      SDA=18  SCL=8  INT=46  (I2C 0x55)
Trackball: UP=3  RIGHT=2  DOWN=15  LEFT=1
Power:   EN=10  BAT_ADC=4
SD Card: CS=39
```

---

## ⚙️ Technical Notes

### Shared SPI bus

The T-Deck Plus uses a **single SPI bus** (SCK=40, MOSI=41, MISO=38) shared between:
- ST7789 LCD (CS=12)
- SX1262 LoRa (CS=9)
- MicroSD (CS=39)

LovyanGFX uses `SPI3_HOST`; RadioLib uses `FSPI` — same physical pins, separate CS management. The main loop is single-threaded, preventing concurrent access.

### SX1262 on 433 MHz

- Optimal range: **433–434 MHz** (antenna tuned to this band)
- Usable 400–500 MHz with reduced sensitivity
- DIO2 configured as RF switch (`setDio2AsRfSwitch(true)`)
- TCXO: 1.8 V (on-board)
- Maximum TX power: 22 dBm

### Radiosonde reception

Radiosondes (RS41, DFM09) transmit at **400–406 MHz**. The 433 MHz antenna works in that band but with approximately **−10 to −15 dB** sensitivity loss. For better results, use a wideband monopole tuned to 403 MHz.

### WSPR

The 433 MHz ISM-band WSPR frequency is not an officially designated amateur band. Use with an amateur radio licence or for laboratory/experimental purposes only.

### SGP4 accuracy

The built-in propagator uses a simplified SGP4-lite (Kepler + ECI→ECEF). Typical accuracy is 10–50 km. For mission-critical tracking, integrate the full [sgp4](https://github.com/dnwrnr/sgp4) library.

---

## 📦 Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | ^1.1.16 | ST7789 display driver |
| [RadioLib](https://github.com/jgromes/RadioLib) | ^6.6.0 | LoRa / FSK / OOK / RTTY / WSPR / CW / LoRaWAN |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | ^1.0.3 | NMEA GPS parser |
| [ArduinoJson](https://arduinojson.org/) | ^7.0.4 | JSON config serialisation |
| ESP32 Preferences | built-in | NVS persistent storage |
| WiFi / HTTPClient | built-in | TLE download over WiFi |

---

## 🔒 Security

> **WiFi passwords are never committed to this repository.**

The `.gitignore` excludes `secrets.h` and `credentials.h`. Any WiFi credentials entered through the Settings screen are stored exclusively in the ESP32 NVS flash and never reach the source tree.

---

*Tacdeck is developed for amateur radio / hobbyist use. Always comply with local regulations regarding transmit frequencies and power levels.*
