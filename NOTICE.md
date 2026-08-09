# Attribution and provenance

Zwift Ride HID Bridge is a downstream GPL-3.0 project. No upstream source file is included wholesale. The following revisions and adaptation boundaries apply to the current implementation.

## Zword_ZwiftRide-to-BLE-Keyboard

- Author/project: Fuenfachsen, **Zword_ZwiftRide-to-BLE-Keyboard**
- Source: https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard
- License: GPL-3.0
- Reviewed revision: `7c7f516b44389f844a747e429b163fa6b68722c7`
- Reviewed file: `Zword_v0-0-1.ino`
- Role: primary upstream project and provenance for the Ride FC82 connection, characteristic UUIDs, ASCII `RideOn` handshake, notification flow, button meanings, and haptic command behavior.

The corresponding bridge code is a new ESPHome BLE-client implementation in `components/zwift_ride_hid/ride_client.*` plus a bounded protobuf implementation in `ride_protocol.*`; it does not carry Zword's Arduino/NimBLE lifecycle or sketch structure. Protocol facts also inform `input_state.*`, `keymap.*`, and the documentation.

## ESPHome-espidf_ble_keyboard

- Author/project: markusg1234, **ESPHome-espidf_ble_keyboard**
- Source: https://github.com/markusg1234/ESPHome-espidf_ble_keyboard
- License: GPL-3.0
- Adapted revision: `21274b03dd424927e35cd67bbb7c9af848daaef5` (`v1.7.0`)
- Adapted area: the keyboard report descriptor and GATT service/attribute layout in `components/espidf_ble_keyboard/espidf_ble_keyboard.cpp`.
- Downstream file: `components/zwift_ride_hid/hid_keyboard.cpp`.

The downstream file identifies this adaptation in its source comment. It is a reduced and modified port: Bluetooth controller/Bluedroid/NVS initialization, process-global callback registration, mouse and media reports, web UI, and multi-host state were removed. The replacement accepts ESPHome 2026.7.4 broker events and implements only the single-host keyboard transport required here.

## Espressif BLE HID device demo

- Author/project: Espressif Systems, ESP-IDF `ble_hid_device_demo`
- Source: https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/ble_hid_device_demo
- License: Apache-2.0
- Role: upstream behavioral and HID-layout reference followed by `ESPHome-espidf_ble_keyboard`, and a secondary reference for this reduced port.

## Protocol documentation reference

Protocol facts are also informed by Makinolo's published Zwift Ride analysis:

- https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/

Keep this file synchronized with source-level provenance comments whenever code, tables, or protocol fixtures are imported or adapted. Preserve copyright notices and license texts supplied with any future upstream material.
