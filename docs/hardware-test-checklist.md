# Hardware test checklist

The repository is a feature-complete **hardware-untested** candidate. Host tests and an ESPHome compile do not validate the radio, GATT timing, iPadOS HID behavior, real packet variants, physical label ordering, or OTA teardown. Work through this list before describing any commit as known-good.

Record the tested full Git SHA, ESPHome version, controller firmware versions, iPad model/iPadOS version, observations, and firmware-size summary with the results.

## Prepare a recoverable first flash

- [ ] Confirm the Device Builder/tooling version is ESPHome `2026.7.4`.
- [ ] Copy the reference YAML, add a strong unique `ota_password`, and keep all fixture credentials out of the installed device.
- [ ] Leave the reference all-zero BLE-client sentinel in place and confirm logs auto-select a device only when service UUID `FC82`, manufacturer/company ID `0x094A`, and Ride-left device ID `8` are all present.
- [ ] Close Zwift, BikeControl, and other apps that could hold the controllers' host connection.
- [ ] Verify USB serial logs and the ability to reflash before depending on OTA.
- [ ] Compile the exact full commit SHA intended for the device; record RAM and flash use.
- [ ] Flash over USB without erasing NVS on later retries unless bonding recovery specifically requires it.

## Boot and status diagnostics

- [ ] XIAO joins Wi-Fi and appears through the encrypted Home Assistant API.
- [ ] `Bridge State`, `Ride Controller Connected`, `HID Host Ready`, and `Bridge Ready` agree with serial logs.
- [ ] Active-low GPIO21 LED follows the intended states: one 200 ms blink every 1.5 seconds while starting/scanning or when only HID is ready; two 100 ms blinks every two seconds with Ride ready but no HID host; solid with both links ready; 100 ms on/off during OTA; three 100 ms blinks every two seconds on an error; and off when stopped.
- [ ] `Invalid Ride Frame Count`, `Ride Reconnect Count`, and `HID Report Count` are monotonic and plausible.
- [ ] Raw lever entities remain disabled in Home Assistant unless actively calibrating them.

Treat exact LED timing as diagnostic rather than a public API until it has been observed on hardware.

## Dual-role BLE and first pairing

- [ ] Power Ride Left and Right. Confirm the ESP32 connects only to Left and receives tunneled input from both halves.
- [ ] Confirm Ride Right (`0x094A`, device ID `7`), an FC82 advertisement without the matching manufacturer tuple, and a matching manufacturer tuple without FC82 do not trigger selection.
- [ ] Confirm the bridge discovers the FC82 service, subscribes to async notifications, and successfully writes ASCII `RideOn`.
- [ ] With the Ride link still ready, find `Zwift Ride KB` in iPadOS Bluetooth settings and pair it.
- [ ] Confirm encrypted HID input works in Notes and that bonding survives an ESP32 reboot without erasing NVS.
- [ ] Confirm the optional one-time connection haptic occurs after the Ride handshake and does not repeat in a loop.
- [ ] Leave `button_feedback: false` for the first input test; enable it later and check that it does not starve notifications or flood writes.
- [ ] Verify only one HID host is accepted and a second host cannot hijack an active session.

If the iPad will not reconnect, first toggle iPad Bluetooth and restart the bridge without erasing NVS. If that fails, forget `Zwift Ride KB` on the iPad, clear the ESP32 bond only as a deliberate recovery step, and pair again. Record which step was necessary.

## All 16 discrete inputs

Use `diagnostic_all_inputs` for identification. Capture an idle frame plus press, hold, and release for every control, and record the decoded full mask rather than byte-local values.

- [ ] D-pad: Left, Up, Right, Down.
- [ ] Right action pad: A, B, Y, and Z.
- [ ] Left discrete controls: LS1/upper (`left_side_upper`), LS2/middle (`left_side_middle`), LB/drop (`left_side_lower`), and the left orange logo/power button.
- [ ] Right discrete controls: RS1/upper (`right_side_upper`), RS2/middle (`right_side_middle`), RB/drop (`right_side_lower`), and the right orange logo/power button.
- [ ] Confirm whether the project's upper/middle aliases match the Ride's printed LS1/LS2 and RS1/RS2 numbering. Correct documentation, not semantic IDs or keys, if the numbers are reversed.
- [ ] Verify the action-pad Z and both logo/power buttons are three independent bits.
- [ ] Measure how long each logo button can be held before the controller powers down; normal Start/Select taps must remain comfortably shorter.
- [ ] Verify representative chords and partial releases: B + Right, a three-button chord, and release of one member while the others remain held.
- [ ] Press more than six distinct non-modifier inputs and confirm explicit 6KRO overflow behavior, then confirm the next normal report recovers.

