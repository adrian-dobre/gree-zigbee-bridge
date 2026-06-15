#pragma once

#include <functional>

#include "GreeAcState.h"
#include "Zigbee.h"

namespace greebridge {

// ---------------------------------------------------------------------------
// Zigbee Heating/Cooling endpoint for the Gree AC.
//
// Exposes the clusters Zigbee2MQTT needs to drive the unit and translates
// incoming ZCL attribute writes into a hardware-neutral AcState. Whenever the
// desired state changes it invokes a callback so the orchestration layer can
// transmit the new state over IR. Sensor readings are pushed in via
// publishClimate().
//
//   - On/Off              (0x0006) : power on/off
//   - Thermostat          (0x0201) : system mode, setpoints, local temp, louver
//   - Fan control         (0x0202) : fan speed
//   - Temperature meas.   (0x0402) : room temperature (read-only)
//   - Humidity meas.      (0x0405) : room humidity (read-only)
// ---------------------------------------------------------------------------

class GreeClimateEndpoint : public ZigbeeEP {
   public:
    using StateChangedCallback = std::function<void(const AcState&)>;

    explicit GreeClimateEndpoint(uint8_t endpoint);

    // Build the cluster list with initial attribute values. Must be called
    // BEFORE Zigbee.begin().
    void buildClusters();

    // Register a callback fired after every state change requested by the
    // coordinator.
    void onStateChanged(StateChangedCallback cb) { _onStateChanged = cb; }

    // Push the latest room readings to the stack (after Zigbee.begin()).
    void publishClimate(float temperatureC, float humidityPct);

    const AcState& state() const { return _state; }
    void printState() const;

    void zbAttributeSet(
        const esp_zb_zcl_set_attr_value_message_t* message) override;

   private:
    AcState _state;

    // ZCL-format mirrors used to seed the cluster attributes at build time.
    int16_t _localTemperature;  // value * 100
    uint16_t _humidity;         // value * 100

    void notifyStateChanged();
    bool setAttr(uint16_t cluster_id, uint16_t attr_id, void* value);

    void handleOnOff(uint8_t value);
    void handleFanMode(uint8_t value);
    void handleThermostat(uint16_t attr, uint8_t type, const void* data);

    StateChangedCallback _onStateChanged;
};

}  // namespace greebridge
