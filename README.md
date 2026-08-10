# Zwift Ride HID Bridge

> **Status: live hardware validation underway.** A XIAO ESP32-S3 has auto-discovered Ride Left, completed the `RideOn` handshake, received tunneled Right input, paired with an iPad as an encrypted HID keyboard, survived active-link OTA/reconnects, and captured all 16 digital controls plus both lever channels. Host tests and CI cover the corrected full button map. Haptics, controller sleep/loss recovery, held-key OTA teardown, exact Delta gameplay, and endurance still need validation. Keep USB recovery available during hardware testing.

This ESPHome external component turns the integrated Zwift Ride controller pair into a stateful BLE keyboard. It is designed first for Delta on iPadOS, but both bundled profiles emit ordinary HID keyboard usages and can work with other applications. The reference target is a Seeed Studio XIAO ESP32-S3 using ESP-IDF and ESPHome `2026.7.4`.

The project is downstream of [Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard](https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard). Zword established the Ride connection, `RideOn` handshake, haptic command, input protocol, and BLE-keyboard bridge that informed this implementation. Preserve that attribution in forks and distributions; exact revisions and adaptation notes live in [NOTICE.md](NOTICE.md).

## What is implemented

- ESPHome owns Wi-Fi, encrypted Home Assistant API access, native OTA, safe mode, scanning, and the single Bluedroid lifecycle.
- The component auto-discovers **Ride Left** using Zwift's FC82 service, company ID, and Ride-left device ID, then hands the selected address to ESPHome's stock BLE client. Ride Left tunnels Ride Right input, so the bridge does not consume a third controller connection.
- The HID keyboard is not advertised until Ride Left completes its handshake and input subscription. Ride loss releases every key and suppresses new HID connections; an already-connected, bonded iPad can remain attached and resume without another pairing cycle.
- After an idle period the bridge releases the Ride link so the controllers can sleep instead of being held awake by the connection, and by default disconnects the bonded HID host too so iPadOS gets its on-screen keyboard back. It reconnects when the controllers' advertising rate returns to the fast regime, which is what a wake or a power-on produces and what the wind-down does not, plus a `zwift_ride_hid.reconnect` action for getting the session back on demand. Roughly three minutes after the release the controllers power themselves off rather than merely sleeping, so past that point returning to the bike means switching them on; the bridge is ready again about five seconds later. Every interval is configurable, and `disconnect_after: 0s` disables the feature.
- `Ride Controllers Powered`, `Ride Advertisement Age (When Idle)`, and `Ride Advertising Rate (When Idle)` diagnostics report whether the controllers are still broadcasting and how fast. The rate is the quantity the reconnect decision compares against, so publishing it makes a wrong threshold visible at a glance instead of only in a capture. `debug_advertisements` logs each advertisement's manufacturer payload, flags, and interval for tuning.
- The component discovers the FC82 characteristics, subscribes to notifications, writes `RideOn`, and can send the controller's haptic command.
- A bounded protobuf decoder accepts both observed analog-record layouts, decodes all 16 known button bits and four signed analog channels, and rejects malformed frames transactionally.
- Threshold plus hysteresis turns both polarities of both active levers into four independent logical inputs.
- Complete six-key-rollover keyboard reports preserve holds and chords, deduplicate shared bindings, and provide deterministic release-all behavior.
- `delta_emulator` is the default universal Delta profile for NES, SNES, GB/GBC, GBA, DS, and Genesis. `diagnostic_all_inputs` gives every known input a distinct test key. N64 is intentionally excluded.
- Optional Home Assistant diagnostics, a connection-state LED, debug capture, connection haptics, and per-button haptics are configurable in YAML.
- OTA/shutdown handling releases every key and quiesces BLE work before the update proceeds.

This is still an unproven firmware candidate. In particular, a successful compile cannot prove iPad bonding, simultaneous central/peripheral radio behavior, reconnect timing, the numbered LS/RS physical aliases, or lever polarity. Follow the [hardware test checklist](docs/hardware-test-checklist.md) before calling any commit known-good.

## Quick start

