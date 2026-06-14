#pragma once

#include <stdint.h>

#include <SensirionI2cSht4x.h>

namespace zigree {

struct ClimateReading {
    float temperatureC;
    float humidityPct;
};

// ---------------------------------------------------------------------------
// Room temperature/humidity source: a Sensirion SHT4x read over I2C. Reads are
// polled (no background task) from the main loop's publish interval.
// ---------------------------------------------------------------------------

class ClimateSensor {
   public:
    bool begin();

    // Fetch the latest reading. Returns false if the sensor read failed.
    bool read(ClimateReading& out);

   private:
    SensirionI2cSht4x _sensor;
    bool _ready = false;
};

}  // namespace zigree
