# Zwift Ride input protocol

This document is the implemented protocol contract for the bridge. It combines the downloaded fork plan with the reviewed upstream sketch and published protocol notes. The bounded host-tested decoder follows this contract; captures from the actual target controllers remain the final authority.

## Topology

Only the **left** Ride controller needs a BLE connection. The right controller establishes its own link to the left, and the left tunnels button state from both sides. Discovery should therefore filter for:

- Zwift service UUID `0000FC82-0000-1000-8000-00805F9B34FB`
- manufacturer ID `0x094A`
- Ride-left device ID `8` (Ride-right is `7`)

The current handshake writes ASCII `RideOn` to the sync-RX characteristic, then subscribes to async notifications:

| Purpose | UUID |
|---|---|
| Async notify | `00000002-19CA-4651-86E5-FA29DCDD09D1` |
| Sync RX / write without response | `00000003-19CA-4651-86E5-FA29DCDD09D1` |
| Sync TX / indicate | `00000004-19CA-4651-86E5-FA29DCDD09D1` |

## Input packet

Input status messages begin with command byte `0x23`, followed by a protobuf payload. Parse the protobuf fields rather than assuming that `pData[2]`, `pData[3]`, and `pData[4]` will always occupy fixed offsets.

The implementation accepts complete notifications up to 128 bytes and performs no heap allocation. It decodes into a temporary value and replaces live state only after the entire packet passes validation, including the required button map.

- field 1: inverse-logic uint32 button bitmap (`0` means pressed)
- analog records have appeared in two wire-compatible semantic layouts and the parser must accept both:
  - published/older layout: field 2 is a length-delimited group containing repeated field-1 analog records;
  - current Zword capture layout: each analog record is a repeated length-delimited field 3 (`0x1A`);
  - within either record, field 1 is location `0..3` and field 2 is a ZigZag-encoded `sint32`, observed at approximately `-100..100`.

Example idle shapes:

```text
# Grouped field 2
23 08 FF FF FF FF 0F 12 18 0A 04 08 00 10 00 ...

# Repeated field 3
23 08 FF FF FF FF 0F 1A 04 08 00 10 00 ...
```

Do not make the outer field number part of the semantic input model. Decode either representation into the same four-channel state and skip unknown protobuf fields safely.

For known discrete buttons:

```text
pressed = (~wire_button_map) & KNOWN_BUTTON_MASK
```

Never invert unknown/reserved bits into phantom presses.

## Complete known discrete map

The physical side-button labels are retained because they are easier to verify on hardware. Protocol aliases in parentheses come from the reverse-engineered enum.

| Mask | Semantic ID | Physical control |
|---:|---|---|
| `0x000001` | `dpad_left` | Left D-pad: Left |
| `0x000002` | `dpad_up` | Left D-pad: Up |
| `0x000004` | `dpad_right` | Left D-pad: Right |
| `0x000008` | `dpad_down` | Left D-pad: Down |
| `0x000010` | `button_a` | Right: A |
| `0x000020` | `button_b` | Right: B |
| `0x000040` | `button_y` | Right: Y |
| `0x000100` | `button_z` | Right action pad: Z face button |
| `0x000200` | `left_side_upper` | Left gear shifter, upper (`SHFT_UP_L`) |
| `0x000400` | `left_side_middle` | Left gear shifter, middle (`SHFT_DN_L`) |
| `0x000800` | `left_side_lower` | Left drop button, LB (`POWERUP_L`) |
| `0x001000` | `left_power` | Left orange Zwift-logo/power button (`ONOFF_L`) |
| `0x002000` | `right_side_upper` | Right gear shifter, upper (`SHFT_UP_R`) |
| `0x004000` | `right_side_middle` | Right gear shifter, middle (`SHFT_DN_R`) |
| `0x010000` | `right_side_lower` | Right drop button, RB (`POWERUP_R`) |
| `0x020000` | `right_power` | Right orange Zwift-logo/power button (`ONOFF_R`) |

Known mask: `0x037F7F`. Bits 7 and 15 and all higher unlisted bits are currently reserved.

Do not conflate the three Z-marked controls: `button_z` is the single right-hand action-pad button, while `left_power` and `right_power` are two separately reported orange Zwift-logo buttons. Upstream Zword observes all three in different bits. Short logo-button taps can therefore be mapped independently; a long hold remains the controllers' power gesture and is not suitable for a hold-heavy emulator action.

The four `SHFT_*` bits are the Ride's discrete `LS1`, `LS2`, `RS1`, and `RS2` shifter/workout-power buttons. Preserve all four even though the first profile needs only one left/right pair for emulator shoulders. Hardware capture must confirm whether the reverse-engineered upper/middle ordering corresponds to the official 1/2 numbering before those aliases become public configuration names.

## Analog steering/brake levers

All channels should be parsed and capturable even when they are not mapped.

| Location | Semantic ID | Treatment |
|---:|---|---|
| `0` | `left_lever` | Signed raw value; expose negative and positive threshold states |
| `1` | `right_lever` | Signed raw value; expose negative and positive threshold states |
| `2` | `reserved_2` | Preserve in capture/diagnostic output; no default HID mapping |
| `3` | `reserved_3` | Preserve in capture/diagnostic output; no default HID mapping |

Each active lever becomes two logical actions:

- `left_lever_negative`, `left_lever_positive`
- `right_lever_negative`, `right_lever_positive`

Use separate press/release thresholds, initially `35` and `20`, to prevent chatter. Hardware capture must determine which sign corresponds to steering versus braking on each side. A sign change should release the old direction before pressing the new direction in the same rebuilt HID report.

## State and safety rules

- Diff decoded semantic state, not entire raw bytes.
- Preserve simultaneous bits; never exact-match a whole bitmap to a single-button code.
- Generate a complete HID report from current state.
- Send an empty report on controller disconnect, HID disconnect, OTA start, bridge reset, and an explicitly decoded idle/reset state. Do not infer idleness from hard-coded raw byte offsets; require all known button bits released and all thresholded lever actions inactive.
- A malformed/truncated `0x23` packet must not overwrite valid current state.
- Notification handling must use bounded buffers and avoid runtime allocation/log-string construction in normal mode.
- Debug capture should be opt-in and may log the raw packet, decoded bitmap, and four analog values.

## Protocol fixtures to capture

- idle
- every discrete control by itself
- D-pad + B and at least two unrelated 3-button chords
- press one chord member, add another, release only one
- each lever slowly through negative/full/zero/positive/full
- both grouped-field-2 and repeated-field-3 analog encodings
- lever plus discrete button
- right controller absent, joining, sleeping, and waking
- controller disconnect during a held chord
- any non-`0x23` keepalive/status frames seen in a 10-minute session

## References

- [Published Zwift Ride protocol analysis](https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/)
- [Reviewed upstream Zword sketch revision](https://github.com/Fuenfachsen/Zword_ZwiftRide-to-BLE-Keyboard/blob/7c7f516b44389f844a747e429b163fa6b68722c7/Zword_v0-0-1.ino)