1. Clone this repository and install [uv](https://docs.astral.sh/uv/).
2. Copy `devices/secrets.example.yaml` to the ignored `devices/secrets.yaml` and replace its fixture values. No controller address is required: both halves advertise as `Zwift SF2`, and the bridge selects Left using service UUID `FC82`, manufacturer/company ID `0x094A`, and device ID `8` (`7` is Right).
3. Validate and compile:

   ```console
   uv sync --locked
   uv run --locked esphome config devices/zwift-ride-hid-bridge.yaml
   uv run --locked esphome compile devices/zwift-ride-hid-bridge.yaml
   ```

4. Do the first installation over USB. Pair `Zwift Ride KB` in iPadOS Bluetooth settings only after serial logs show that the bridge has booted normally.

The checked-in device YAML deliberately uses a local external-component path so CI and repository checkouts compile the code being reviewed. For ESPHome Device Builder, replace only that block with the immutable Git form in [devices/README.md](devices/README.md), using a reviewed full 40-character commit SHA.

## Default Delta controls

The important defaults are arrows for the D-pad; `x`/`z` for A/B; `s`/`a` for X/Y; `q`/`w` for L/R; Tab/Return for Select/Start; and `p` for Delta's menu. The action pad is bound by position rather than by matching letters, because Zwift's diamond (Y top, A right, B bottom, Z left) is rotated relative to Delta's (X top, A right, B bottom, Y left). The remaining shifters, drop button, and four lever polarities receive distinct spare keys rather than disappearing.

See [the complete mapping table](docs/delta-mapping.md) for every button—including Z, both logo/power buttons, LS1, LS2, RS1, RS2, LB, RB, and all four thresholded lever actions—and for the diagnostic profile.

## Pairing, reconnect, and updates

- Close Zwift, BikeControl, and other apps that may already hold the controllers' host connection.
- Power both Ride controllers. The bridge connects only to Ride Left; Right normally joins through Left.
- Controller discovery uses active, continuous scanning while the Ride client is idle: an 80 ms receive window every 160 ms (50% nominal receive duty) in back-to-back five-minute scan sessions. The duty matters more than it looks: at the former 9.375% roughly half of all advertisements were missed, which reported the controllers' ~640 ms slow phase as a ~6 s one and invalidated every threshold sized against it. The component pauses the global scanner throughout discovery, connection setup, and the active session, then resumes as soon as disconnect cleanup returns the client to idle.
- Automatic selection locks the first exact Ride Left match for that boot. If more than one Ride setup is in radio range, replace the all-zero `ble_client` address with the intended Left controller's real MAC to pin it explicitly.
- Pair the advertised `Zwift Ride KB` once from the iPad. Bond data is stored in ESP32 NVS, so normal application OTA updates should not require re-pairing. Erasing flash/NVS will.
- Short taps of the two orange logo/power controls emit Select and Start. A long hold is still the controller power gesture and may disconnect it.
- Controller loss sends an empty report while the HID link is available; either link loss clears the bridge's internal key state so a later session cannot inherit held keys. ESPHome then retries Ride, but HID advertising returns only after a fresh controller handshake. These paths are implemented but remain hardware-untested.
- A quiet session ends deliberately: after `idle_timeout.disconnect_after`, the bridge releases the Ride link and stops advertising the keyboard. A held control counts as use and never triggers the timeout; lever movement below the press threshold does not.
- Ride Left does not go quiet when released. Measured at 50% scan duty it advertises continuously at ~196 ms for about two minutes, drops to a continuous ~640 ms, then stops near three minutes. The bridge reconnects on that 3.3x rate change rather than on any silence: the rate rising latches the controller as no longer fast, and falling back is the wake. `Ride Controllers Powered` shows which state the controllers are in, and the `Reconnect Ride Controllers` button overrides the whole thing.
- At OTA start the bridge releases keys, pauses Ride/HID report processing, disconnects both peers, and stops HID advertising before the update proceeds. An aborted/failed OTA resumes controller scanning without accepting queued input or advertising HID until a fresh Ride handshake; a successful OTA reboots into the new image.

After the first USB flash, normal updates are one pinned-SHA change and an ESPHome OTA install. Keep the last hardware-tested SHA handy: rollback means restoring that SHA in Device Builder, compiling, and installing it over the network. OTA cannot repair every bootloader, partition, or flash failure, so USB remains the recovery path.

## Design constraints

Do not load a second keyboard component, NimBLE-Arduino, or code that initializes Bluetooth or installs process-global BLE callbacks. `zwift_ride_hid` shares ESPHome's Bluedroid event brokers. Leave `bluetooth_proxy`, additional BLE clients, and unrelated generic BLE services disabled until dual-role heap/radio endurance is measured.

The parser, hysteresis state machine, and report builder stay independent of ESPHome so they can run as ordinary C++ host tests:

```console
cmake -S tests -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

More detail is in the [implementation/validation plan](PLAN.md), [protocol inventory](docs/protocol.md), [hardware checklist](docs/hardware-test-checklist.md), and [architecture decision](docs/decisions/0001-esphome-external-component.md).

## Deploying and watching a live bridge

[`tools/`](tools/README.md) holds the release and observation scripts for a
Device Builder instance: rendering the pinned deployment configuration from the
reviewed reference YAML, driving compile/install, streaming filtered device
logs, and watching diagnostic transitions over the encrypted API. Every
site-specific value comes from the environment, so no address or credential
lives in this repository. CI does not use these scripts.

## License and provenance

The repository is licensed `GPL-3.0-only`; see [LICENSE](LICENSE). GPL was selected because this is a downstream adaptation of GPL-3.0 software, rather than the author's usual MIT default. [NOTICE.md](NOTICE.md) records the upstream projects, reviewed revisions, and the role of each source.
