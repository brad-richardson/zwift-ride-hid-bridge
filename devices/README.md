# Device configuration

[`zwift-ride-hid-bridge.yaml`](zwift-ride-hid-bridge.yaml) is the reference configuration for a Seeed Studio XIAO ESP32-S3 with 8 MB flash and octal PSRAM. It pins ESPHome `2026.7.4`, uses ESP-IDF, and intentionally loads the component from `../components` so a repository checkout and CI compile exactly the source under review.

The image is feature-complete and in live hardware validation. Auto-discovery, the Ride handshake, Right-side tunneling, encrypted iPad HID, OTA reconnect, every digital input identity, both lever channels, and active-session scanner shutdown have been observed on the target XIAO. Keep a USB cable and serial recovery available until the sleep/loss and endurance gates pass.

## Secrets

The configuration reuses the secret names from the supplied ESPHome base configuration:

- `ssid`
- `password`
- `fallback_password`
- `api_key`

A Device Builder instance may already store the network under a `wifi_ssid`/`wifi_password` pair instead. Rename the two `!secret` references, or let `tools/dashboard.py` substitute them at render time, if that is the case.

Add one device-specific value:

- `ota_password` — a strong password used only for native ESPHome OTA.

For local builds:

```console
cp devices/secrets.example.yaml devices/secrets.yaml
```

Replace every fixture value. `devices/secrets.yaml` is ignored by Git. The tracked example uses deliberately non-secret values that are syntactically suitable for CI; never flash those values.

Both controllers normally advertise as `Zwift SF2`. The component automatically selects the first advertisement containing service UUID `FC82`, manufacturer/company ID `0x094A`, and device ID `8` (Ride Right is device ID `7`). It locks that address for the rest of the boot. Close Zwift, BikeControl, and other possible central clients while discovering or testing it.

The reference `ble_client` uses `00:00:00:00:00:00` as an explicit auto-discovery sentinel because ESPHome's stock client schema requires an address. At a venue or home where more than one Ride Left could be visible, replace the sentinel with the intended controller's real MAC. A nonzero address disables automatic selection and preserves stock ESPHome pinned-client behavior.

The YAML keeps ESPHome's tracker in continuous mode as the disconnected baseline. At runtime the component uses the tracker's public API to stop the global scanner whenever the Ride client leaves `IDLE`, including connection setup and an established session, and restores continuous scanning only after complete disconnect. Do not add a Bluetooth proxy, another BLE client, or advertisement-driven sensors without revisiting this policy because the ESP32 has one shared scanner.

Scanning also stays on throughout an idle suppression window, which is how the component notices that the controllers stopped advertising and later woke. The ESP32 is USB-powered, so the cost is radio time rather than battery.

## Local repository build

Keep the checked-in source block when the YAML and component are in the same clone:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [zwift_ride_hid]
```

Then run:

```console
uv sync --locked
uv run --locked esphome config devices/zwift-ride-hid-bridge.yaml
uv run --locked esphome compile devices/zwift-ride-hid-bridge.yaml
```

## Immutable Device Builder deployment

[`tools/dashboard.py`](../tools/README.md) automates the substitutions below by rendering the deployed configuration from this reference YAML, so the two cannot drift. Do the equivalent by hand only if the tooling is unavailable.

The deployed instance differs from this reference in exactly four ways: the `name`/`friendly_name` substitutions, the two Wi-Fi secret names if the network is stored under the `wifi_*` pair, and the source block. Replace only `external_components` with:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/brad-richardson/zwift-ride-hid-bridge.git
      ref: 0123456789abcdef0123456789abcdef01234567
      path: components
    components: [zwift_ride_hid]
    refresh: never
```

Replace the example with a real, reviewed full 40-character commit SHA. Do not deploy `main`, a branch name, or a movable tag. The immutable update loop is:

1. Let CI test and compile the candidate commit.
2. Copy that commit's full SHA into `ref`.
3. Compile in Device Builder and review the configuration and firmware-size output.
4. Install over the network with native ESPHome OTA.
5. Run the smoke tests in the hardware checklist.
6. If the candidate fails, restore the last known-good SHA, compile, and install that image OTA.

`refresh: never` is appropriate because a full commit SHA cannot change. Changing `ref` gives ESPHome a new source revision. The first installation should still be over USB; retain USB access because an OTA image cannot recover every bootloader, partition-table, or flash problem.

## Component options used by the reference config

```yaml
zwift_ride_hid:
  ble_client_id: ride_left
  hid_name: Zwift Ride KB
  profile: delta_emulator
  analog_levers:
    press_threshold: 35
    release_threshold: 20
    expose_raw: true
  haptics:
    connect_confirmation: true
    button_feedback: false
  idle_timeout:
    disconnect_after: 15min
    sleep_confirmation: 30s
    max_suppression: 60min
    release_hid: true
    slow_gap: 5s
    wake_burst_count: 3
    wake_burst_window: 3s
  status_led:
    number: GPIO21
    inverted: true
  diagnostics:
    ride_connected:
      name: Ride Controller Connected
    hid_connected:
      name: HID Host Ready
    ready:
      name: Bridge Ready
    state:
      name: Bridge State
    ride_advertising:
      name: Ride Advertising
    advertisement_age:
      name: Ride Advertisement Age
    reconnect_count:
      name: Ride Reconnect Count
    invalid_frame_count:
      name: Invalid Ride Frame Count
    hid_report_count:
      name: HID Report Count
    idle_disconnect_count:
      name: Ride Idle Disconnect Count
    setup_timeout_count:
      name: Ride Setup Timeout Count
    haptic_timeout_count:
      name: Ride Haptic Timeout Count
    left_lever:
      name: Left Lever Raw
      disabled_by_default: true
    right_lever:
      name: Right Lever Raw
      disabled_by_default: true
  debug_capture: false
  debug_advertisements: false
```

