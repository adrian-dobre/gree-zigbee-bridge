#pragma once

#include <stdint.h>

#include "GreeAcState.h"
#include "IrTransmitter.h"

namespace greebridge {

// ---------------------------------------------------------------------------
// Gree YT1F (a.k.a. "GREE_YT") infrared protocol encoder.
//
// Ported from the GreeYTHeatpumpIR implementation in the arduino-heatpumpir
// library. The frame is eight payload bytes plus a checksum nibble, sent twice
// by the controller for reliability. This class only knows how to turn an
// AcState into IR marks/spaces — it has no opinion on when to send.
// ---------------------------------------------------------------------------

class GreeYtProtocol {
   public:
    // Encode the AC state and push the full carrier waveform into the given
    // transmitter (caller invokes sendFrame()).
    static void encodeState(const AcState& state, IrTransmitter& tx);

    // Encode an "iFeel" room-temperature report frame (tells the unit the
    // temperature measured by an external sensor).
    static void encodeIFeelTemperature(uint8_t roomTempC, IrTransmitter& tx);

   private:
    static void buildPayload(const AcState& state, uint8_t buffer[9]);
    static uint8_t checksum(const uint8_t buffer[9]);
    static void sendByteLsbFirst(uint8_t value, IrTransmitter& tx);
};

}  // namespace greebridge
