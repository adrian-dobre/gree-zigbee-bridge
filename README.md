# Zigree — Gree G10 Zigbee-to-IR bridge

A Zigbee-to-IR bridge for a **Gree G10 air conditioner** (remote **YT1F**),
running on a **Seeed XIAO ESP32-C6**. The device joins your Zigbee network and
presents the AC as a standard Heating/Cooling unit so it can be driven from
**Zigbee2MQTT**, **Apple Home** (via Z2M + HomeKit), or any Zigbee coordinator.

It transmits Gree YT1F infrared frames to control the unit and reads room
temperature/humidity from an SHT4x sensor over I2C.

## Hardware

| Function          | XIAO ESP32-C6 pad | GPIO    |
| ----------------- | ----------------- | ------- |
| IR blaster signal | D9                | GPIO20  |
| Sensor I2C SDA    | D4                | GPIO22  |
| Sensor I2C SCL    | D5                | GPIO23  |
| BOOT button       | —                 | GPIO9   |

Sensor: Sensirion **SHT4x** (SHT40/SHT41) on I2C address `0x44`.

All pins and feature flags live in [include/Config.h](include/Config.h).

## Project layout

| File                                                       | Responsibility                                       |
| ---------------------------------------------------------- | ---------------------------------------------------- |
| [include/Config.h](include/Config.h)                       | Pins, feature flags, Zigbee identity                 |
| [include/GreeAcState.h](include/GreeAcState.h)             | Hardware-neutral AC state model                      |
| [src/GreeClimateEndpoint.cpp](src/GreeClimateEndpoint.cpp) | Zigbee endpoint: clusters + ZCL ↔ state mapping      |
| [src/GreeIrController.cpp](src/GreeIrController.cpp)       | Turns AC state into YT1F IR frames                   |
| [src/GreeYtProtocol.cpp](src/GreeYtProtocol.cpp)           | Gree YT1F protocol encoder                           |
| [src/IrTransmitter.cpp](src/IrTransmitter.cpp)             | Native ESP32-C6 RMT IR transmitter (38 kHz carrier)  |
| [src/ClimateSensor.cpp](src/ClimateSensor.cpp)             | SHT4x temperature/humidity reader                    |
| [src/ZigbeeNetwork.cpp](src/ZigbeeNetwork.cpp)             | Network-state reset helpers                          |
| [src/main.cpp](src/main.cpp)                               | Orchestration only                                   |

The IR protocol is ported from the
[gree-g10-ac-rc](https://github.com/adrian-dobre/gree-g10-ac-rc) project and the
`GreeYTHeatpumpIR` encoder, but transmitted via the ESP32-C6's native RMT
peripheral instead of a third-party IR library.

## Feature flags

Defined in [include/Config.h](include/Config.h); override from `platformio.ini`
build flags if needed:

| Flag        | Default | Effect                                    |
| ----------- | ------- | ----------------------------------------- |
| `USE_IR`    | `1`     | Transmit AC commands over IR              |
| `USE_IFEEL` | `0`     | Report room temperature to the AC (iFeel) |

## Exposed Zigbee clusters

Single endpoint (`1`), manufacturer `Zigree`, model `GreeG10-YT1F`:

| Cluster                       | ID       | Purpose                                          |
| ----------------------------- | -------- | ------------------------------------------------ |
| On/Off                        | `0x0006` | Power on/off                                     |
| Thermostat                    | `0x0201` | System mode (heat/cool/auto/dry/fan), setpoints, current temperature, AC louver (vertical swing) |
| Fan control                   | `0x0202` | Fan speed (auto/low/medium/high/turbo)           |
| Temperature measurement       | `0x0402` | Current room temperature (read-only)             |
| Relative humidity measurement | `0x0405` | Current room humidity (read-only)                |

## Build & flash

This project uses [PlatformIO](https://platformio.org/) with the
[pioarduino](https://github.com/pioarduino/platform-espressif32) ESP32 platform.

```sh
# Build
pio run

# Flash + (optionally) open the serial monitor
pio run --target upload
pio device monitor -b 115200
```

> If `pio` is not on your `PATH`, use the full path to the PlatformIO Python env,
> e.g. `~/.platformio/penv/bin/pio run`.

Serial output is `115200` baud.

## Pairing with Zigbee2MQTT

1. Enable joining in Zigbee2MQTT (**Permit join**).
2. Power up / reset the board. On first boot after flashing, the firmware clears
   any stored Zigbee network state and starts a fresh join.
3. The device appears as model `GreeG10-YT1F` (vendor `Zigree`).
4. Because this is a custom device, install the included **external converter**
   so Z2M renders the climate/fan/swing controls — see below. Without it, Z2M
   still pairs the device but only shows raw clusters.

### Re-pairing / factory reset

The board clears its stored Zigbee state automatically **once per flashed
firmware image**. To force a clean re-join at any other time, **hold the BOOT
button (GPIO9) while resetting/powering on** the board — the stored network
state is erased and the device re-commissions.

> If you remove and re-add the device in Z2M, do a BOOT-button reset as well so
> the device and the coordinator agree on a fresh network/reporting state.
> Note: changing the model string in firmware also requires re-pairing.

## Zigbee2MQTT external converter

A converter is provided at [z2m/zigree.js](z2m/zigree.js). It maps the device's
clusters to Z2M exposes (climate, fan, vertical swing/louver, on/off, current
temperature, current humidity).

1. Copy `z2m/zigree.js` into your Zigbee2MQTT data directory.
2. Reference it from `configuration.yaml`:

   ```yaml
   external_converters:
     - zigree.js
   ```

3. Restart Zigbee2MQTT.

> The converter intentionally does **not** request attribute reporting for the
> thermostat setpoints / system mode. Those attributes are not reportable per
> the ZCL spec, and asking the firmware to report them can crash the underlying
> zboss stack. They are read on demand instead.

## Roadmap

- [x] Zigbee end-device firmware exposing the AC clusters
- [x] Stable join + Z2M integration
- [x] Zigbee2MQTT external converter
- [x] Gree YT1F IR encoder + native RMT transmitter
- [x] SHT4x temperature/humidity sensor
- [ ] Verify IR control against the physical unit
- [ ] Optional iFeel room-temperature reporting to the AC
