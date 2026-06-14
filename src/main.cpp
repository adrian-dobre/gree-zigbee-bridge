#include <Arduino.h>

#include <cmath>
#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected"
#endif

#include "Zigbee.h"

static constexpr uint8_t ZIGBEE_AC_ENDPOINT = 1;
static constexpr float kInitialTemperatureC = 25.6f;
static constexpr float kInitialHumidityPct = 47.2f;
static constexpr float kTargetTemperatureC = 24.0f;
static constexpr float kTargetHumidityPct = 45.0f;
static constexpr uint8_t kInitialLouverPosition =
    ESP_ZB_ZCL_THERMOSTAT_LOUVER_HALF_OPEN;
static constexpr uint8_t kInitialFanMode = ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_AUTO;
static constexpr uint8_t kInitialFanSequence =
    ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_SEQUENCE_LOW_MED_HIGH;
static constexpr uint8_t kInitialOnOff = 1;
static constexpr uint8_t kInitialSystemMode =
    ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_COOL;
static constexpr uint8_t kInitialControlSequence =
    ESP_ZB_ZCL_THERMOSTAT_CONTROL_SEQ_OF_OPERATION_COOLING_ONLY;

class GreeACEndpoint : public ZigbeeEP {
   public:
    explicit GreeACEndpoint(uint8_t endpoint) : ZigbeeEP(endpoint) {
        _device_id = ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID;

        _cluster_list = nullptr;

        _ep_config = {
            .endpoint = _endpoint,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID,
            .app_device_version = 0,
        };

        _on_off = kInitialOnOff;
        _system_mode = kInitialSystemMode;
        _fan_mode = kInitialFanMode;
        _louver_position = kInitialLouverPosition;
        _cooling_setpoint = toZclTemperature(kTargetTemperatureC);
        _heating_setpoint = toZclTemperature(kTargetTemperatureC);
        _local_temperature = toZclTemperature(kInitialTemperatureC);
        _humidity = toZclHumidity(kInitialHumidityPct);
    }

