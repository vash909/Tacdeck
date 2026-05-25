#include "Radio.h"
#include "../utils/WSPREncoder.h"
#include <Arduino.h>

// ================================================================
// Constructor: bind SPI pins before SX1262 constructor
// Uses HSPI (= SPI3_HOST) — same SPI host as LovyanGFX display.
// The bus is pre-initialised in main.cpp before display.begin(), so
// _spi.begin() here just re-attaches to the already-configured host.
// ================================================================
Radio::Radio()
  : _spi(HSPI),
    _radio(new Module(TDECK_LORA_CS, TDECK_LORA_DIO1,
                      TDECK_LORA_RST, TDECK_LORA_BUSY, _spi))
{}

// ================================================================
bool Radio::begin() {
    if (_initialized) return true;

    // Attach to the shared SPI3_HOST bus (already initialised in main.cpp).
    // Passing SS=-1 disables hardware CS — RadioLib manages CS via GPIO.
    // This call is safe even after LovyanGFX has used the same host:
    // Arduino's SPIClass returns the existing handle without re-routing GPIOs.
    _spi.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI, -1);

    // Default LoRa init
    LoRaCfg def;
    _lastState = _radio.begin(def.freq, def.bw, def.sf, def.cr,
                               def.syncWord, def.power,
                               def.preamble, LORA_TCXO_VOLTAGE);
    if (_lastState != RADIOLIB_ERR_NONE) {
        Serial.printf("[Radio] begin() failed: %d\n", _lastState);
        return false;
    }

    // DIO2 controls the internal RF switch on T-Deck
    _radio.setDio2AsRfSwitch(true);

    // CRC on by default
    _radio.setCRC(2);

    _mode        = RadioMode::IDLE;
    _initialized = true;
    Serial.println("[Radio] SX1262 OK");
    return true;
}

void Radio::end() {
    _deleteClients();
    _radio.sleep();
    _initialized = false;
}

// ================================================================
// LoRa
// ================================================================
bool Radio::loraBegin(const LoRaCfg& cfg) {
    _deleteClients();
    _lastState = _radio.begin(cfg.freq, cfg.bw, cfg.sf, cfg.cr,
                               cfg.syncWord, cfg.power,
                               cfg.preamble, LORA_TCXO_VOLTAGE);
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _radio.setDio2AsRfSwitch(true);
    _radio.setCRC(cfg.crc ? 2 : 0);
    _mode = RadioMode::IDLE;
    return true;
}

bool Radio::loraTx(const uint8_t* data, size_t len) {
    _mode      = RadioMode::LORA_TX;
    // RadioLib 6.x takes non-const pointer; data is not modified
    _lastState = _radio.transmit(const_cast<uint8_t*>(data), len);
    _mode      = RadioMode::IDLE;
    return _lastState == RADIOLIB_ERR_NONE;
}

bool Radio::loraStartRx() {
    _lastState = _radio.startReceive();
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _mode = RadioMode::LORA_RX;
    return true;
}

bool Radio::loraAvailable() {
    return _radio.available();
}

bool Radio::loraRead(RxPacket& pkt) {
    pkt.len = sizeof(pkt.data);
    _lastState = _radio.readData(pkt.data, pkt.len);
    if (_lastState != RADIOLIB_ERR_NONE) {
        pkt.valid = false;
        return false;
    }
    pkt.len       = _radio.getPacketLength();
    pkt.rssi      = _radio.getRSSI();
    pkt.snr       = _radio.getSNR();
    pkt.freqErr   = _radio.getFrequencyError();
    pkt.timestamp = millis();
    pkt.valid     = true;

    // Restart RX
    _radio.startReceive();
    return true;
}

// ================================================================
// FSK
// ================================================================
bool Radio::fskBegin(const FSKCfg& cfg) {
    _deleteClients();
    _lastState = _radio.beginFSK(cfg.freq, cfg.bitRate, cfg.freqDev,
                                  cfg.rxBW, cfg.power,
                                  cfg.preamble, cfg.ook);
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _radio.setDio2AsRfSwitch(true);
    _mode = RadioMode::IDLE;
    return true;
}

bool Radio::fskTx(const uint8_t* data, size_t len) {
    _mode      = RadioMode::FSK_TX;
    _lastState = _radio.transmit(const_cast<uint8_t*>(data), len);
    _mode      = RadioMode::IDLE;
    return _lastState == RADIOLIB_ERR_NONE;
}

bool Radio::fskStartRx() {
    _lastState = _radio.startReceive();
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _mode = RadioMode::FSK_RX;
    return true;
}

bool Radio::fskAvailable() {
    return _radio.available();
}

bool Radio::fskRead(RxPacket& pkt) {
    pkt.len    = sizeof(pkt.data);
    _lastState = _radio.readData(pkt.data, pkt.len);
    if (_lastState != RADIOLIB_ERR_NONE) { pkt.valid = false; return false; }
    pkt.len       = _radio.getPacketLength();
    pkt.rssi      = _radio.getRSSI();
    pkt.snr       = 0;
    pkt.timestamp = millis();
    pkt.valid     = true;
    _radio.startReceive();
    return true;
}

