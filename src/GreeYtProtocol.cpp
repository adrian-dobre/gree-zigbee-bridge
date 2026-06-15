#include "GreeYtProtocol.h"

#include <string.h>

namespace greebridge {

namespace {

// --- Protocol byte codes (from GreeYTHeatpumpIR) ---------------------------
constexpr uint8_t GREE_POWER_OFF = 0x00;
constexpr uint8_t GREE_POWER_ON = 0x08;

constexpr uint8_t GREE_MODE_AUTO = 0x00;
constexpr uint8_t GREE_MODE_COOL = 0x01;
constexpr uint8_t GREE_MODE_DRY = 0x02;
constexpr uint8_t GREE_MODE_FAN = 0x03;
constexpr uint8_t GREE_MODE_HEAT = 0x04;

constexpr uint8_t GREE_FAN_AUTO = 0x00;
constexpr uint8_t GREE_FAN1 = 0x10;
constexpr uint8_t GREE_FAN2 = 0x20;
constexpr uint8_t GREE_FAN3 = 0x30;

// Vertical swing positions (byte 4 value when swing == SWING).
constexpr uint8_t GREE_VDIR_AUTO = 0x00;
constexpr uint8_t GREE_VDIR_SWING = 0x01;
constexpr uint8_t GREE_VDIR_UP = 0x02;
constexpr uint8_t GREE_VDIR_MIDDLE = 0x04;
constexpr uint8_t GREE_VDIR_MDOWN = 0x05;
constexpr uint8_t GREE_VDIR_DOWN = 0x06;

constexpr uint8_t GREE_VSWING_BIT = (1 << 6);  // byte 0
constexpr uint8_t GREE_IFEEL_BIT = 0x08;       // byte 5

// byte 2 flags
constexpr uint8_t GREE_TURBO_BIT = (1 << 4);
constexpr uint8_t GREE_LIGHT_BIT = (1 << 5);
constexpr uint8_t GREE_HEALTH_BIT = (1 << 6);

// --- Timings (microseconds), 38 kHz carrier --------------------------------
constexpr uint32_t HDR_MARK = 9000;
constexpr uint32_t HDR_SPACE = 4000;
constexpr uint32_t BIT_MARK = 620;
constexpr uint32_t ONE_SPACE = 1600;
constexpr uint32_t ZERO_SPACE = 540;
constexpr uint32_t MSG_SPACE = 19000;

constexpr uint32_t IFEEL_HDR_MARK = 8200;
constexpr uint32_t IFEEL_HDR_SPACE = 3800;
constexpr uint32_t IFEEL_BIT_MARK = 650;

uint8_t modeCode(Mode mode) {
    switch (mode) {
        case Mode::Auto: return GREE_MODE_AUTO;
        case Mode::Cool: return GREE_MODE_COOL;
        case Mode::Dry: return GREE_MODE_DRY;
        case Mode::Fan: return GREE_MODE_FAN;
        case Mode::Heat: return GREE_MODE_HEAT;
    }
    return GREE_MODE_COOL;
}

uint8_t fanCode(FanSpeed fan) {
    switch (fan) {
        case FanSpeed::Auto: return GREE_FAN_AUTO;
        case FanSpeed::Low: return GREE_FAN1;
        case FanSpeed::Medium: return GREE_FAN2;
        case FanSpeed::High: return GREE_FAN3;
        case FanSpeed::Turbo: return GREE_FAN3;  // max speed + turbo bit (below)
    }
    return GREE_FAN_AUTO;
}

uint8_t swingCode(Swing swing) {
    switch (swing) {
        case Swing::Auto: return GREE_VDIR_AUTO;
        case Swing::Swing: return GREE_VDIR_SWING;
        case Swing::Up: return GREE_VDIR_UP;
        case Swing::Middle: return GREE_VDIR_MIDDLE;
        case Swing::MiddleDown: return GREE_VDIR_MDOWN;
        case Swing::Down: return GREE_VDIR_DOWN;
    }
    return GREE_VDIR_AUTO;
}

}  // namespace

void GreeYtProtocol::buildPayload(const AcState& state, uint8_t buffer[9]) {
    memset(buffer, 0, 9);

    uint8_t mode = modeCode(state.mode);
    uint8_t fan = fanCode(state.fan);

    // Temperature: valid 16..30, encoded as (tempC - 16). AUTO forces 25; DRY
    // forces fan speed 1 (per the original remote's behaviour).
    uint8_t tempC = state.activeTempC();
    if (tempC < 16) tempC = 16;
    if (tempC > 30) tempC = 30;
    if (state.mode == Mode::Auto) tempC = 25;
    if (state.mode == Mode::Dry) fan = GREE_FAN1;

    uint8_t power = state.power ? GREE_POWER_ON : GREE_POWER_OFF;
    uint8_t vdir = swingCode(state.swing);
    bool turbo = (state.fan == FanSpeed::Turbo);

    // byte 0: fan | mode | power
    buffer[0] = fan | mode | power;
    // byte 1: temperature
    buffer[1] = static_cast<uint8_t>(tempC - 16);
    // byte 2: LIGHT + HEALTH always on for YT; TURBO when max fan is selected
    buffer[2] = GREE_LIGHT_BIT | GREE_HEALTH_BIT;
    if (turbo) buffer[2] |= GREE_TURBO_BIT;
    // byte 3: fixed
    buffer[3] = 0x50;
    // Vertical louver. Continuous swing is flagged by bit 6 of byte 0; the
    // value also rides in byte 5 (the only transmitted byte that carries it on
    // this unit). Fixed positions (UP..DOWN) use byte 5 alone, without the
    // swing bit, exactly like the related YAA/YAC remotes. byte 4's slot is not
    // transmitted for YT (only the fixed '010' bits are), so it is left unset.
    if (vdir == GREE_VDIR_SWING) {
        buffer[0] |= GREE_VSWING_BIT;
        buffer[5] = vdir;
    } else if (vdir != GREE_VDIR_AUTO) {
        buffer[5] = vdir;
    }
    // bytes 6..7 default 0 (no iFeel in a normal state frame)

    buffer[8] = checksum(buffer);
}

uint8_t GreeYtProtocol::checksum(const uint8_t buffer[9]) {
    return ((((buffer[0] & 0x0F) + (buffer[1] & 0x0F) + (buffer[2] & 0x0F) +
              (buffer[3] & 0x0F) + ((buffer[5] & 0xF0) >> 4) +
              ((buffer[6] & 0xF0) >> 4) + ((buffer[7] & 0xF0) >> 4) + 0x0A) &
             0x0F)
            << 4) |
           (buffer[7] & 0x0F);
}

void GreeYtProtocol::sendByteLsbFirst(uint8_t value, IrTransmitter& tx) {
    for (uint8_t bit = 0; bit < 8; bit++) {
        tx.mark(BIT_MARK);
        tx.space((value & (1 << bit)) ? ONE_SPACE : ZERO_SPACE);
    }
}

void GreeYtProtocol::encodeState(const AcState& state, IrTransmitter& tx) {
    uint8_t buffer[9];
    buildPayload(state, buffer);

    tx.startFrame();

    // Header
    tx.mark(HDR_MARK);
    tx.space(HDR_SPACE);

    // Payload part #1: bytes 0..3
    for (uint8_t i = 0; i < 4; i++) sendByteLsbFirst(buffer[i], tx);

    // Three fixed separator bits, always '010'
    tx.mark(BIT_MARK);
    tx.space(ZERO_SPACE);
    tx.mark(BIT_MARK);
    tx.space(ONE_SPACE);
    tx.mark(BIT_MARK);
    tx.space(ZERO_SPACE);

    // Message gap
    tx.mark(BIT_MARK);
    tx.space(MSG_SPACE);

    // Payload part #2: bytes 5..8
    for (uint8_t i = 5; i < 9; i++) sendByteLsbFirst(buffer[i], tx);

    // End mark
    tx.mark(BIT_MARK);
    tx.space(0);
}

void GreeYtProtocol::encodeIFeelTemperature(uint8_t roomTempC,
                                            IrTransmitter& tx) {
    uint8_t payload[2] = {roomTempC, 0xA5};

    tx.startFrame();
    tx.mark(IFEEL_HDR_MARK);
    tx.space(IFEEL_HDR_SPACE);

    for (uint8_t i = 0; i < 2; i++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            tx.mark(IFEEL_BIT_MARK);
            tx.space((payload[i] & (1 << bit)) ? ONE_SPACE : ZERO_SPACE);
        }
    }

    tx.mark(IFEEL_BIT_MARK);
    tx.space(0);
}

}  // namespace greebridge
