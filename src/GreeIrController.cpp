#include "GreeIrController.h"

#include <Arduino.h>

#include "GreeYtProtocol.h"

namespace greebridge {

GreeIrController::GreeIrController(uint8_t pin, uint32_t carrierHz)
    : _tx(pin, carrierHz) {}

bool GreeIrController::begin() {
    _ready = _tx.begin();
    return _ready;
}

void GreeIrController::apply(const AcState& state) {
    if (!_ready) return;

    GreeYtProtocol::encodeState(state, _tx);
    _tx.sendFrame();

    Serial.printf(
        "[IR] Sent state: power=%s mode=%u fan=%u swing=%u temp=%uC\n",
        state.power ? "ON" : "OFF", static_cast<unsigned>(state.mode),
        static_cast<unsigned>(state.fan), static_cast<unsigned>(state.swing),
        state.activeTempC());
}

void GreeIrController::reportRoomTemperature(uint8_t roomTempC) {
    if (!_ready) return;
    GreeYtProtocol::encodeIFeelTemperature(roomTempC, _tx);
    _tx.sendFrame();
    Serial.printf("[IR] Sent iFeel room temperature: %uC\n", roomTempC);
}

}  // namespace greebridge
