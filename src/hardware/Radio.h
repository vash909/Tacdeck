#pragma once
#include <RadioLib.h>
#include <SPI.h>
#include "pins.h"
#include "config.h"

// ================================================================
// Radio — SX1262 wrapper for all RF modes
// ================================================================

enum class RadioMode : uint8_t {
    IDLE = 0,
    LORA_RX,
    LORA_TX,
    FSK_RX,
    FSK_TX,
    OOK_RX,
    RTTY_TX,
    CW_TX,
    WSPR_TX,
    SPECTRUM,
};

struct LoRaCfg {
    float    freq      = LORA_DEFAULT_FREQ;
    float    bw        = LORA_DEFAULT_BW;
    uint8_t  sf        = LORA_DEFAULT_SF;
    uint8_t  cr        = LORA_DEFAULT_CR;
    uint8_t  syncWord  = LORA_DEFAULT_SYNC;
    int8_t   power     = LORA_DEFAULT_POWER;
    uint16_t preamble  = LORA_DEFAULT_PREAMBLE;
    bool     crc       = true;
};

struct FSKCfg {
    float   freq    = FSK_DEFAULT_FREQ;
    float   bitRate = FSK_DEFAULT_BITRATE;
    float   freqDev = FSK_DEFAULT_FREQDEV;
    float   rxBW    = FSK_DEFAULT_RXBW;
    int8_t  power   = FSK_DEFAULT_POWER;
    uint16_t preamble = FSK_DEFAULT_PREAMBLE;
    bool    ook     = false;
};

struct RxPacket {
    uint8_t  data[256];
    size_t   len;
    float    rssi;
    float    snr;
    float    freqErr;
    uint32_t timestamp;
    bool     valid;
};

class Radio {
public:
    Radio();
    bool begin();
    void end();

    // ----- LoRa -----
    bool loraBegin(const LoRaCfg& cfg);
    bool loraTx(const uint8_t* data, size_t len);
    bool loraStartRx();
    bool loraAvailable();
    bool loraRead(RxPacket& pkt);

    // ----- FSK -----
    bool fskBegin(const FSKCfg& cfg);
    bool fskTx(const uint8_t* data, size_t len);
    bool fskStartRx();
    bool fskAvailable();
    bool fskRead(RxPacket& pkt);

    // ----- OOK -----
    bool ookBegin(float freqMHz, float bitrateKbps, float rxBW);
    bool ookStartRx();

    // ----- Utility / Spectrum -----
    float    getRSSI();
    float    getChannelRSSI(float freqMHz);   // single RSSI measurement
    bool     setFrequency(float freqMHz);
    bool     standby();
    bool     sleep();

    // ----- Clients (TX modes) -----
    RTTYClient*  rtty()  { return _rtty; }
    WSPRClient*  wspr()  { return _wspr; }
    MorseClient* morse() { return _morse; }

    bool initRTTY(float freq, uint32_t shift, float baud, uint8_t enc = ITA2);
    bool initWSPR(float freq, float ppm = 0.0f);
    bool initMorse(float freq, int8_t power = CW_DEFAULT_POWER);

    // ----- State -----
    RadioMode mode()  const { return _mode; }
    int16_t   state() const { return _lastState; }
    bool      ready() const { return _initialized; }
    SX1262&   hw()          { return _radio; }

    // Human-readable error from RadioLib state code
    static const char* stateStr(int16_t s);

private:
    SPIClass  _spi;
    SX1262    _radio;
    RadioMode _mode        = RadioMode::IDLE;
    int16_t   _lastState   = RADIOLIB_ERR_NONE;
    bool      _initialized = false;

    RTTYClient*  _rtty  = nullptr;
    WSPRClient*  _wspr  = nullptr;
    MorseClient* _morse = nullptr;

    void _deleteClients();
};
