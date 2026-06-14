// Zigbee2MQTT external converter for Zigree — the Gree AC Zigbee-to-IR bridge.
//
// Device firmware: src/main.cpp (manufacturer "Zigree", model "GreeG10-YT1F").
// Single endpoint (1) exposing:
//   genOnOff (0x0006)                 -> power on/off
//   hvacThermostat (0x0201)           -> system mode, cooling/heating setpoints,
//                                        local temperature, AC louver position
//   hvacFanCtrl (0x0202)              -> fan speed
//   msTemperatureMeasurement (0x0402) -> current temperature (read-only)
//   msRelativeHumidity (0x0405)       -> current humidity (read-only)
//
// The Zigbee device keeps exposing everything granularly (e.g. the full
// ac_louver_position enum). On top of that, this converter adds two climate
// features purely for the Apple Home / HomeKit "AC" tile:
//   * fan_mode  -> the HeaterCooler "Fan Speed" slider
//   * swing_mode -> the HeaterCooler oscillate toggle (on = vertical swing,
//     off = louver held in the middle). swing_mode is just a friendly view of
//     the same acLouverPosition attribute, so both controls stay in sync.
//
// Install: copy this file somewhere Zigbee2MQTT can read it and reference it from
// configuration.yaml:
//   external_converters:
//     - zigree.js
// then restart Zigbee2MQTT.

const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;
const ea = exposes.access;

// Fan speeds exposed by Zigree, mapped to ZCL hvacFanCtrl fanMode values.
// "turbo" reuses the SMART (0x06) slot and the firmware decodes it back to the
// AC's turbo / maximum-airflow setting. In Apple Home the climate fan_mode
// feature becomes the HeaterCooler "Fan Speed" slider.
const fanModeMap = {off: 0, low: 1, medium: 2, high: 3, auto: 5, turbo: 6};
const fanModeByValue = Object.fromEntries(
    Object.entries(fanModeMap).map(([k, v]) => [v, k]));

// AC louver positions (ZCL hvacThermostat acLouverPosition) exposed granularly
// by the device. Values are 1-based per the ZCL spec (FULLY_CLOSED=0x01). The
// firmware maps "fully_closed" to continuous swing and the remaining positions
// to fixed louver angles (fully_open = down ... quarter_open = up).
const louverByName = {
    fully_closed: 1,
    fully_open: 2,
    quarter_open: 3,
    half_open: 4,
    three_quarters_open: 5,
};
const louverByValue = Object.fromEntries(
    Object.entries(louverByName).map(([k, v]) => [v, k]));

// Apple Home's oscillate toggle maps to the climate "swing_mode" feature.
//   on  -> louver fully closed (AC swings vertically)
//   off -> louver quarter open (AC stops the louver in the quarter-open angle)
const SWING_LOUVER_ON = louverByName.fully_closed;   // 1 (= continuous swing)
const SWING_LOUVER_OFF = louverByName.quarter_open;  // 3 (fixed angle)

const fzLocal = {
    fan_mode: {
        cluster: 'hvacFanCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.fanMode === undefined) return;
            const mode = fanModeByValue[msg.data.fanMode];
            if (mode === undefined) return;
            return {fan_mode: mode, fan_state: mode === 'off' ? 'OFF' : 'ON'};
        },
    },
    // Publish the granular louver position AND the derived swing_mode so the
    // Apple Home oscillate toggle and the detailed enum stay in sync.
    louver: {
        cluster: 'hvacThermostat',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.acLouverPosition === undefined) return;
            const result = {
                swing_mode:
                    msg.data.acLouverPosition === SWING_LOUVER_ON ? 'on' : 'off',
            };
            const name = louverByValue[msg.data.acLouverPosition];
            if (name !== undefined) result.ac_louver_position = name;
            return result;
        },
    },
};

