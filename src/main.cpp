#include <Arduino.h>

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected"
#endif

#include <cmath>

#include "ClimateSensor.h"
#include "Config.h"
#include "GreeClimateEndpoint.h"
#include "GreeIrController.h"
#include "Zigbee.h"
#include "ZigbeeNetwork.h"

// ---------------------------------------------------------------------------
// GreeBridge — Zigbee bridge for a Gree air conditioner (remote YT1F / G10).
//
// Orchestration only. It wires together the three components, each of which
// lives in its own module:
//   - GreeClimateEndpoint : what the Zigbee coordinator sees (clusters/state)
//   - GreeIrController     : what the AC sees (YT1F infrared frames)
//   - ClimateSensor        : room temperature/humidity (SHT4x or simulated)
// ---------------------------------------------------------------------------

using namespace greebridge;

static GreeClimateEndpoint greeEndpoint(kZigbeeEndpoint);
static GreeIrController irController(kIrTxPin, kIrCarrierHz);
static ClimateSensor climateSensor;

// Pending IR transmission. State changes are coalesced: the latest desired
// state is stashed here and only sent once it has been stable for
// kIrDebounceMs, so dragging a slider produces one IR frame instead of many.
static AcState pendingState;
static bool hasPendingState = false;
static uint32_t lastStateChangeMs = 0;

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nStarting GreeBridge (Gree G10 / YT1F Zigbee bridge)...");
    delay(2000);

    maybeResetZigbeeNetwork();

#if USE_IR
    irController.begin();
#endif
    climateSensor.begin();

    // Coalesce rapid desired-state changes; the IR frame is transmitted from
    // loop() once the state has settled (see kIrDebounceMs).
    greeEndpoint.onStateChanged([](const AcState& state) {
        pendingState = state;
        hasPendingState = true;
        lastStateChangeMs = millis();
    });

    greeEndpoint.buildClusters();
    greeEndpoint.setManufacturerAndModel(kManufacturer, kModel);

    bool endpoint_added = Zigbee.addEndpoint(&greeEndpoint);
    Serial.printf("[Zigbee] addEndpoint returned %d\n", endpoint_added);

    Zigbee.setRebootOpenNetwork(kRebootOpenNetworkSeconds);

    if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee failed to start, restarting...");
        delay(1000);
        ESP.restart();
    }

    Serial.println("Zigbee end device started");

    // Accept the 16 C cooling setpoint; ZBOSS hardcodes a 16 C floor otherwise.
    greeEndpoint.installSetpointCheckOverride();

    greeEndpoint.printState();
    Serial.println("Waiting to connect to a Zigbee network...");
}

void loop() {
    static bool was_connected = false;
    static uint32_t last_update = 0;

    bool connected = Zigbee.connected();
    if (connected && !was_connected) {
        Serial.println("Connected to Zigbee network");
    }
    was_connected = connected;

    // Flush a debounced IR command once the desired state has settled.
    if (hasPendingState && millis() - lastStateChangeMs >= kIrDebounceMs) {
        hasPendingState = false;
#if USE_IR
        irController.apply(pendingState);
#endif
    }

    if (connected && millis() - last_update > kSensorPublishIntervalMs) {
        last_update = millis();

        ClimateReading reading;
        if (climateSensor.read(reading)) {
            greeEndpoint.publishClimate(reading.temperatureC,
                                        reading.humidityPct);
#if USE_IFEEL && USE_IR
            irController.reportRoomTemperature(
                static_cast<uint8_t>(lround(reading.temperatureC)));
#endif
        }
        greeEndpoint.printState();
    }
    delay(50);
}
