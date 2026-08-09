# Operator tooling

Scripts for deploying and observing a live bridge. They exist because the
project's release path is a Device Builder instance on the local network rather
than a CLI flash, and because the hardware checks in
[docs/hardware-test-checklist.md](../docs/hardware-test-checklist.md) need a way
to watch state transitions without a serial cable.

Nothing here is required to build or test the firmware. CI never runs it.

## Setup

`websockets` lives in the non-default `tools` dependency group, so a plain
`uv sync` keeps the ESPHome-only dependency set that CI installs:

```console
uv sync --group tools
```

Every site-specific value comes from the environment. No address, device name,
credential, or key path belongs in this repository:

```console
export ESPHOME_DASHBOARD_URL="ws://<device-builder-host>:6052/ws"
export ESPHOME_DASHBOARD_CREDENTIALS="$HOME/.config/<dashboard-credentials-file>"
export ESPHOME_DASHBOARD_CONFIG="<dashboard-configuration>.yaml"

# Only needed where the deployed device differs from the reference YAML.
export ESPHOME_DEVICE_NAME="<deployed-esphome-name>"
export ESPHOME_FRIENDLY_NAME="<deployed-friendly-name>"
export ESPHOME_WIFI_SSID_SECRET="<wifi-ssid-secret-name>"
export ESPHOME_WIFI_PASSWORD_SECRET="<wifi-password-secret-name>"

export ZWIFT_BRIDGE_HOST="<device-address>"
export ZWIFT_BRIDGE_NAME="<deployed-esphome-name>"
export ZWIFT_BRIDGE_API_KEY_FILE="$HOME/.config/<api-key-file>"
```

The credentials file holds the dashboard username on the first line and the
password on the second.

## Releasing

`dashboard.py` derives the deployed configuration from the reviewed
`devices/zwift-ride-hid-bridge.yaml` instead of keeping a second copy, so the
two can never drift. It substitutes exactly the documented deployment
differences — device name, friendly name, Wi-Fi secret names, and the immutable
Git source pin — and fails loudly if any substitution does not apply exactly
once.

```console
uv run --group tools tools/dashboard.py render <full-40-character-sha> | less
uv run --group tools tools/dashboard.py deploy <full-40-character-sha>
uv run --group tools tools/dashboard.py compile
uv run --group tools tools/dashboard.py jobs
uv run --group tools tools/dashboard.py install     # OTA; drops both BLE links
uv run --group tools tools/dashboard.py status
```

`deploy` only writes the configuration; it never installs. Review the compile
output and firmware-size summary before `install`. Installing reboots the
bridge, which disconnects the Ride controllers and the iPad. Rolling back is the
same sequence with the previous known-good SHA.

Only a full 40-character commit SHA is accepted — a branch or tag would make the
installed firmware unreproducible.

## Observing

```console
uv run tools/bridge_state.py snapshot
uv run tools/bridge_state.py watch 900
uv run --group tools tools/dashboard_logs.py 300
uv run --group tools tools/dashboard_logs.py 900 'Ride idle|woke'
```

`bridge_state.py` needs no extra group: `aioesphomeapi` already ships with
ESPHome. `watch` prints only transitions, which makes it the right tool for the
15-minute idle disconnect and the sleep/wake reconnect. `dashboard_logs.py`
drops the idle `0x23` input frames by default, since the controllers notify
continuously.