The press threshold must be greater than the release threshold. Raw lever entities are disabled by default in Home Assistant but enabled at the component while the physical signs are being established. After calibration, set `expose_raw: false` and remove the two raw sensors if they are not useful.

`connect_confirmation` requests one haptic pulse after the Ride handshake. `button_feedback` requests vibration for input transitions and is deliberately off to avoid distracting feedback and unnecessary GATT traffic. Haptics should be disabled first when diagnosing controller compatibility.

## Idle disconnect

Holding the Ride GATT link keeps the controllers awake, so an unattended bridge would flatten their batteries. `idle_timeout` releases the link instead:

- `disconnect_after` (default `15min`) is the quiet period. Any press, release, or lever threshold crossing restarts it, and a control that is simply held counts as continuous use. `0s` disables the feature and restores the original always-connected behavior.
- `sleep_confirmation` (default `30s`) sets only when `Ride Advertising` reads false. It no longer decides reconnection; see below.
- `max_suppression` (default `60min`) is the safety net. If the controllers never stop advertising, the bridge reconnects anyway rather than staying offline until a reboot. `0s` removes the cap and should only be used once the controllers' sleep behavior is well understood.
- `release_hid` (default `true`) also disconnects the bonded HID host for the duration of the idle period. A connected keyboard makes iPadOS hide its on-screen keyboard, so leaving the link up costs the iPad its software keyboard for hours. Set it to `false` to keep the host attached for the fastest possible resume.

### How the bridge decides the controllers are back

Ride Left does not stop advertising when released, and this took three attempts to characterise correctly. Measured at 50% scan duty:

| phase | when | advertising interval |
|---|---|---|
| fast | release to ~2 min | ~196 ms, continuous |
| slow | ~2 min to ~3 min | ~640 ms, continuous |
| asleep | after ~3 min | silent |

Two rules were tried and disproved on hardware first:

- **Wait for silence.** There is none until the controller sleeps, so this only ever reconnected after the full wind-down.
- **Wait for a burst of advertisements.** At 640 ms a handful of sightings arrive within a couple of seconds anyway, so the slow phase satisfied it continuously and the bridge reconnected into a controller that was merely winding down.

Both failures trace to the same measurement error. The rate was originally inferred from gaps between sightings at 9.375% scan duty, where roughly half of all advertisements are missed — which reported the ~640 ms slow phase as a ~6 s one, a tenfold error. Every threshold sized against that number was wrong.

What actually separates the two phases is the 3.3x rate change, so the bridge measures the mean gap over the last `rate_samples` sightings and applies hysteresis:

- the rate rising past `slow_rate` (default `500ms`) latches the controller as no longer fast;
- once latched, the rate falling below `wake_rate` (default `350ms`) is the wake, and reconnects.

Sightings closer together than about 100 ms are collapsed first, because one advertising event is often seen twice from different channels and would otherwise halve the apparent gap.

This is why the reference tracker runs at 50% receive duty (`80ms`/`160ms`) rather than the original 9.375%. The rate has to be resolved for any of this to work, and the scanner only runs while the Ride link is down.

`Ride Advertising` and `Ride Advertisement Age` show what the bridge currently sees; `sleep_confirmation` sets only when the former reads false.

Re-measure with `debug_advertisements: true` and `logger.level: DEBUG` if controller firmware changes. Turn it off afterwards — it is verbose and puts the controller's address in the log stream.

While suppressed, `Bridge State` reads `ride_idle_sleeping` and `Ride Idle Disconnect Count` separates deliberate releases from `Ride Reconnect Count`. Every key is released and `Zwift Ride KB` stops advertising; whether the bonded host is also disconnected depends on `release_hid`.

Two diagnostics explain the wait. `Ride Advertising` is false once the controllers have been quiet for `sleep_confirmation`, which is precisely the promise that the next advertisement will reconnect — if it reads true, a button press will *not* bring the session back, by design. `Ride Advertisement Age` gives the seconds since the last sighting and refreshes every five seconds while suppressed. The bridge also logs `Ride Left stopped advertising ...; armed to reconnect` at INFO when it crosses the threshold.

### Getting the session back immediately

`zwift_ride_hid.reconnect` cancels suppression regardless of what the controllers are broadcasting. The reference configuration exposes it as the `Reconnect Ride Controllers` button, which is the reliable escape hatch when the controllers are awake and the sleep/wake path therefore cannot fire:

```yaml
button:
  - platform: template
    name: Reconnect Ride Controllers
    entity_category: diagnostic
    on_press:
      - zwift_ride_hid.reconnect:
```

The active-low LED is assigned inside `zwift_ride_hid`; do not also configure ESPHome's generic `status_led` on GPIO21. Expected patterns are documented in the [hardware checklist](../docs/hardware-test-checklist.md).

## Resource choices

`esp32_ble.max_connections: 3` reserves one connection for Ride Left, one for the iPad HID host, and leaves one spare for stack behavior. Do not add `bluetooth_proxy`, a second keyboard implementation, or extra BLE clients until memory and reconnect behavior are measured.

The web server and Home Assistant time component are intentionally omitted. Native API logging and OTA cover the bring-up workflow while leaving more RAM for simultaneous Wi-Fi, BLE central, BLE peripheral, and bonding activity.