const tzLocal = {
    fan_mode: {
        key: ['fan_mode'],
        convertSet: async (entity, key, value, meta) => {
            const mode = String(value).toLowerCase();
            const fanMode = fanModeMap[mode];
            if (fanMode === undefined) {
                throw new Error(`Unsupported fan mode: ${value}`);
            }
            await entity.write('hvacFanCtrl', {fanMode});
            return {state: {fan_mode: mode,
                fan_state: mode === 'off' ? 'OFF' : 'ON'}};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read('hvacFanCtrl', ['fanMode']);
        },
    },
    // Granular louver control. The stock tz.thermostat_ac_louver_position uses
    // a 0-based name->value map (fully_closed=0), but the ZCL spec and this
    // firmware are 1-based (FULLY_CLOSED=0x01). Use our own 1-based map so the
    // device does not reject writes with INVALID_VALUE.
    ac_louver_position: {
        key: ['ac_louver_position'],
        convertSet: async (entity, key, value, meta) => {
            const name = String(value).toLowerCase();
            const acLouverPosition = louverByName[name];
            if (acLouverPosition === undefined) {
                throw new Error(`Unsupported ac_louver_position: ${value}`);
            }
            await entity.write('hvacThermostat', {acLouverPosition});
            return {state: {
                ac_louver_position: name,
                swing_mode: acLouverPosition === SWING_LOUVER_ON ? 'on' : 'off',
            }};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read('hvacThermostat', ['acLouverPosition']);
        },
    },
    // Oscillate toggle -> vertical swing on/off, backed by the same
    // acLouverPosition attribute the granular ac_louver_position control uses.
    swing_mode: {
        key: ['swing_mode'],
        convertSet: async (entity, key, value, meta) => {
            const on = String(value).toLowerCase() === 'on';
            const acLouverPosition = on ? SWING_LOUVER_ON : SWING_LOUVER_OFF;
            await entity.write('hvacThermostat', {acLouverPosition});
            return {state: {
                swing_mode: on ? 'on' : 'off',
                ac_louver_position: louverByValue[acLouverPosition],
            }};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read('hvacThermostat', ['acLouverPosition']);
        },
    },
};

const definition = {
    zigbeeModel: ['GreeG10-YT1F'],
    model: 'GreeG10-YT1F',
    vendor: 'Zigree',
    description: 'Gree G10 air conditioner (YT1F remote) Zigbee-to-IR bridge',
    fromZigbee: [
        fz.on_off,
        fz.thermostat,
        fzLocal.fan_mode,
        fzLocal.louver,
        fz.temperature,
        fz.humidity,
    ],
    toZigbee: [
        tz.on_off,
        tz.thermostat_system_mode,
        tz.thermostat_occupied_cooling_setpoint,
        tz.thermostat_occupied_heating_setpoint,
        tz.thermostat_local_temperature,
        tzLocal.ac_louver_position,
        tzLocal.fan_mode,
        tzLocal.swing_mode,
    ],
    exposes: [
        e.switch(),
        e.climate()
            .withLocalTemperature()
            .withSetpoint('occupied_cooling_setpoint', 16, 30, 0.5)
            .withSetpoint('occupied_heating_setpoint', 16, 30, 0.5)
            .withSystemMode(['off', 'auto', 'cool', 'heat', 'dry', 'fan_only'])
            .withFanMode(['off', 'low', 'medium', 'high', 'auto', 'turbo'])
            // Apple Home oscillate toggle: on = vertical swing, off = fixed.
            .withSwingMode(['off', 'on']),
        e.temperature(),
        e.humidity(),
        // The YT1F protocol maps "fully_closed" to continuous vertical swing
        // and the remaining positions to fixed louver angles (fully_open =
        // down, three_quarters_open = middle-down, half_open = middle,
        // quarter_open = up).
        exposes
            .enum('ac_louver_position', ea.ALL, [
                'fully_closed',
                'fully_open',
                'quarter_open',
                'half_open',
                'three_quarters_open',
            ])
            .withDescription(
                'Vertical louver: "fully_closed" enables continuous swing, ' +
                    'the other values set a fixed angle'),
    ],
    configure: async (device, coordinatorEndpoint, logger) => {
        const endpoint = device.getEndpoint(1);

        // Bind the clusters so the coordinator receives reports.
        await reporting.bind(endpoint, coordinatorEndpoint, [
            'genOnOff',
            'hvacThermostat',
            'hvacFanCtrl',
            'msTemperatureMeasurement',
            'msRelativeHumidity',
        ]);

        // Only configure reporting for attributes that are reportable per the
        // ZCL spec. Thermostat setpoints / system mode are NOT reportable and
        // asking the device to report them can crash the zboss stack, so they
        // are polled / read on demand instead.
        await reporting.onOff(endpoint);
        await reporting.thermostatTemperature(endpoint);
        await reporting.temperature(endpoint);
        await reporting.humidity(endpoint);
    },
};

module.exports = definition;