    void createClusterList() {
        esp_zb_thermostat_cfg_t thermostat_cfg =
            ESP_ZB_DEFAULT_THERMOSTAT_CONFIG();

        thermostat_cfg.thermostat_cfg.local_temperature = _local_temperature;
        thermostat_cfg.thermostat_cfg.occupied_cooling_setpoint =
            _cooling_setpoint;
        thermostat_cfg.thermostat_cfg.occupied_heating_setpoint =
            _heating_setpoint;
        thermostat_cfg.thermostat_cfg.control_sequence_of_operation =
            _control_sequence;
        thermostat_cfg.thermostat_cfg.system_mode = _system_mode;

        esp_zb_on_off_cluster_cfg_t on_off_cfg = {
            .on_off = static_cast<bool>(_on_off),
        };
        esp_zb_fan_control_cluster_cfg_t fan_cfg = {
            .fan_mode = _fan_mode,
            .fan_mode_sequence = kInitialFanSequence,
        };
        esp_zb_temperature_meas_cluster_cfg_t temp_meas_cfg = {
            .measured_value = _local_temperature,
            .min_value = -4000,
            .max_value = 10000,
        };
        esp_zb_humidity_meas_cluster_cfg_t humidity_cfg = {
            .measured_value = _humidity,
            .min_value = 0,
            .max_value = 10000,
        };

        _cluster_list = esp_zb_zcl_cluster_list_create();
        if (_cluster_list == nullptr) {
            Serial.println("[Zigbee] Failed to create cluster list");
            return;
        }
        Serial.printf("[Zigbee] cluster_list created: %p\n", _cluster_list);
        esp_err_t ret = ESP_OK;
        ret = esp_zb_cluster_list_add_basic_cluster(
            _cluster_list,
            esp_zb_basic_cluster_create(&thermostat_cfg.basic_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        Serial.printf("[Zigbee] add_basic_cluster -> 0x%04x\n", (int)ret);
        ret = esp_zb_cluster_list_add_identify_cluster(
            _cluster_list,
            esp_zb_identify_cluster_create(&thermostat_cfg.identify_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        Serial.printf("[Zigbee] add_identify_cluster(server) -> 0x%04x\n",
                      (int)ret);
        ret = esp_zb_cluster_list_add_identify_cluster(
            _cluster_list,
            esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY),
            ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
        Serial.printf("[Zigbee] add_identify_cluster(client) -> 0x%04x\n",
                      (int)ret);
        ret = esp_zb_cluster_list_add_on_off_cluster(
            _cluster_list, esp_zb_on_off_cluster_create(&on_off_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        Serial.printf("[Zigbee] add_on_off_cluster -> 0x%04x\n", (int)ret);
        ret = esp_zb_cluster_list_add_thermostat_cluster(
            _cluster_list,
            esp_zb_thermostat_cluster_create(&thermostat_cfg.thermostat_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        Serial.printf("[Zigbee] add_thermostat_cluster -> 0x%04x\n", (int)ret);
        ret = esp_zb_cluster_list_add_fan_control_cluster(
            _cluster_list, esp_zb_fan_control_cluster_create(&fan_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        Serial.printf("[Zigbee] add_fan_control_cluster -> 0x%04x\n", (int)ret);
        ret = esp_zb_cluster_list_add_temperature_meas_cluster(
            _cluster_list,
            esp_zb_temperature_meas_cluster_create(&temp_meas_cfg),
            ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
        Serial.printf("[Zigbee] add_temperature_meas_cluster -> 0x%04x\n",
                      (int)ret);
        esp_zb_attribute_list_t* humidity_cluster =
            esp_zb_humidity_meas_cluster_create(&humidity_cfg);
        ret = esp_zb_cluster_list_add_humidity_meas_cluster(
            _cluster_list, humidity_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
        Serial.printf("[Zigbee] add_humidity_meas_cluster -> 0x%04x\n",
                      (int)ret);
        setEpConfig(_ep_config, _cluster_list);
        Serial.printf("[Zigbee] setEpConfig done, cluster_list=%p\n",
                      _cluster_list);
        checkClusters();
    }

    void initAttributes() {
        int16_t temp_min = -4000;
        int16_t temp_max = 10000;
        uint16_t temp_tolerance = 100;
        uint16_t humidity_min = 0;
        uint16_t humidity_max = 10000;

        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &_on_off);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
                        &_system_mode);
        updateAttribute(
            ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
            ESP_ZB_ZCL_ATTR_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION_ID,
            &_control_sequence);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID,
                        &_cooling_setpoint);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID,
                        &_heating_setpoint);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID,
                        &_local_temperature);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_AC_LOUVER_POSITION_ID,
                        &_louver_position);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL,
                        ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, &_fan_mode);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL,
                        ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_SEQUENCE_ID,
                        (void*)&kInitialFanSequence);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                        &_local_temperature);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID,
                        &temp_min);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID,
                        &temp_max);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_TOLERANCE_ID,
                        &temp_tolerance);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
                        &_humidity);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MIN_VALUE_ID,
                        &humidity_min);
        updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MAX_VALUE_ID,
                        &humidity_max);
    }

    void printState() const {
        float current_temp = _local_temperature / 100.0f;
        float current_humidity = _humidity / 100.0f;
        float cooling_setpoint = _cooling_setpoint / 100.0f;
        float heating_setpoint = _heating_setpoint / 100.0f;
        Serial.printf(
            "AC State: on=%u, mode=%u, fan=%u, temp=%.2f°C, humidity=%.2f%%, "
            "target_cool=%.2f°C, target_heat=%.2f°C, louver=%u\n",
            _on_off, _system_mode, _fan_mode, current_temp, current_humidity,
            cooling_setpoint, heating_setpoint, _louver_position);
    }

    esp_zb_attribute_list_t* getEndpointClusterAttrList(
        uint16_t cluster_id, uint8_t role_mask) const {
        esp_zb_zcl_cluster_t* cluster =
            esp_zb_zcl_get_cluster(_endpoint, cluster_id, role_mask);
        if (cluster == nullptr) {
            return nullptr;
        }
        return cluster->attr_list;
    }

    void checkClusters() const {
        struct ClusterCheck {
            uint16_t id;
            const char* name;
        } checks[] = {
            {ESP_ZB_ZCL_CLUSTER_ID_BASIC, "Basic"},
            {ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY, "Identify"},
            {ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, "OnOff"},
            {ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT, "Thermostat"},
            {ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, "FanControl"},
            {ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, "TempMeasurement"},
            {ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
             "HumidityMeasurement"}};
        for (auto& c : checks) {
            uint8_t both_mask =
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE | ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE;
            esp_zb_attribute_list_t* cl_runtime =
                getEndpointClusterAttrList(c.id, both_mask);
            if (cl_runtime) {
                Serial.printf("Cluster %s (0x%04x): present at endpoint\n",
                              c.name, c.id);
                continue;
            }
            esp_zb_attribute_list_t* cl_server =
                esp_zb_cluster_list_get_cluster(_cluster_list, c.id,
                                                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            esp_zb_attribute_list_t* cl_client =
                esp_zb_cluster_list_get_cluster(_cluster_list, c.id,
                                                ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
            esp_zb_attribute_list_t* cl_both =
                esp_zb_cluster_list_get_cluster(_cluster_list, c.id, both_mask);
            if (cl_server) {
                Serial.printf(
                    "Cluster %s (0x%04x): present in list as SERVER\n", c.name,
                    c.id);
            } else if (cl_client) {
                Serial.printf(
                    "Cluster %s (0x%04x): present in list as CLIENT\n", c.name,
                    c.id);
            } else if (cl_both) {
                Serial.printf("Cluster %s (0x%04x): present in list as BOTH\n",
                              c.name, c.id);
            } else {
                Serial.printf("Cluster %s (0x%04x): MISSING\n", c.name, c.id);
            }
        }
    }

    void zbAttributeSet(
        const esp_zb_zcl_set_attr_value_message_t* message) override {
        if (!message || message->attribute.data.value == nullptr) {
            return;
        }
        const uint16_t cluster = message->info.cluster;
        const uint16_t attr = message->attribute.id;
        switch (cluster) {
            case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF: {
                if (attr == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
                    message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                    uint8_t value =
                        *(const uint8_t*)message->attribute.data.value;
                    _on_off = value ? 1 : 0;
                    if (!_on_off) {
                        _system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
                    } else if (_system_mode ==
                               ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF) {
                        _system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_COOL;
                    }
                    updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                                    ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
                                    &_system_mode);
                    updateAttribute(cluster, attr, &_on_off);
                    Serial.printf("[Zigbee] On/Off set to %s\n",
                                  _on_off ? "ON" : "OFF");
                }
                break;
            }
            case ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL: {
                if (attr == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID &&
                    message->attribute.data.type ==
                        ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                    uint8_t value =
                        *(const uint8_t*)message->attribute.data.value;
                    if (value <= ESP_ZB_ZCL_FAN_CONTROL_FAN_MODE_SMART) {
                        _fan_mode = value;
                        updateAttribute(cluster, attr, &_fan_mode);
                        Serial.printf("[Zigbee] Fan mode set to %u\n",
                                      _fan_mode);
                    }
                }
                break;
            }
            case ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT: {
                if (attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID &&
                    message->attribute.data.type ==
                        ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                    uint8_t value =
                        *(const uint8_t*)message->attribute.data.value;
                    _system_mode = value;
                    _on_off =
                        (_system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF)
                            ? 0
                            : 1;
                    updateAttribute(cluster, attr, &_system_mode);
                    updateAttribute(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &_on_off);
                    Serial.printf("[Zigbee] System mode set to %u\n",
                                  _system_mode);
                } else if (
                    (attr ==
                         ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID ||
                     attr ==
                         ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) &&
                    message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
                    int16_t value =
                        *(const int16_t*)message->attribute.data.value;
                    if (attr ==
                        ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID) {
                        _cooling_setpoint = value;
                    } else {
                        _heating_setpoint = value;
                    }
                    updateAttribute(cluster, attr, &value);
                    Serial.printf(
                        "[Zigbee] Setpoint %s set to %.2f°C\n",
                        attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID
                            ? "cooling"
                            : "heating",
                        value / 100.0f);
                } else if (
                    attr == ESP_ZB_ZCL_ATTR_THERMOSTAT_AC_LOUVER_POSITION_ID &&
                    message->attribute.data.type ==
                        ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                    uint8_t value =
                        *(const uint8_t*)message->attribute.data.value;
                    _louver_position = value;
                    updateAttribute(cluster, attr, &_louver_position);
                    Serial.printf("[Zigbee] Louver position set to %u\n",
                                  _louver_position);
                }
                break;
            }
            default:
                break;
        }
    }

   private:
    uint8_t _on_off;
    uint8_t _system_mode;
    uint8_t _control_sequence;
    uint8_t _fan_mode;
    uint8_t _louver_position;
    int16_t _cooling_setpoint;
    int16_t _heating_setpoint;
    int16_t _local_temperature;
    uint16_t _humidity;

    static int16_t toZclTemperature(float value) {
        return static_cast<int16_t>(round(value * 100.0f));
    }

    static uint16_t toZclHumidity(float value) {
        return static_cast<uint16_t>(round(value * 100.0f));
    }

    bool updateAttribute(uint16_t cluster_id, uint16_t attr_id, void* value) {
        uint8_t preferred_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
        if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT ||
            cluster_id == ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT) {
            preferred_role = ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE;
        }

        esp_zb_attribute_list_t* cluster = esp_zb_cluster_list_get_cluster(
            _cluster_list, cluster_id, preferred_role);
        if (cluster == nullptr) {
            cluster = getEndpointClusterAttrList(cluster_id, preferred_role);
        }
        if (cluster == nullptr) {
            /* fallback: try the other role */
            uint8_t alt_role =
                (preferred_role == ESP_ZB_ZCL_CLUSTER_SERVER_ROLE)
                    ? ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE
                    : ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
            cluster = esp_zb_cluster_list_get_cluster(_cluster_list, cluster_id,
                                                      alt_role);
            if (cluster == nullptr) {
                cluster = getEndpointClusterAttrList(cluster_id, alt_role);
            }
        }
        if (cluster == nullptr) {
            /* try combined mask (both roles) */
            uint8_t both =
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE | ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE;
            cluster = esp_zb_cluster_list_get_cluster(_cluster_list, cluster_id,
                                                      both);
            if (cluster == nullptr) {
                cluster = getEndpointClusterAttrList(cluster_id, both);
            }
        }
        if (cluster == nullptr) {
            Serial.printf(
                "[Zigbee] Failed to find cluster 0x%04x for attr 0x%04x\n",
                cluster_id, attr_id);
            return false;
        }
        esp_err_t ret = esp_zb_cluster_update_attr(cluster, attr_id, value);
        if (ret != ESP_OK) {
            Serial.printf(
                "[Zigbee] Failed to update attr 0x%04x on cluster 0x%04x: "
                "0x%x\n",
                attr_id, cluster_id, ret);
            return false;
        }
        return true;
    }
};

GreeACEndpoint greeAC(ZIGBEE_AC_ENDPOINT);

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("Waiting 2s for serial monitor...");
    delay(2000);
    Serial.println("Starting Zigbee AC stub...");

    greeAC.createClusterList();
    greeAC.setManufacturerAndModel("GreeZigbee", "GreeACStub");
    bool endpoint_added = Zigbee.addEndpoint(&greeAC);
    Serial.printf("[Zigbee] addEndpoint returned %d\n", endpoint_added);
    Zigbee.setRebootOpenNetwork(180);

    if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee failed to start, restarting...");
        delay(1000);
        ESP.restart();
    }

    Serial.println("Zigbee end device started");
    greeAC.initAttributes();
    greeAC.printState();
    greeAC.checkClusters();
}

void loop() {
    static uint32_t last_print = 0;
    if (millis() - last_print > 10000) {
        last_print = millis();
        greeAC.printState();
    }
    delay(50);
}
