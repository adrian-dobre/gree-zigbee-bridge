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
    uint8_t targetTempC = 24;  // single target setpoint, valid range 16..30

    // The AC takes a single target temperature per IR frame, used for every
    // mode. It is backed by the thermostat cluster's heating setpoint, the only
    // setpoint ZBOSS allows down to 16 C (the cooling setpoint has a hardcoded
    // 16 C-exclusive floor in the precompiled stack).
    uint8_t activeTempC() const { return targetTempC; }

    bool operator==(const AcState& o) const {
        return power == o.power && mode == o.mode && fan == o.fan &&
               swing == o.swing && activeTempC() == o.activeTempC();
    }
    bool operator!=(const AcState& o) const { return !(*this == o); }
};

}  // namespace greebridge
