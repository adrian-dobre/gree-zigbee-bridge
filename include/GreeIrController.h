#pragma once

#include "GreeAcState.h"
#include "IrTransmitter.h"

namespace greebridge {

// ---------------------------------------------------------------------------
// Drives the Gree AC over infrared. Owns the IR transmitter and the YT1F
// encoder, and exposes a small intent-level API ("apply this state", "report
// this room temperature"). Like the original remote, each command is sent
// twice with a short gap for reliability.
// ---------------------------------------------------------------------------

class GreeIrController {
   public:
    GreeIrController(uint8_t pin, uint32_t carrierHz);

    bool begin();

    // Transmit the given AC state to the unit.
    void apply(const AcState& state);

    // Transmit an "iFeel" room-temperature report (only meaningful if the unit
    // is in iFeel mode).
    void reportRoomTemperature(uint8_t roomTempC);

   private:
    IrTransmitter _tx;
    bool _ready = false;
};

}  // namespace greebridge
