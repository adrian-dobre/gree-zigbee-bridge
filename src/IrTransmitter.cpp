#include "IrTransmitter.h"

#include <Arduino.h>

namespace zigree {

namespace {

// RMT resolution: 1 tick = 1 microsecond. Every Gree mark/space is well within
// the 15-bit (32767 us) per-phase limit, so each duration maps to one tick.
constexpr uint32_t kRmtResolutionHz = 1'000'000;

// Carrier duty cycle. ~1/3 is the usual value for IR LEDs and matches what the
// IRremoteESP8266 stack used.
constexpr float kCarrierDuty = 0.33f;

// Without DMA the ESP32-C6 RMT TX channel holds up to this many symbols; the
// driver streams the remainder via an ISR refill. The Gree frame (~70 symbols)
// is comfortably covered, and a single refill window spans tens of ms, so the
// Zigbee stack cannot starve it.
constexpr size_t kMemBlockSymbols = 48;

// 15-bit per-phase duration ceiling.
constexpr uint32_t kMaxPhaseUs = 32767;

}  // namespace

IrTransmitter::IrTransmitter(uint8_t pin, uint32_t carrierHz)
    : _pin(pin), _carrierHz(carrierHz) {}

bool IrTransmitter::begin() {
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = static_cast<gpio_num_t>(_pin);
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = kRmtResolutionHz;
    tx_cfg.mem_block_symbols = kMemBlockSymbols;
    tx_cfg.trans_queue_depth = 4;
    tx_cfg.flags.invert_out = false;

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &_channel);
    if (err != ESP_OK) {
        Serial.printf("[IR] rmt_new_tx_channel failed: 0x%x\n",
                      static_cast<int>(err));
        return false;
    }

    // 38 kHz carrier, applied to the HIGH (mark) phases only. Getting this
    // polarity wrong (carrier on the spaces) is why a naive RMT setup looks
    // dead to the AC.
    rmt_carrier_config_t carrier_cfg = {};
    carrier_cfg.frequency_hz = _carrierHz;
    carrier_cfg.duty_cycle = kCarrierDuty;
    carrier_cfg.flags.polarity_active_low = false;
    err = rmt_apply_carrier(_channel, &carrier_cfg);
    if (err != ESP_OK) {
        Serial.printf("[IR] rmt_apply_carrier failed: 0x%x\n",
                      static_cast<int>(err));
        return false;
    }

    rmt_copy_encoder_config_t enc_cfg = {};
    err = rmt_new_copy_encoder(&enc_cfg, &_encoder);
    if (err != ESP_OK) {
        Serial.printf("[IR] rmt_new_copy_encoder failed: 0x%x\n",
                      static_cast<int>(err));
        return false;
    }

    err = rmt_enable(_channel);
    if (err != ESP_OK) {
        Serial.printf("[IR] rmt_enable failed: 0x%x\n", static_cast<int>(err));
        return false;
    }

    _ready = true;
    Serial.printf("[IR] RMT transmitter ready on pin %u (%lu Hz carrier)\n",
                  _pin, static_cast<unsigned long>(_carrierHz));
    return true;
}

void IrTransmitter::push(uint8_t level, uint32_t micros) {
    if (micros > kMaxPhaseUs) micros = kMaxPhaseUs;
    _durations.push_back(static_cast<uint16_t>(micros));
    _levels.push_back(level);
}

void IrTransmitter::startFrame() {
    _durations.clear();
    _levels.clear();
}

void IrTransmitter::mark(uint32_t micros) {
    push(1, micros);
}

void IrTransmitter::space(uint32_t micros) {
    push(0, micros);
}

void IrTransmitter::sendFrame() {
    if (!_ready || _durations.empty()) return;

    // Drop a trailing zero-length segment (the protocol terminates frames with
    // space(0)); a zero duration inside an RMT symbol marks end-of-stream.
    size_t n = _durations.size();
    while (n > 0 && _durations[n - 1] == 0) n--;
    if (n == 0) return;

    // Pack alternating (mark, space) pairs into RMT symbols (two phases each).
    _symbols.clear();
    _symbols.reserve((n + 1) / 2);
    for (size_t i = 0; i < n; i += 2) {
        rmt_symbol_word_t sym = {};
        sym.duration0 = _durations[i];
        sym.level0 = _levels[i];
        if (i + 1 < n) {
            sym.duration1 = _durations[i + 1];
            sym.level1 = _levels[i + 1];
        } else {
            // Odd tail: second phase is the end-of-stream marker (duration 0).
            sym.duration1 = 0;
            sym.level1 = 0;
        }
        _symbols.push_back(sym);
    }

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = 0;  // idle low (IR LED off) after the frame

    esp_err_t err = rmt_transmit(_channel, _encoder, _symbols.data(),
                                 _symbols.size() * sizeof(rmt_symbol_word_t),
                                 &tx_cfg);
    if (err != ESP_OK) {
        Serial.printf("[IR] rmt_transmit failed: 0x%x\n",
                      static_cast<int>(err));
        return;
    }

    // Block until the hardware has clocked out the whole frame.
    rmt_tx_wait_all_done(_channel, 200);
}

}  // namespace zigree