// ================================================================
// OOK
// ================================================================
bool Radio::ookBegin(float freqMHz, float bitrateKbps, float rxBW) {
    _deleteClients();
    _lastState = _radio.beginFSK(freqMHz, bitrateKbps, 0.0f, rxBW,
                                  0, 16, true);   // ook=true
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _radio.setDio2AsRfSwitch(true);
    _mode = RadioMode::IDLE;
    return true;
}

bool Radio::ookStartRx() {
    _lastState = _radio.startReceive();
    if (_lastState != RADIOLIB_ERR_NONE) return false;
    _mode = RadioMode::OOK_RX;
    return true;
}

// ================================================================
// Spectrum / RSSI utilities
// ================================================================
float Radio::getRSSI() {
    return _radio.getRSSI();
}

float Radio::getChannelRSSI(float freqMHz) {
    _radio.standby();
    _radio.setFrequency(freqMHz);
    _radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
    delayMicroseconds(200);
    float rssi = _radio.getRSSI();
    _radio.standby();
    return rssi;
}

bool Radio::setFrequency(float freqMHz) {
    _lastState = _radio.setFrequency(freqMHz);
    return _lastState == RADIOLIB_ERR_NONE;
}

bool Radio::standby() {
    _mode      = RadioMode::IDLE;
    _lastState = _radio.standby();
    return _lastState == RADIOLIB_ERR_NONE;
}

bool Radio::sleep() {
    _mode      = RadioMode::IDLE;
    _lastState = _radio.sleep();
    return _lastState == RADIOLIB_ERR_NONE;
}

// ================================================================
// TX clients
// ================================================================
bool Radio::initRTTY(float freq, uint32_t shift, float baud, uint8_t enc) {
    _deleteClients();
    _rtty = new RTTYClient(&_radio);
    _lastState = _rtty->begin(freq, shift, baud, enc);
    if (_lastState != RADIOLIB_ERR_NONE) {
        delete _rtty; _rtty = nullptr;
        return false;
    }
    _mode = RadioMode::RTTY_TX;
    return true;
}

bool Radio::initWSPR(float freq, float /*ppm*/) {
    _deleteClients();
    // WSPR uses FSK4: tone spacing ~1.4648 Hz, rate ~1.4648 Bd
    // We approximate with 1 Hz shift and 1 Bd (closest integer)
    _wspr = new FSK4Client(&_radio);
    _lastState = _wspr->begin(freq, 1, 1);  // base, shift_Hz, rate_Bd
    if (_lastState != RADIOLIB_ERR_NONE) {
        delete _wspr; _wspr = nullptr;
        return false;
    }
    _mode = RadioMode::WSPR_TX;
    return true;
}

bool Radio::wsprTransmit(const char* callsign, const char* grid, int8_t powerDbm) {
    if (!initWSPR(WSPR_TX_FREQ, 0.0f)) return false;
    if (!_wspr) return false;

    // Encode 162 WSPR symbols
    uint8_t symbols[WSPREncoder::NUM_SYMBOLS];
    WSPREncoder::encode(callsign, grid, powerDbm, symbols);

    // Transmit — each symbol ≈ 683 ms (8192/12000 s)
    // FSK4Client::write() outputs one symbol per baud period
    size_t sent = _wspr->write(symbols, WSPREncoder::NUM_SYMBOLS);

    _wspr->standby();
    _mode = RadioMode::IDLE;
    return (sent == WSPREncoder::NUM_SYMBOLS);
}

bool Radio::initMorse(float freq, int8_t power) {
    _deleteClients();
    _radio.setFrequency(freq);
    _radio.setOutputPower(power);
    _morse = new MorseClient(&_radio);
    _lastState = _morse->begin(freq, true);  // true = use carrier
    if (_lastState != RADIOLIB_ERR_NONE) {
        delete _morse; _morse = nullptr;
        return false;
    }
    _mode = RadioMode::CW_TX;
    return true;
}

void Radio::_deleteClients() {
    delete _rtty;  _rtty  = nullptr;
    delete _wspr;  _wspr  = nullptr;
    delete _morse; _morse = nullptr;
}

// ================================================================
const char* Radio::stateStr(int16_t s) {
    switch (s) {
        case RADIOLIB_ERR_NONE:             return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:   return "Chip not found";
        case RADIOLIB_ERR_PACKET_TOO_LONG:  return "Packet too long";
        case RADIOLIB_ERR_TX_TIMEOUT:       return "TX timeout";
        case RADIOLIB_ERR_RX_TIMEOUT:       return "RX timeout";
        case RADIOLIB_ERR_CRC_MISMATCH:     return "CRC mismatch";
        case RADIOLIB_ERR_INVALID_FREQUENCY:return "Invalid frequency";
        case RADIOLIB_ERR_INVALID_BANDWIDTH:return "Invalid BW";
        case RADIOLIB_ERR_INVALID_SPREADING_FACTOR: return "Invalid SF";
        case RADIOLIB_ERR_INVALID_CODING_RATE: return "Invalid CR";
        default:
            static char buf[16];
            snprintf(buf, sizeof(buf), "Err %d", s);
            return buf;
    }
}
