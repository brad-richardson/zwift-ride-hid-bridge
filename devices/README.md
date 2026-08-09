# Device configuration

[`zwift-ride-hid-bridge.yaml`](zwift-ride-hid-bridge.yaml) is the reference configuration for a Seeed Studio XIAO ESP32-S3 with 8 MB flash and octal PSRAM. It pins ESPHome `2026.7.4`, uses ESP-IDF, and intentionally loads the component from `../components` so a repository checkout and CI compile exactly the source under review.

The image is feature-complete but hardware-untested. Keep a USB cable and serial recovery available for the first installation.

## Secrets

The configuration reuses the secret names from the supplied ESPHome base configuration:

- `ssid`
- `password`
- `fallback_password`
- `api_key`

The user's Device Builder also contains `wifi_ssid` and `wifi_password`, but the supplied working base uses `ssid` and `password`; the reference YAML follows that base. Rename the two `!secret` references if the intended network is stored under the `wifi_*` pair instead.

Add two device-specific values:

- `ota_password` — a strong password used only for native ESPHome OTA;
- `ride_left_mac` — Ride Left's BLE address. It is configuration rather than a credential, but keeping it in secrets leaves tracked YAML reusable.

For local builds:

```console
cp devices/secrets.example.yaml devices/secrets.yaml
```

Replace every fixture value. `devices/secrets.yaml` is ignored by Git. The tracked example uses deliberately non-secret values that are syntactically suitable for CI; never flash those values.

Both controllers normally advertise as `Zwift SF2`. Identify Ride Left by manufacturer ID `0x094A`, device ID `8` (Ride Right is device ID `7`). Close Zwift, BikeControl, and other possible central clients while discovering or testing it.

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

When the YAML lives on the home server, replace only `external_components` with:

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
    reconnect_count:
      name: Ride Reconnect Count
    invalid_frame_count:
      name: Invalid Ride Frame Count
    hid_report_count:
      name: HID Report Count
    left_lever:
      name: Left Lever Raw
      disabled_by_default: true
    right_lever:
      name: Right Lever Raw
      disabled_by_default: true
  debug_capture: false
```

The press threshold must be greater than the release threshold. Raw lever entities are disabled by default in Home Assistant but enabled at the component while the physical signs are being established. After calibration, set `expose_raw: false` and remove the two raw sensors if they are not useful.

`connect_confirmation` requests one haptic pulse after the Ride handshake. `button_feedback` requests vibration for input transitions and is deliberately off to avoid distracting feedback and unnecessary GATT traffic. Haptics should be disabled first when diagnosing controller compatibility.

The active-low LED is assigned inside `zwift_ride_hid`; do not also configure ESPHome's generic `status_led` on GPIO21. Expected patterns are documented in the [hardware checklist](../docs/hardware-test-checklist.md).

## Resource choices

`esp32_ble.max_connections: 3` reserves one connection for Ride Left, one for the iPad HID host, and leaves one spare for stack behavior. Do not add `bluetooth_proxy`, a second keyboard implementation, or extra BLE clients until memory and reconnect behavior are measured.

The web server and Home Assistant time component are intentionally omitted. Native API logging and OTA cover the bring-up workflow while leaving more RAM for simultaneous Wi-Fi, BLE central, BLE peripheral, and bonding activity.
