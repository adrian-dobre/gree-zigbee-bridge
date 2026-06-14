#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Zigree — a Zigbee bridge for Gree air conditioners (remote YT1F / G10).
//
// Central place for the few hardware- and integration-specific values. Tweak
// the pins and feature flags here when wiring up the IR blaster and the
// temperature/humidity sensor.
// ---------------------------------------------------------------------------

namespace zigree {

// --- Zigbee identity -------------------------------------------------------
// NOTE: changing the model string requires re-pairing the device in
// Zigbee2MQTT (the external converter matches on this exact string).
inline constexpr char kManufacturer[] = "Zigree";
inline constexpr char kModel[] = "GreeG10-YT1F";

inline constexpr uint8_t kZigbeeEndpoint = 1;

// How long the network stays open for joining after a reboot, in seconds.
inline constexpr uint16_t kRebootOpenNetworkSeconds = 180;

// --- Pins (Seeed XIAO ESP32-C6) -------------------------------------------
// BOOT button (active low). Hold it during reset/power-up to clear the stored
// Zigbee network state and force a fresh pairing.
inline constexpr uint8_t kBootButtonPin = 9;

// IR blaster signal pin (drives the IR LED, via a transistor). D9 / GPIO20,
// matching the reference gree-g10-ac-rc wiring. Non-inverted: carrier-on
// drives the pin high.
inline constexpr uint8_t kIrTxPin = 20;
inline constexpr uint32_t kIrCarrierHz = 38000;  // Gree uses a 38 kHz carrier.

// I2C bus for the SHT4x temperature/humidity sensor. D4 = SDA, D5 = SCL.
inline constexpr uint8_t kI2cSdaPin = 22;
inline constexpr uint8_t kI2cSclPin = 23;

// --- Feature flags ---------------------------------------------------------
// While 1, every AC state change is encoded and transmitted over IR. Harmless
// to leave on even without a blaster attached (the pin simply toggles).
#ifndef USE_IR
#define USE_IR 1
#endif

// Send the measured room temperature to the AC as an "iFeel" frame so the unit
// regulates against the bridge's SHT4x sensor instead of its own internal one.
#ifndef USE_IFEEL
#define USE_IFEEL 0
#endif

// --- Timing ----------------------------------------------------------------
// How often the sensor is read and its readings published, in ms.
inline constexpr uint32_t kSensorPublishIntervalMs = 10000;

// Debounce window for outgoing IR commands, in ms. Rapid state changes (e.g.
// dragging the fan-speed slider in Apple Home) collapse into a single IR frame
// sent once the desired state has been stable for this long.
inline constexpr uint32_t kIrDebounceMs = 400;

// --- Initial AC state ------------------------------------------------------
inline constexpr uint8_t kInitialTargetTempC = 24;  // 16..30

}  // namespace zigree