Expected diagnostic keys are arrows; `a/b/y/z`; F1–F4 for LS1/LS2/LB/left logo; F5–F8 for RS1/RS2/RB/right logo; and `1/2/3/4` for lever polarities.

## Both analog levers

- [ ] Record centered, slow negative travel, full negative, slow positive travel, and full positive for left and right.
- [ ] Determine which physical motion each sign represents: braking, inward steering, or outward steering.
- [ ] Confirm the full usable range and whether center has a persistent offset on either controller.
- [ ] With press 35 / release 20, hover around both thresholds and check that each polarity presses once and releases once without chatter.
- [ ] Cross directly from negative to positive and confirm the old direction releases before the new direction presses in the rebuilt report.
- [ ] Hold a lever direction with one or more discrete buttons and confirm a combined report.
- [ ] Preserve unexpected locations 2 and 3 in debug capture without generating a default HID key.
- [ ] After calibration, decide whether raw Home Assistant sensors should remain configured; disable `expose_raw` for normal use if not.

## Delta profile

- [ ] Switch back to `profile: delta_emulator` and verify keys in Notes before opening Delta.
- [ ] Confirm arrows, `x/z/s/a`, `q/w`, Tab, Return, and Escape match Delta's fallback mapping.
- [ ] Verify left short logo tap = Select and right short logo tap = Start.
- [ ] Verify LS1/RS1 = L/R and LS2/RS2 remain independently visible as `e/r`.
- [ ] Verify LB = Space, RB = Escape, and the four lever polarities = `1/2/3/4`.
- [ ] Test holds and gameplay chords in NES, SNES, GB/GBC, GBA, DS, and Genesis; record saved per-system overrides.
- [ ] Confirm N64 is documented as unsupported and is not presented as a working universal mapping.

## Disconnect and reconnect safety

- [ ] Turn off Ride during a held key. The HID host must receive an empty report and no key may remain stuck.
- [ ] Let the controllers sleep, wake them, and verify Ride returns to ready without rebooting the ESP32.
- [ ] Power Right before Left and Left before Right; tunneled input should recover in either order.
- [ ] Turn iPad Bluetooth off during a held key, turn it back on, and verify reconnect plus clean subsequent input.
- [ ] Reboot the ESP32 and confirm both Ride and the bonded iPad reconnect without re-pairing.
- [ ] Remove Wi-Fi temporarily; an established Ride/HID session should continue and recover API connectivity later.
- [ ] Deliberately open a competing controller app and document the expected failure/recovery behavior.

## OTA lifecycle and rollback

- [ ] Keep the previous known-good full commit SHA before starting.
- [ ] Begin native OTA with an input held. Observe an empty report before Ride disconnect and before input/report processing is quiesced.
- [ ] Complete an OTA and confirm the new image boots, reconnects Wi-Fi/Ride/iPad, and preserves the bond.
- [ ] Abort or force one OTA error. Confirm input remains released and scanning/advertising resume without a reboot.
- [ ] Install the previous pinned SHA OTA as a rollback drill.
- [ ] Trigger ESPHome safe mode with an intentionally failing application boot only when USB recovery is available; verify recovery access.

OTA is application-level recovery, not a substitute for USB. It cannot repair every bootloader, partition table, flash corruption, or networking failure.

## Endurance gate

- [ ] Complete a 45-minute Delta session with holds, chords, both controller halves, and one controller sleep/wake cycle.
- [ ] Run a four-hour idle/reconnect/input loop.
- [ ] Record minimum free heap, largest free block if available, reconnect count, invalid-frame count, unexpected release-all/6KRO warnings, and BLE event drops.
- [ ] Confirm no steadily growing memory use, repeated pairing prompts, stuck keys, haptic write storm, or degraded input latency.
- [ ] Mark a specific full Git SHA known-good only after all required checks pass.
