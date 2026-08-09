# Device configuration

[`zwift-ride-hid-bridge.yaml`](zwift-ride-hid-bridge.yaml) is the Seeed Studio XIAO ESP32-S3 reference configuration. It currently loads the compile-only component scaffold from the local checkout.

## Existing secrets

The configuration reuses these names already present in Device Builder:

- `ssid`
- `password`
- `fallback_password`
- `api_key`

Add these local values before compiling:

- `ota_password` — a strong, unique password for native OTA
- `ride_left_mac` — the local address of Ride Left (configuration, not a credential)

ESPHome's OTA documentation explicitly recommends a strong unique password. Reusing the API key or fallback-hotspot password would unnecessarily couple credentials.

No secret values are stored here. For local compilation, copy `secrets.example.yaml` to `secrets.yaml` and replace every value; `secrets.yaml` is ignored by Git.

Set `ride_left_mac` after identifying the controller advertising manufacturer ID `0x094A`, device ID `8`. Both controllers advertise as `Zwift SF2`, so the name alone is insufficient. A discovery helper is planned; ESPHome's stock `ble_client` currently requires a MAC address. The MAC is stored through `!secret` simply to keep a device-specific value out of the reusable tracked YAML.

## Local component development

Keep this form while the YAML and repository are in the same checkout:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [zwift_ride_hid]
```

## Device Builder deployment

After publishing the repository, replace only the `external_components` block with:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/OWNER/zwift-ride-hid-bridge.git
      ref: 0123456789abcdef0123456789abcdef01234567
      path: components
    components: [zwift_ride_hid]
    refresh: never
```

Use a real, reviewed 40-character commit SHA. Do not deploy from `main`; a SHA cannot move underneath a known-good device build. Updating is then:

1. replace `ref` with the tested new commit SHA;
2. compile in Device Builder;
3. inspect logs/firmware size and install OTA;
4. restore the previous SHA and reinstall if hardware validation fails.

`refresh: never` avoids needless Git checks for an immutable revision. A different ref is a distinct source and will be fetched when the YAML changes.

## Resource choices

The web server and Home Assistant time component remain omitted until the Wi-Fi + Ride client + BLE HID memory test passes. Native API logs and OTA are enough during development. The reference configuration requires ESPHome 2026.7.4, which has passed a full local ESP32-S3 compile. Move that pin only after the home server and compile check are updated together.
