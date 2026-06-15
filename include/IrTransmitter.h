#pragma once

#include <stdint.h>

#include <vector>

#include <driver/rmt_tx.h>

namespace greebridge {

// ---------------------------------------------------------------------------
// IR transmitter for consumer-IR remote frames. Frames are described as an
// alternating sequence of "mark" (carrier bursts) and "space" (silence)
// durations in microseconds, which is the natural vocabulary of consumer-IR
// protocols.
//
// The 38 kHz carrier AND every mark/space duration are produced entirely by
// the ESP32 RMT peripheral (hardware), so the waveform is immune to the timing
// jitter that a software bit-banged sender (e.g. IRremoteESP8266, which busy-
// waits with delayMicroseconds) suffers while the Zigbee radio stack services
// interrupts. That hardware timing is what makes transmission reliable on a
// busy Zigbee end device.
//
// mark()/space() accumulate the raw timing buffer; sendFrame() packs it into
// RMT symbols and clocks it out in one hardware-timed shot.
// ---------------------------------------------------------------------------

class IrTransmitter {
   public:
    IrTransmitter(uint8_t pin, uint32_t carrierHz);

    // Initialise the RMT channel + carrier. Returns false on failure.
    bool begin();

    // Begin composing a frame. Each mark()/space() appends one segment.
    void startFrame();
    void mark(uint32_t micros);
    void space(uint32_t micros);

    // Transmit the composed frame (blocking until complete).
    void sendFrame();

   private:
    void push(uint8_t level, uint32_t micros);

    uint8_t _pin;
    uint32_t _carrierHz;
    bool _ready = false;

    rmt_channel_handle_t _channel = nullptr;
    rmt_encoder_handle_t _encoder = nullptr;

    // Flat timing buffer: parallel duration (microseconds) + level (1 = carrier
    // burst, 0 = silence). Packed into RMT symbols in sendFrame().
    std::vector<uint16_t> _durations;
    std::vector<uint8_t> _levels;
    std::vector<rmt_symbol_word_t> _symbols;
};

}  // namespace greebridge
