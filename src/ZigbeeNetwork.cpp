#include "ZigbeeNetwork.h"

#include <Arduino.h>
#include <Preferences.h>

#include "Config.h"
#include "esp_partition.h"

namespace zigree {

namespace {

// Erase the zboss network-state partitions so the device rejoins cleanly.
void eraseZigbeePartitions() {
    const char* names[] = {"zb_storage", "zb_fct"};
    for (const char* name : names) {
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, name);
        if (part == nullptr) {
            Serial.printf("[Zigbee] partition '%s' not found\n", name);
            continue;
        }
        esp_err_t err = esp_partition_erase_range(part, 0, part->size);
        Serial.printf("[Zigbee] erased '%s' (%u bytes): %s\n", name,
                      static_cast<unsigned>(part->size), esp_err_to_name(err));
    }
}

}  // namespace

void maybeResetZigbeeNetwork() {
    pinMode(kBootButtonPin, INPUT_PULLUP);
    bool button_held = (digitalRead(kBootButtonPin) == LOW);

    Preferences prefs;
    prefs.begin("zigree", false);
    bool already_reset = prefs.getBool("zb_cleared", false);

    if (button_held || !already_reset) {
        Serial.printf("[Zigbee] Clearing stored network state (%s)\n",
                      button_held ? "BOOT button held" : "one-time per image");
        eraseZigbeePartitions();
        prefs.putBool("zb_cleared", true);
    }
    prefs.end();
}

}  // namespace zigree
