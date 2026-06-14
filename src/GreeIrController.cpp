#include "GreeIrController.h"

#include <Arduino.h>

#include "GreeYtProtocol.h"

namespace zigree {

namespace {
// The original YT1F remote repeats every command; mirror that for reliability.
constexpr uint8_t kRepeatCount = 2;
constexpr uint32_t kRepeatGapMs = 50;
}  // namespace

GreeIrController::GreeIrController(uint8_t pin, uint32_t carrierHz)
    : _tx(pin, carrierHz) {}

bool GreeIrController::begin() {
    _ready = _tx.begin();
    return _ready;
}

void GreeIrController::apply(const AcState& state) {
    if (!_ready) return;
    for (uint8_t i = 0; i < kRepeatCount; i++) {
        GreeYtProtocol::encodeState(state, _tx);
        _tx.sendFrame();
        if (i + 1 < kRepeatCount) delay(kRepeatGapMs);
    }
    Serial.printf(
        "[IR] Sent state: power=%s mode=%u fan=%u swing=%u temp=%uC\n",
        state.power ? "ON" : "OFF", static_cast<unsigned>(state.mode),
        static_cast<unsigned>(state.fan), static_cast<unsigned>(state.swing),
        state.targetTempC);
}

void GreeIrController::reportRoomTemperature(uint8_t roomTempC) {
    if (!_ready) return;
    GreeYtProtocol::encodeIFeelTemperature(roomTempC, _tx);
    _tx.sendFrame();
    Serial.printf("[IR] Sent iFeel room temperature: %uC\n", roomTempC);
}

}  // namespace zigree
