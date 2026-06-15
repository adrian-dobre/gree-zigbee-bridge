#pragma once

#include <stdint.h>

namespace greebridge {

// ---------------------------------------------------------------------------
// Canonical, hardware-neutral description of the air conditioner's state.
//
// The Zigbee endpoint translates incoming ZCL attribute writes into this
// struct; the IR controller translates this struct into a Gree YT1F frame.
// Keeping a single source of truth here means neither side has to know about
// the other's encoding.
// ---------------------------------------------------------------------------

enum class Mode : uint8_t {
    Auto,
    Cool,
    Dry,
    Fan,
    Heat,
};

enum class FanSpeed : uint8_t {
    Auto,
    Low,
    Medium,
    High,
    Turbo,  // maximum airflow (sets the AC's turbo flag)
};

// Vertical louver control. Beyond continuous swing, the YT1F protocol can pin
// the louver at fixed angles (the Gree VDIR_* positions). We expose continuous
// swing plus four fixed positions, which the Zigbee endpoint maps onto the ZCL
// AC louver enum.
enum class Swing : uint8_t {
    Auto,        // louver held in its default position (no explicit angle)
    Swing,       // continuous up/down movement
    Up,          // fixed: louver raised (VDIR_UP)
    Middle,      // fixed: louver centred (VDIR_MIDDLE)
    MiddleDown,  // fixed: louver below centre (VDIR_MDOWN)
    Down,        // fixed: louver lowered (VDIR_DOWN)
};

struct AcState {
    bool power = false;
    Mode mode = Mode::Cool;
    FanSpeed fan = FanSpeed::Auto;
    Swing swing = Swing::Auto;
    uint8_t coolTempC = 24;  // cooling setpoint, valid range 16..30
    uint8_t heatTempC = 24;  // heating setpoint, valid range 16..30

    // The AC takes a single target temperature per IR frame. The thermostat
    // cluster keeps cooling and heating setpoints independently; pick the one
    // that matches the active mode (heating uses the heat setpoint, every other
    // mode uses the cool setpoint).
    uint8_t activeTempC() const {
        return mode == Mode::Heat ? heatTempC : coolTempC;
    }

    bool operator==(const AcState& o) const {
        return power == o.power && mode == o.mode && fan == o.fan &&
               swing == o.swing && activeTempC() == o.activeTempC();
    }
    bool operator!=(const AcState& o) const { return !(*this == o); }
};

}  // namespace greebridge
