# Zwift Ride HID Bridge

> **Status: Milestone 0, compile-only and nonfunctional.** The firmware builds, but it does not yet perform the Ride handshake, decode controls, or advertise a BLE HID keyboard. Do not flash it expecting a working bridge.

This repository is the starting point for an ESPHome external component that will turn the integrated Zwift Ride controller pair into a stateful BLE keyboard. The first use case is Delta on iPadOS, but the input model and mappings are emulator/application-independent. It targets the Seeed Studio XIAO ESP32-S3.

The project is downstream of [Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard](https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard). Zword established the Ride connection, `RideOn` handshake, haptics, and BLE-keyboard bridge that this project adapts. Preserve that attribution in forks and distributions.

The intended implementation is designed around the user's existing ESPHome Device Builder:

1. ESPHome supplies Wi-Fi, secrets, logs, encrypted Home Assistant API, safe mode, and native OTA.
2. ESPHome will own one Bluedroid instance and its existing GATT client will connect to Ride Left; the in-repository `zwift_ride_hid` component will add a broker-aware HID keyboard peripheral without reinitializing Bluetooth.
3. Ride Left tunnels the Right controller's input, so the finished bridge should normally need only two BLE links in total.
4. Every known discrete button and both directions of both analog brake/steering levers will receive stable semantic IDs and independently bindable HID keys.
5. Complete keyboard reports will provide real key-down/key-up state, holds, chords, duplicate-mapping safety, and deterministic release-all behavior.

The repository currently contains the reviewed plan, protocol inventory, a minimal compile-only component scaffold, and a XIAO reference configuration. The scaffold configures ESPHome's BLE client but does not yet perform the Ride handshake, decode inputs, or advertise a HID keyboard. Start with [PLAN.md](PLAN.md), then see the [Delta mapping guide](docs/delta-mapping.md), [protocol inventory](docs/protocol.md), [hardware checklist](docs/hardware-test-checklist.md), and [ADR 0001](docs/decisions/0001-esphome-external-component.md).

The scaffold configuration and complete ESP32-S3 firmware compile have been verified with ESPHome `2026.7.4`. The baseline image uses approximately 41% of internal RAM and 32% of an OTA application partition before the HID implementation is added.

## Updating from ESPHome Device Builder

The intended installed-device configuration points `external_components` at this Git repository using a full 40-character commit SHA. To update, change that one SHA, compile, and install OTA. To roll back, restore the previous SHA and compile again. See [devices/README.md](devices/README.md) for the exact development and deployment snippets.

Do not load a second keyboard component, NimBLE-Arduino, or any code that initializes Bluetooth or registers global callbacks. `zwift_ride_hid` must attach only through ESPHome's Bluedroid event brokers. Leave `bluetooth_proxy` and unrelated generic BLE services disabled in the first release to preserve connection slots and memory.

For local validation with [uv](https://docs.astral.sh/uv/), copy `devices/secrets.example.yaml` to the ignored `devices/secrets.yaml`, then run `uv run esphome config devices/zwift-ride-hid-bridge.yaml` or `uv run esphome compile devices/zwift-ride-hid-bridge.yaml`.

## Primary outcome

After one initial USB flash, normal updates install over Wi-Fi from Device Builder. The end-to-end target is a 45-minute keyboard-driven session that supports holds, chords, every Ride button, thresholded lever actions, and a controller sleep/wake cycle without rebooting the ESP32 or leaving a key held. The default `delta_emulator` profile matches DeltaCore's bundled keyboard defaults and targets Delta's NES, SNES, GB/GBC, GBA, DS, and Genesis systems; N64 is deliberately deferred because it needs a separate analog/C-button design.

## Licensing and provenance

This repository is licensed under `GPL-3.0-only`; see [LICENSE](LICENSE). It is intentionally GPL rather than MIT because adapting GPL-3.0 upstream code requires the distributed derivative to remain under GPL-compatible terms. Record the exact upstream commit and adapted files in `NOTICE.md` before importing source code.

Relevant sources and references:

- [Zword upstream](https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard) — GPL-3.0 upstream and primary project provenance
- [Zwift Ride protocol notes](https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/)
- [ESPHome external components](https://esphome.io/components/external_components/)
- [ESPHome OTA and safe mode](https://esphome.io/components/ota/)
- [ESPHome BLE HID keyboard reference](https://github.com/markusg1234/ESPHome-espidf_ble_keyboard) — GPL-3.0 implementation reference; not yet imported
- [Espressif BLE HID device example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/ble_hid_device_demo)
- [Delta controller mapping guide](https://faq.deltaemulator.com/using-delta/controllers)
- [Seeed XIAO ESP32-S3 hardware guide](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
