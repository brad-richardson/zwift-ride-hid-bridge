# Hardware test checklist

Do not treat the Milestone-0 scaffold as functional bridge firmware. It compiles, but the Ride handshake, input decoder, HID service, and release-all lifecycle are not implemented yet.

## Before first functional flash

- [ ] Record the ESPHome Device Builder version; it must be 2026.7.4 or a jointly reviewed newer pin.
- [ ] Add a strong unique `ota_password` to Device Builder secrets.
- [ ] Identify Ride Left by manufacturer ID `0x094A`, device ID `8`; put its MAC in `ride_left_mac` without committing it.
- [ ] Close Zwift, BikeControl, and other apps that may hold the controllers' single host connection.
- [ ] Confirm USB serial recovery before relying on OTA.

## Dual-role feasibility gate

- [ ] Pair the iPad with `Zwift Ride KB` and send a synthetic press plus release in Notes.
- [ ] At the same time, connect to Ride Left, write `RideOn`, subscribe, and receive a `0x23` notification.
- [ ] Reboot without erasing NVS and confirm both the bonded iPad and Ride Left reconnect.
- [ ] Start an OTA while a synthetic key is held; observe an empty report before BLE teardown.
- [ ] Abort one OTA and confirm advertising/scanning resumes without a reboot.

## Complete digital-input capture

Capture one idle frame and one press/release for every item. Record the full decoded mask, not byte-local `0xFE`-style values.

- [ ] D-pad: Left, Up, Right, Down.
- [ ] Right face pad: A, B, Y, face Z.
- [ ] Left shifter/workout-power buttons: LS1 and LS2; determine upper/middle order.
- [ ] Right shifter/workout-power buttons: RS1 and RS2; determine upper/middle order.
- [ ] Drop buttons: LB and RB.
- [ ] Orange logo/power buttons: left and right short taps independently.
- [ ] Confirm how long each logo button may be held before the controller powers down.
- [ ] Verify representative chords and partial releases, including B + Right.

## Analog paddle capture

- [ ] Record centered, both travel directions, and full travel for ZL and ZR.
- [ ] Determine which sign is steering and which is braking on each side.
- [ ] Exercise the proposed press 35 / release 20 hysteresis and check for chatter.
- [ ] Preserve unexpected channels 2 and 3 in diagnostic captures without mapping them.

## Delta profile

- [ ] Confirm the fallback mapping in Notes/Delta: arrows, `x/z/s/a`, `q/w`, Return, Tab, and Escape.
- [ ] Verify left short logo tap = Select and right short logo tap = Start.
- [ ] Verify outer shoulder pair = L/R; keep the second shifter pair independently visible as `e/r`.
- [ ] Verify LB = Space, RB = Escape, and all four paddle directions = `1/2/3/4`.
- [ ] Test NES, SNES, GB/GBC, GBA, DS, and Genesis controls; record any saved per-system overrides.
- [ ] Confirm N64 remains explicitly unsupported rather than silently mis-mapped.

## Recovery and endurance

- [ ] Ride controller sleep/wake reconnects without rebooting the XIAO.
- [ ] iPad Bluetooth off/on reconnects without a stuck key.
- [ ] Wi-Fi loss does not break an established controller/HID session.
- [ ] Native OTA succeeds and the old known-good SHA remains available for rollback.
- [ ] Complete a 45-minute Delta session with holds/chords and one controller sleep/wake.
- [ ] Log minimum free heap, largest free block, BLE event drops, and reconnect count.
