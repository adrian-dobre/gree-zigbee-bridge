#include "ClimateSensor.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"

namespace zigree {

bool ClimateSensor::begin() {
    Wire.begin(kI2cSdaPin, kI2cSclPin);
    _sensor.begin(Wire, SHT41_I2C_ADDR_44);
    if (_sensor.softReset() != 0) {
        Serial.println("[Sensor] SHT4x soft reset failed");
        _ready = false;
        return false;
    }
    _ready = true;
    Serial.println("[Sensor] SHT4x ready");
    return true;
}

bool ClimateSensor::read(ClimateReading& out) {
    if (!_ready) return false;
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint16_t err = _sensor.measureHighPrecision(temperature, humidity);
    if (err != 0) {
        Serial.printf("[Sensor] SHT4x read error: 0x%04x\n", err);
        return false;
    }
    out.temperatureC = temperature;
    out.humidityPct = humidity;
    return true;
}

}  // namespace zigree
