# Attribution and provenance

Zwift Ride HID Bridge is downstream of:

- **Zword_ZwiftRide-to-BLE-Keyboard** by Fuenfachsen
  - Source: https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard
  - License: GPL-3.0
  - Role: original Zwift Ride to BLE keyboard bridge, including discovery, `RideOn` handshake, notification flow, and haptic behavior
  - Reviewed protocol revision: `7c7f516b44389f844a747e429b163fa6b68722c7`
  - Imported revision: none yet; record the full commit SHA before adapting source

Implementation references under consideration:

- **ESPHome-espidf_ble_keyboard** by markusg1234
  - Source: https://github.com/markusg1234/ESPHome-espidf_ble_keyboard
  - License: GPL-3.0
  - Role: known ESPHome/ESP-IDF Bluedroid HID implementation with iOS pairing and key-hold support
  - Audited revision: `21274b03dd424927e35cd67bbb7c9af848daaef5` (`v1.7.0`)
  - Imported revision: none yet; record the adapted files/sections before copying source

Protocol facts are also informed by Makinolo's published Zwift Ride analysis:

- https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/

Keep this file current whenever code or protocol fixtures are imported. Preserve copyright notices and license texts supplied by upstream projects.
