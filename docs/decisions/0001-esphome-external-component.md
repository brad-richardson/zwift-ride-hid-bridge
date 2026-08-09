# ADR 0001: ESPHome-owned Bluedroid with a broker-aware HID component

- Status: Accepted; implementation complete, hardware validation outstanding
- Date: 2026-08-08

## Context

The bridge must act as a BLE GATT client to Zwift Ride Left and a BLE HID peripheral to an iPad. It should update over the network from the user's existing ESPHome Device Builder and recover safely from a bad application boot.

The downloaded Zword fork plan recommends NimBLE end-to-end for a standalone Arduino build. That is a sound standalone choice, but bringing NimBLE-Arduino into an ESPHome/ESP-IDF firmware would add another host lifecycle and would not provide the familiar Device Builder path.

ESPHome already supplies a Bluedroid manager, scanner, and GATT client. It initializes Bluedroid, installs the process-global GAP/GATTC/GATTS callbacks, and fans events out through internal brokers. ESPHome does not currently provide a stock HID keyboard profile.

A current GPL-3.0 ESPHome BLE keyboard project demonstrates iOS pairing and held-key reports, but it cannot be loaded unchanged beside `ble_client`. At revision `21274b03dd424927e35cd67bbb7c9af848daaef5`, its setup reinitializes the Bluetooth controller/Bluedroid and replaces the global GAP/GATTS callbacks. It also handles every security event as a keyboard host and changes global advertising/address state. That would break or interfere with ESPHome scanning and the Ride connection.

## Decision

Use ESPHome 2026.7.4 as the initially pinned operational shell and sole Bluedroid owner. The `zwift_ride_hid` external component:

- uses ESPHome's `esp32_ble_tracker` and `ble_client` for Ride discovery, connection, characteristic access, and reconnection;
- receives HID GAP/GATTS events through ESPHome's BLE callback brokers rather than raw global registration;
- includes only the keyboard, battery, and device-information GATT services, encrypted CCCDs, iOS-compatible security behavior, and stateful hold/release logic needed by this bridge;
- filters GATTS events by HID app/interface and GAP security events by the connected iPad peer;
- defers NVS/flash and other blocking work out of IDF callbacks;
- releases all HID keys and quiesces bridge activity synchronously when OTA begins or shutdown starts.

Do not load the unmodified keyboard component, NimBLE-Arduino, or T-vK. Leave `bluetooth_proxy`, extra BLE clients, and unrelated generic BLE services disabled in version 1 to conserve connection slots and memory.

Use native ESPHome OTA, API encryption, captive-portal recovery, and safe mode. Device Builder fetches `https://github.com/brad-richardson/zwift-ride-hid-bridge` as an external component at a full immutable commit SHA. Local-path loading is only for repository development and CI.

## Consequences

Positive:

- normal updates and rollback remain a one-SHA change in Device Builder;
- Wi-Fi, BLE client, OTA, logging, secrets, API encryption, bonding storage, and boot recovery use ESPHome's lifecycle;
- only one part of the process owns Bluetooth initialization and global callbacks;
- the Ride client can reuse ESPHome's scanning/reconnection machinery;
- the HID reference has already demonstrated iOS pairing and press-and-hold behavior.

Costs and risks:

- the callback-broker APIs are source-level ESPHome APIs and can change, so the project must pin and compile-test an exact ESPHome patch version;
- the useful HID reference is much larger than this bridge needs, so only audited minimal pieces should be adapted;
- Wi-Fi, one GATT client link, one HID server link, and bonding require early RAM/radio endurance tests;
- all adapted GPL code needs exact provenance and preserved notices.

## Alternatives

### Let the external component own Bluedroid

Rejected for the primary plan. It can work if every ESPHome BLE component is omitted, and it resembles the existing keyboard project, but it duplicates low-level lifecycle responsibilities inside ESPHome and requires a custom Ride GATT client. It is less integrated with the user's reason for choosing ESPHome.

### Load the existing ESPHome keyboard component beside `ble_client`

Rejected. Its current implementation initializes Bluedroid and replaces global callbacks, while `ble_client` expects ESPHome's manager to own those same resources. Loading both is unsafe composition.

### ESPHome plus NimBLE-Arduino/T-vK

Rejected. It introduces a second BLE host/lifecycle and an Arduino compatibility dependency into an ESP-IDF configuration.

### Standalone PlatformIO/Arduino plus NimBLE

Viable fallback if ESPHome is abandoned. It follows the downloaded plan closely, but OTA, secrets, recovery, and deployment would become a separate system.

### Rust firmware or a Rust-backed external component

Deferred for version 1. ESPHome's component/code-generation boundary and the Bluedroid event brokers used here are C++, so Rust would add an ESP-IDF/FFI build layer precisely around the most timing- and lifecycle-sensitive code. That is technically possible, but it works against the one-pinned-external-component deployment goal and makes the initial dual-role proof harder to audit. Rust remains reasonable for offline capture tools or an independently tested protocol library after the ESPHome HID path is proven.

### Clean-room MIT implementation

Not selected. An independently written implementation based only on protocol facts and permissive references could potentially be MIT-licensed, but it gives up the lowest-rewrite path. This repository is `GPL-3.0-only` and preserves Zword as its upstream.

## Release validation gate

The complete software candidate was intentionally built before hardware became available. Do not describe a commit as known-good or publish a stable release until one firmware simultaneously demonstrates:

1. ESPHome Wi-Fi, encrypted API, safe mode, and OTA;
2. bonded BLE keyboard input to iPad Notes;
3. Ride-left scan, connect, `RideOn`, and one notification; and
4. clean reboot/reconnect without pairing loss or a stuck key.

## References

- [Zword upstream](https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard)
- [Audited ESPHome BLE keyboard revision](https://github.com/markusg1234/ESPHome-espidf_ble_keyboard/tree/21274b03dd424927e35cd67bbb7c9af848daaef5)
- [Conflicting keyboard setup/callback registration](https://github.com/markusg1234/ESPHome-espidf_ble_keyboard/blob/21274b03dd424927e35cd67bbb7c9af848daaef5/components/espidf_ble_keyboard/espidf_ble_keyboard.cpp#L1861-L1926)
- [ESPHome external components](https://esphome.io/components/external_components/)
- [ESPHome BLE client](https://esphome.io/components/ble_client/)
- [ESPHome OTA](https://esphome.io/components/ota/)
- [ESP-IDF BLE HID device demo](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/ble_hid_device_demo)
