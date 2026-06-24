#include "GreeClimateEndpoint.h"

#include <Arduino.h>

#include <cmath>

#include "Config.h"

namespace greebridge {

namespace {

// Measurement bounds (ZCL units: value * 100).
constexpr int16_t kTempMinValue = -4000;  // -40.00 C
constexpr int16_t kTempMaxValue = 10000;  // 100.00 C
constexpr uint16_t kTempTolerance = 100;  // 1.00 C
constexpr uint16_t kHumidityMinValue = 0;
constexpr uint16_t kHumidityMaxValue = 10000;  // 100.00 %

// Minimum cooling setpoint limit (ZCL units: value * 100). ZBOSS validates
// incoming cooling-setpoint writes against the cluster's MinCool/AbsMinCool
// setpoint-limit attributes, whose default floor is 0x0640 (16.00 C); a write
// equal to that floor is rejected, so selecting 16 C in Z2M/Apple Home comes
// back as INVALID_VALUE and never reaches the IR layer. Advertise a slightly
// lower floor (15 C) so 16 C passes validation. This is not user-visible: the
// Z2M converter exposes 16 C as the UI minimum and the IR encoder clamps to
// 16..30 before transmitting.
constexpr int16_t kCoolSetpointMinLimit = 1500;  // 15.00 C

constexpr uint8_t kFanSequence =
    ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_SEQUENCE_LOW_MED_HIGH;
// Cooling + heating so both setpoints and all modes are valid.
constexpr uint8_t kControlSequence =
    ESP_ZB_ZCL_THERMOSTAT_CONTROL_SEQ_OF_OPERATION_COOLING_AND_HEATING_4_PIPES;

int16_t toZclTemperature(float value) {
    return static_cast<int16_t>(lround(value * 100.0f));
}

// --- AcState <-> ZCL mapping ----------------------------------------------

uint8_t toZclSystemMode(const AcState& s) {
    if (!s.power) return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
    switch (s.mode) {
        case Mode::Auto: return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO;
        case Mode::Cool: return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_COOL;
        case Mode::Heat: return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT;
        case Mode::Dry: return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_DRY;
        case Mode::Fan: return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_FAN_ONLY;
    }
    return ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_COOL;
}

// Returns false if the mode value is not one we map (state left unchanged).
bool fromZclSystemMode(uint8_t value, AcState& s) {
    switch (value) {
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF:
            s.power = false;
            return true;
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO:
            s.power = true;
            s.mode = Mode::Auto;
            return true;
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_COOL:
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_PRECOOLING:
            s.power = true;
            s.mode = Mode::Cool;
            return true;
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT:
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_EMERGENCY_HEATING:
            s.power = true;
            s.mode = Mode::Heat;
            return true;
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_DRY:
            s.power = true;
            s.mode = Mode::Dry;
            return true;
        case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_FAN_ONLY:
            s.power = true;
            s.mode = Mode::Fan;
            return true;
        default:
            return false;
    }
}

uint8_t toZclFanMode(FanSpeed fan) {
    switch (fan) {
        case FanSpeed::Auto: return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_AUTO;
        case FanSpeed::Low: return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_LOW;
        case FanSpeed::Medium: return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_MEDIUM;
        case FanSpeed::High: return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_HIGH;
        case FanSpeed::Turbo: return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_SMART;
    }
    return ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_AUTO;
}

FanSpeed fromZclFanMode(uint8_t value) {
    switch (value) {
        case ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_LOW: return FanSpeed::Low;
        case ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_MEDIUM: return FanSpeed::Medium;
        case ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_HIGH: return FanSpeed::High;
        case ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_SMART: return FanSpeed::Turbo;
        default: return FanSpeed::Auto;  // OFF / ON / AUTO
    }
}

uint8_t toZclLouver(Swing swing) {
    switch (swing) {
        case Swing::Swing: return ESP_ZB_ZCL_THERMOSTAT_LOUVER_FULLY_CLOSED;
        case Swing::Down: return ESP_ZB_ZCL_THERMOSTAT_LOUVER_FULLY_OPEN;
        case Swing::Up: return ESP_ZB_ZCL_THERMOSTAT_LOUVER_QUARTER_OPEN;
        case Swing::Middle: return ESP_ZB_ZCL_THERMOSTAT_LOUVER_HALF_OPEN;
        case Swing::MiddleDown:
            return ESP_ZB_ZCL_THERMOSTAT_LOUVER_THREE_QUARTERS_OPEN;
        case Swing::Auto: return ESP_ZB_ZCL_THERMOSTAT_LOUVER_HALF_OPEN;
    }
    return ESP_ZB_ZCL_THERMOSTAT_LOUVER_HALF_OPEN;
}

Swing fromZclLouver(uint8_t value) {
    switch (value) {
        case ESP_ZB_ZCL_THERMOSTAT_LOUVER_FULLY_CLOSED: return Swing::Swing;
        case ESP_ZB_ZCL_THERMOSTAT_LOUVER_FULLY_OPEN: return Swing::Down;
        case ESP_ZB_ZCL_THERMOSTAT_LOUVER_QUARTER_OPEN: return Swing::Up;
        case ESP_ZB_ZCL_THERMOSTAT_LOUVER_HALF_OPEN: return Swing::Middle;
        case ESP_ZB_ZCL_THERMOSTAT_LOUVER_THREE_QUARTERS_OPEN:
            return Swing::MiddleDown;
        default: return Swing::Middle;
    }
}

void addCluster(const char* name, esp_err_t ret) {
    if (ret != ESP_OK) {
        Serial.printf("[Zigbee] add %s cluster failed -> 0x%x\n", name,
                      static_cast<int>(ret));
    }
}

}  // namespace

GreeClimateEndpoint::GreeClimateEndpoint(uint8_t endpoint) : ZigbeeEP(endpoint) {
    _device_id = ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID;
    _cluster_list = nullptr;

    _ep_config = {
        .endpoint = _endpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID,
        .app_device_version = 0,
    };

    _state.power = true;
    _state.mode = Mode::Cool;
    _state.fan = FanSpeed::Auto;
    _state.swing = Swing::Auto;
    _state.coolTempC = kInitialTargetTempC;
    _state.heatTempC = kInitialTargetTempC;

    _localTemperature = toZclTemperature(25.0f);
    _humidity = static_cast<uint16_t>(toZclTemperature(47.0f));
}

void GreeClimateEndpoint::buildClusters() {
    int16_t cool_setpoint = static_cast<int16_t>(_state.coolTempC * 100);
    int16_t heat_setpoint = static_cast<int16_t>(_state.heatTempC * 100);
    uint8_t system_mode = toZclSystemMode(_state);
    uint8_t fan_mode = toZclFanMode(_state.fan);
    uint8_t louver = toZclLouver(_state.swing);
    bool on_off = _state.power;

    esp_zb_thermostat_cfg_t thermostat_cfg = ESP_ZB_DEFAULT_THERMOSTAT_CONFIG();
    thermostat_cfg.thermostat_cfg.local_temperature = _localTemperature;
    thermostat_cfg.thermostat_cfg.occupied_cooling_setpoint = cool_setpoint;
    thermostat_cfg.thermostat_cfg.occupied_heating_setpoint = heat_setpoint;
    thermostat_cfg.thermostat_cfg.control_sequence_of_operation =
        kControlSequence;
    thermostat_cfg.thermostat_cfg.system_mode = system_mode;

    esp_zb_on_off_cluster_cfg_t on_off_cfg = {.on_off = on_off};
    esp_zb_fan_control_cluster_cfg_t fan_cfg = {
        .fan_mode = fan_mode,
        .fan_mode_sequence = kFanSequence,
    };
    esp_zb_temperature_meas_cluster_cfg_t temp_meas_cfg = {
        .measured_value = _localTemperature,
        .min_value = kTempMinValue,
        .max_value = kTempMaxValue,
    };
    esp_zb_humidity_meas_cluster_cfg_t humidity_cfg = {
        .measured_value = static_cast<int16_t>(_humidity),
        .min_value = static_cast<int16_t>(kHumidityMinValue),
        .max_value = static_cast<int16_t>(kHumidityMaxValue),
    };

    _cluster_list = esp_zb_zcl_cluster_list_create();
    if (_cluster_list == nullptr) {
        Serial.println("[Zigbee] Failed to create cluster list");
        return;
    }

    // Basic + Identify (mandatory).
    addCluster("basic", esp_zb_cluster_list_add_basic_cluster(
                            _cluster_list,
                            esp_zb_basic_cluster_create(&thermostat_cfg.basic_cfg),
                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    addCluster("identify",
               esp_zb_cluster_list_add_identify_cluster(
                   _cluster_list,
                   esp_zb_identify_cluster_create(&thermostat_cfg.identify_cfg),
                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // On/Off.
    addCluster("on_off",
               esp_zb_cluster_list_add_on_off_cluster(
                   _cluster_list, esp_zb_on_off_cluster_create(&on_off_cfg),
                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Thermostat, with the optional AC louver position attribute for swing.
    esp_zb_attribute_list_t* thermostat_cluster =
        esp_zb_thermostat_cluster_create(&thermostat_cfg.thermostat_cfg);
    esp_zb_thermostat_cluster_add_attr(
        thermostat_cluster, ESP_ZB_ZCL_ATTR_THERMOSTAT_AC_LOUVER_POSITION_ID,
        &louver);
    // Lower the cooling setpoint floor so 16 C is accepted (see
    // kCoolSetpointMinLimit). MinCool must stay within AbsMinCool, so set both.
    int16_t cool_min_limit = kCoolSetpointMinLimit;
    esp_zb_thermostat_cluster_add_attr(
        thermostat_cluster,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MIN_COOL_SETPOINT_LIMIT_ID,
        &cool_min_limit);
    esp_zb_thermostat_cluster_add_attr(
        thermostat_cluster,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT_ID, &cool_min_limit);
    addCluster("thermostat", esp_zb_cluster_list_add_thermostat_cluster(
                                 _cluster_list, thermostat_cluster,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Fan control.
    addCluster("fan_control",
               esp_zb_cluster_list_add_fan_control_cluster(
                   _cluster_list, esp_zb_fan_control_cluster_create(&fan_cfg),
                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Temperature + humidity measurement (server role, read by the coordinator).
    esp_zb_attribute_list_t* temp_cluster =
        esp_zb_temperature_meas_cluster_create(&temp_meas_cfg);
    esp_zb_temperature_meas_cluster_add_attr(
        temp_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_TOLERANCE_ID,
        (void*)&kTempTolerance);
    addCluster("temp_meas",
               esp_zb_cluster_list_add_temperature_meas_cluster(
                   _cluster_list, temp_cluster,
                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    addCluster("humidity_meas",
               esp_zb_cluster_list_add_humidity_meas_cluster(
                   _cluster_list,
                   esp_zb_humidity_meas_cluster_create(&humidity_cfg),
                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    setEpConfig(_ep_config, _cluster_list);
    Serial.printf("[Zigbee] Cluster list ready: %p\n", _cluster_list);
}

void GreeClimateEndpoint::printState() const {
    Serial.printf(
        "AC State: power=%s mode=%u fan=%u swing=%u cool=%uC heat=%uC | "
        "room=%.2fC humidity=%.2f%%\n",
        _state.power ? "ON" : "OFF", static_cast<unsigned>(_state.mode),
        static_cast<unsigned>(_state.fan), static_cast<unsigned>(_state.swing),
        _state.coolTempC, _state.heatTempC, _localTemperature / 100.0f,
        _humidity / 100.0f);
}

void GreeClimateEndpoint::publishClimate(float temperatureC, float humidityPct) {
    _localTemperature = toZclTemperature(temperatureC);
    _humidity = static_cast<uint16_t>(toZclTemperature(humidityPct));

    setAttr(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &_localTemperature);
    setAttr(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
            ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &_humidity);
    // Mirror room temperature into the thermostat's current temperature.
    setAttr(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
            ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID, &_localTemperature);
}

void GreeClimateEndpoint::notifyStateChanged() {
    if (_onStateChanged) _onStateChanged(_state);
}

bool GreeClimateEndpoint::setAttr(uint16_t cluster_id, uint16_t attr_id,
                                  void* value) {
    esp_zb_zcl_status_t ret = setClusterAttribute(
        cluster_id, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id, value, false);
    if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
        Serial.printf(
            "[Zigbee] Failed to set attr 0x%04x on cluster 0x%04x: 0x%x\n",
            attr_id, cluster_id, ret);
        return false;
    }
    return true;
}

void GreeClimateEndpoint::handleOnOff(uint8_t value) {
    _state.power = value != 0;
    uint8_t system_mode = toZclSystemMode(_state);
    // Keep the thermostat system_mode in sync with the power state.
    setAttr(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
            ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID, &system_mode);
    Serial.printf("[Zigbee] Power -> %s\n", _state.power ? "ON" : "OFF");
    notifyStateChanged();
}

void GreeClimateEndpoint::handleFanMode(uint8_t value) {
    _state.fan = fromZclFanMode(value);
    Serial.printf("[Zigbee] Fan mode -> %u\n", static_cast<unsigned>(_state.fan));
    notifyStateChanged();
}

void GreeClimateEndpoint::handleThermostat(uint16_t attr, uint8_t type,
                                           const void* data) {
    if (attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID &&
        type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
        if (!fromZclSystemMode(*static_cast<const uint8_t*>(data), _state)) {
            return;
        }
        uint8_t on_off = _state.power ? 1 : 0;
        // Keep the On/Off cluster in sync with the selected mode.
        setAttr(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                &on_off);
        Serial.printf("[Zigbee] System mode -> power=%s mode=%u\n",
                      _state.power ? "ON" : "OFF",
                      static_cast<unsigned>(_state.mode));
        notifyStateChanged();
    } else if ((attr ==
                    ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID ||
                attr ==
                    ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) &&
               type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
        int16_t value = *static_cast<const int16_t*>(data);
        long tempC = lround(value / 100.0);
        if (tempC < 16) tempC = 16;
        if (tempC > 30) tempC = 30;
        // The cluster keeps cooling and heating setpoints independently; store
        // whichever one was written. The IR layer later selects the setpoint
        // matching the active mode (see AcState::activeTempC).
        if (attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) {
            _state.heatTempC = static_cast<uint8_t>(tempC);
            Serial.printf("[Zigbee] Heating setpoint -> %uC\n",
                          _state.heatTempC);
        } else {
            _state.coolTempC = static_cast<uint8_t>(tempC);
            Serial.printf("[Zigbee] Cooling setpoint -> %uC\n",
                          _state.coolTempC);
        }
        notifyStateChanged();
    } else if (attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_AC_LOUVER_POSITION_ID &&
               type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
        _state.swing = fromZclLouver(*static_cast<const uint8_t*>(data));
        Serial.printf("[Zigbee] Louver -> %u\n",
                      static_cast<unsigned>(_state.swing));
        notifyStateChanged();
    }
}

void GreeClimateEndpoint::zbAttributeSet(
    const esp_zb_zcl_set_attr_value_message_t* message) {
    if (!message || message->attribute.data.value == nullptr) return;

    const uint16_t cluster = message->info.cluster;
    const uint16_t attr = message->attribute.id;
    const uint8_t type = message->attribute.data.type;
    const void* data = message->attribute.data.value;

    switch (cluster) {
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:
            if (attr == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
                type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                handleOnOff(*static_cast<const uint8_t*>(data));
            }
            break;

        case ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL:
            if (attr == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID &&
                type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                handleFanMode(*static_cast<const uint8_t*>(data));
            }
            break;

        case ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT:
            handleThermostat(attr, type, data);
            break;

        default:
            break;
    }
}

}  // namespace greebridge
