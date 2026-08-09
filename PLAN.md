# Implementation plan

> **Implementation status:** the version-1 software surface described below is present, with host tests and a pinned ESPHome compile check in CI. It has not been exercised on a XIAO ESP32-S3, real Ride controllers, or an iPad. Milestone acceptance lists are therefore hardware validation gates, not claims of completion.

## Recommendation

Build this as one pinned ESPHome external component for the ESP32-S3/ESP-IDF framework. ESPHome owns the operational shell and the single Bluedroid lifecycle—Wi-Fi, Device Builder, secrets, logging, encrypted API, safe mode, OTA, scanning, and the Ride GATT client. `zwift_ride_hid` adds the missing HID keyboard server through ESPHome's BLE event brokers:

- GATT client to one Zwift Ride **left** controller
- BLE HID keyboard peripheral to the iPad

This is the selected ESPHome-centered route. A working GPL-3.0 ESPHome BLE keyboard component demonstrates Bluedroid HID, iOS pairing, and held keys, but an audit found that it cannot be loaded unchanged beside ESPHome's built-in BLE client: it reinitializes the controller/host and replaces the global GAP/GATTS callbacks. The bridge therefore contains only the broker-aware HID server behavior it needs and leaves stack/NVS initialization, process-global callback ownership, web keyboard, mouse, and multi-host behavior out.

Use ESPHome's `esp32_ble_tracker` and `ble_client` for Ride Left. Register the HID handlers with ESPHome's callback broker, filter GATTS events by application/interface and security events by the iPad peer, and reserve one server connection. Do not add NimBLE-Arduino, T-vK, the unmodified keyboard component, `bluetooth_proxy`, or unrelated generic BLE services. The dual-role proof remains the hard hardware gate before the implementation is called usable.

## Scope

### Version 1

- First flash by USB; every normal update after that through native ESPHome OTA.
- Production YAML loads only `zwift_ride_hid` from this Git repository at an immutable 40-character commit SHA.
- Auto-discover or configure the Ride left controller, then reconnect after sleep/drop.
- `RideOn` handshake, notification subscription, and haptic command.
- Decode the protobuf wire format, not fixed byte offsets.
- Support all 16 known discrete inputs.
- Support both signed analog lever channels as four configurable thresholded actions (negative/positive for each side), with hysteresis.
- Preserve the two reserved analog channels in diagnostic output.
- Generate stateful HID reports that support holds and chords.
- Release all keys on controller loss, HID-host loss, invalid session reset, and OTA start.
- Compile-time YAML mapping profiles, starting with `delta_emulator` and `diagnostic_all_inputs`.
- Optional status entities/logging without publishing every input event to Home Assistant in normal mode.

### Deferred

- Runtime remapping web UI.
- BLE gamepad/composite HID mode.
- Media-key layer.
- Multiple Ride pairs.
- Automatic controller firmware compatibility beyond current `FC82`, unless a fixture proves the older service remains worth supporting.

## Repository shape

```text
zwift-ride-hid-bridge/
├── README.md
├── PLAN.md
├── LICENSE
├── NOTICE.md
├── pyproject.toml                 # pinned ESPHome/dev tooling
├── .gitignore
├── components/
│   └── zwift_ride_hid/
│       ├── __init__.py            # YAML schema and ESPHome code generation
│       ├── zwift_ride_hid.h/.cpp  # lifecycle, mapping aggregation, safety
│       ├── ride_client.h/.cpp     # ESPHome BLE-client handshake/notify/haptics
│       ├── ride_protocol.h/.cpp   # bounded protobuf/varint parser
│       ├── input_state.h/.cpp     # semantic buttons and lever hysteresis
│       ├── keymap.h/.cpp          # canned mappings and whole HID reports
│       └── hid_keyboard.h/.cpp    # broker-aware Bluedroid HID service/reports
├── devices/
│   ├── zwift-ride-hid-bridge.yaml # reference device config
│   └── secrets.example.yaml
├── tests/
│   ├── CMakeLists.txt
│   └── host_core_tests.cpp        # protocol/state/mapping host coverage
└── docs/
    ├── protocol.md
    ├── delta-mapping.md
    ├── hardware-test-checklist.md
    └── decisions/
        └── 0001-esphome-external-component.md
```

Keep the parser, edge detector, and mapping aggregator independent of ESPHome so they can run as fast host tests.

## Device Builder deployment contract

Development uses a local component path from the checked-out repository. An installed device uses the Git form below:

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

`ref` must be a reviewed full commit SHA, not `main` and not a movable tag. The update workflow is deliberately boring:

1. CI compiles/tests the candidate commit.
2. Change the one SHA in Device Builder.
3. Compile, inspect the firmware-size summary, and install OTA.
4. If hardware validation fails, restore the previous SHA and install again.

Keep the external component self-contained. Any source dependency that cannot be vendored with compatible attribution must also be pinned immutably. Never require a live dependency on a second BLE-keyboard component.

## Runtime design

```text
Ride left controller
  └─ FC82 notification (right controller is already tunneled here)
      └─ bounded protobuf decoder
          ├─ 16-bit discrete semantic state
          └─ 4 signed analog channel values
              └─ threshold + hysteresis → semantic lever states
                  └─ physical-to-HID mapping aggregator
                      └─ complete keyboard report → iPadOS/host application
```

The bridge should rebuild the whole keyboard report from current semantic state whenever anything changes. This is safer than issuing unrelated incremental `press()`/`release()` calls:

- two physical controls may intentionally map to one key without premature release;
- a dropped input frame cannot leave a stale per-button object behind;
- `release_all()` is a single deterministic empty report;
- chords naturally become one report containing multiple simultaneous keys.

Use a standard keyboard report first and explicitly detect/report six-key rollover overflow. The intended mappings should stay well below that limit. Consider NKRO only after iPadOS compatibility testing.

## Configuration surface

The external-component schema is deliberately small and validated by ESPHome:

```yaml
ble_client:
  - id: ride_left
    # ESPHome requires a value; zero asks the bridge to select Ride Left from
    # its FC82 service plus Zwift manufacturer/device identifiers.
    mac_address: "00:00:00:00:00:00"

zwift_ride_hid:
  ble_client_id: ride_left
  hid_name: "Zwift Ride KB"
  profile: delta_emulator
  analog_levers:
    press_threshold: 35
    release_threshold: 20
    expose_raw: false
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
  debug_capture: false
```

Advanced `mappings:` overrides remain deferred until a canned profile works on hardware. Each known physical input already has a stable semantic ID, even if a future profile disables its HID mapping.

## Milestones and gates

### 0. Bootstrap and hardware facts — software complete, hardware pending

Pin ESPHome 2026.7.4 (or move the repository and home server together to a reviewed newer patch) and keep `secrets.example.yaml` credential-free. Target the Seeed Studio XIAO ESP32-S3 with 8 MB flash, 8 MB octal PSRAM, and its active-low user LED on GPIO21. Prefer the specific `seeed_xiao_esp32s3` board definition; retain the user's proven `esp32-s3-devkitc-1` definition as the documented fallback if their Device Builder version rejects the specific ID. The repository is public and the production contract now points to its real URL; the user must select a reviewed post-implementation SHA after CI passes.

Acceptance:

- reference YAML validates and compiles in CI;
- first USB flash boots, joins Wi-Fi, appears in Device Builder, and accepts a no-op OTA update;
- no credentials or real device addresses enter git.

The feature-complete candidate compiles on ESPHome 2026.7.4 / ESP-IDF 5.5.5. With automatic Ride discovery, its 1,278,851-byte image uses 140,387 bytes of DIRAM (41.1%) and 32.5% of the 3,932,160-byte OTA application partition, leaving 67% of that partition free.

### 1. Dual-role feasibility (implemented, hard hardware gate)

The component implements the Ride client and a minimal HID server while letting ESPHome initialize Bluedroid and own its event brokers. Source provenance is recorded in `NOTICE.md`. The remaining gate is to prove, in the same running image, an iPad HID link plus a Ride-left connection, successful `RideOn`, and a real `0x23` notification.

Acceptance:

- iPad pairs with `Zwift Ride KB` and a synthetic test action types one character in Notes;
- the bridge simultaneously connects to Ride left and receives a `0x23` frame;
- one OTA succeeds after releasing keys and suspending BLE work;
- after reboot, the bonded iPad and Ride controller reconnect without erasing pairings;
- heap and BLE-event-drop counters remain stable during a 20-minute idle/reconnect loop.

Decision gate:

- **Pass:** continue with the ESPHome-owned Bluedroid design.
- **Fail due to a correctable HID broker limitation:** add the smallest broker/lifecycle adaptation inside the external component; never replace ESPHome's global callbacks.
- **Fail due to an irreducible ESPHome lifecycle/resource conflict:** switch to standalone ESP-IDF/Bluedroid with a separately secured OTA path. Preserve the parser, tests, and repository layout. Do not introduce a second BLE stack merely to save the ESPHome shell.

### 2. Protocol and complete input model — implemented and host-tested

The bounded protobuf decoder for message `0x23`:

- parse field 1 as a uint32 varint button map;
- invert only the known button mask;
- parse the nested analog group and ZigZag `sint32` values;
- ignore unknown fields safely;
- reject truncated/oversized frames without changing current input state;
- retain a raw hex/decoded capture mode behind a flag.

Add semantic states for all inputs listed in [docs/protocol.md](docs/protocol.md). Analog channels 0 and 1 each become negative and positive logical actions with configurable press/release thresholds. Channels 2 and 3 remain captured and named `reserved_2`/`reserved_3`.

Acceptance:

- fixtures cover every single button, every known chord, idle, malformed input, positive/negative lever travel, and threshold jitter;
- edge tests prove independent press/release transitions;
- controller disconnect always produces an empty HID report.

### 3. Application profiles and every-input mapping — implemented and host-tested

Two profiles are present:

1. `delta_emulator`: one stable keyboard vocabulary for Delta's NES, SNES, GB/GBC, GBA, DS, and Genesis cores. Every remaining Ride control still receives a unique spare key so it can be bound per system.
2. `diagnostic_all_inputs`: every discrete input and every lever direction emits a unique key, making mapping verification easy in Notes or a keyboard tester.

N64 is explicitly not covered by `delta_emulator`: its analog stick and four C directions need a separate mapping/HID decision. A later `delta_n64` profile may use keyboard thresholds or a composite gamepad only after hardware testing.

Initial `delta_emulator` defaults:

| Ride input | HID key | Intended Delta action |
|---|---:|---|
| D-pad U/D/L/R | Arrow keys | D-pad |
| A / B | `x` / `z` | A / B |
| Y / right action-pad Z | `a` / `s` | Y / X |
| Left side upper | `q` | L |
| Right side upper | `w` | R |
| Left orange logo/power | Tab | Select |
| Right orange logo/power | Enter | Start |
| Left drop button (LB) | Space | Fast Forward |
| Right drop button (RB) | Escape | Delta menu/pause |
| Left side middle | `e` | configurable spare |
| Right side middle | `r` | configurable spare |
| Left lever − / + | `1` / `2` | configurable |
| Right lever − / + | `3` / `4` | configurable |

These core keys match DeltaCore's bundled keyboard fallback: arrows, `x`/`z`/`s`/`a`, `q`/`w`, Return, Tab, and Escape. A saved per-system/controller mapping overrides that fallback, so Delta's Customize Controls screen remains the runtime source of truth after customization. Firmware guarantees distinct, stable, stateful keys; the [Delta mapping guide](docs/delta-mapping.md) records both the upstream defaults and optional bindings.

Acceptance:

- every physical control is observable and independently bindable;
- holding a direction remains down;
- B + direction remains simultaneous;
- pressing two controls mapped to the same HID key and releasing one does not release the other;
- lever jitter around a threshold does not chatter.

### 4. Reconnect, OTA, and recovery — implemented, hardware pending

The implementation exposes an explicit lifecycle equivalent to:

```text
SCANNING → CONNECTING → DISCOVERING → HANDSHAKING → READY
    ↑                                                │
    └──────── backoff + rescan ← DISCONNECTED ──────┘
```

On any transition out of `READY`, send an empty HID report before cleanup. Re-scan continuously with bounded backoff and filter for Ride left rather than relying on power-on order.

OTA lifecycle:

- on begin: empty HID report, stop new input processing, disconnect the HID host and Ride, and stop HID advertising;
- update over Wi-Fi using ESPHome native OTA and a new, unique `ota_password` secret;
- reboot into the new image, with ESPHome safe mode/rollback left enabled;
- keep USB serial recovery documented because OTA cannot repair every partition/bootloader failure.

Acceptance:

- controller sleep/wake recovers without ESP32 reboot;
- iPad Bluetooth off/on recovers without stuck keys;
- OTA during an idle paired session succeeds and leaves no held key;
- an intentionally bad application boot reaches ESPHome recovery behavior.

### 5. Status and endurance — implemented diagnostics, hardware pending

The reference configuration assigns the XIAO's one active-low orange user LED to the bridge and enables low-rate diagnostics. Validate the intended patterns on hardware:

- one slow blink: starting, scanning, or HID-only;
- short-short pause: Ride ready, no HID host;
- solid: Ride and HID host ready;
- fast blink: OTA;
- repeating triple blink: recoverable fault;
- off: stopped.

Run the full 45-minute FireRed ride, including one controller sleep/wake. Also run a 4-hour synthetic reconnect/chord test while tracking minimum free heap, largest free block, dropped BLE events, reconnect count, and unexpected release-all count.

## Test strategy

### Fast tests on every change

- varint and ZigZag decoding;
- known-mask inversion and all 16 discrete inputs;
- chord transitions and partial releases;
- analog threshold/hysteresis in both directions;
- duplicate physical-to-HID mappings;
- malformed/truncated frames and fuzzed lengths;
- report rollover behavior.

### CI

- pin ESPHome rather than building against an unbounded latest release;
- validate the example YAML with dummy secrets;
- compile the ESP32-S3 image;
- run the allocation-free core's CMake/CTest suite with warnings as errors;
- fail if the external component introduces direct Bluedroid initialization or raw process-global GAP/GATTS registration.

### Hardware matrix

- USB first flash and serial recovery;
- native OTA from the existing home server;
- iPad Notes pairing/reconnect;
- Delta NES, SNES, GB/GBC, GBA, DS, and Genesis binding screens;
- all discrete buttons, all chords needed by play, both analog lever signs;
- Ride left/right startup in either order;
- competing Zwift/BikeControl apps closed, then deliberately opened to document connection behavior;
- controller sleep, iPad sleep, ESP reboot, Wi-Fi loss, and OTA.

## Risks to resolve early

| Risk | Mitigation |
|---|---|
| ESPHome has no stock BLE HID keyboard | Keep the in-repository HID server minimal and broker-aware; retain the simultaneous Ride+iPad hardware test as a hard gate. |
| HID code initializes BLE or replaces global callbacks | ESPHome remains sole stack owner; review/CI reject `esp_bt_controller_init`, `esp_bluedroid_init`, and raw callback registration in the component. |
| Wi-Fi + central BLE + peripheral BLE pressure RAM/radio | Minimal ESPHome config, no web server by default, fixed buffers, heap telemetry, endurance gate. |
| Analog lever meaning/sign varies | Capture raw signed values, calibrate each side, use YAML thresholds and hysteresis. |
| Proprietary protocol changes | Proper protobuf parsing, manufacturer/service filters, sanitized fixtures, protocol version notes. |
| Stuck keys after packet or link loss | Whole-report generation plus empty report on every teardown/OTA path. |
| iPad bonding changes during firmware updates | Preserve NVS/partition layout; test OTA reboot and re-pair recovery before endurance work. |
| Upstream GPL code provenance | GPL-3.0-only repo, prominent Zword attribution, and exact SHA/file records before importing or adapting code. |

## Remaining inputs before hardware validation

These do not block repository implementation, but they do block the first device test:

- confirmation that Device Builder can use the repository pin, ESPHome `2026.7.4`;
- hardware confirmation that automatic discovery selects only Ride Left and reconnects to the per-boot locked address;
- confirmation that the documented upper/middle controls match the printed LS1/LS2 and RS1/RS2 numbering;
- which sign of each analog lever should be considered steering vs braking on the physical bike.

Known hardware/configuration facts:

- Seeed Studio XIAO ESP32-S3, 8 MB flash and 8 MB octal PSRAM;
- user LED GPIO21, active low;
- ESP-IDF framework;
- existing Device Builder secrets: `ssid`, `password`, `fallback_password`, and `api_key`;
- one new required secret: `ota_password`; no controller address is required for automatic discovery;
- Wi-Fi output power convention: `8.5db`;
- native ESPHome OTA, encrypted API, captive-portal fallback, uptime/Wi-Fi diagnostics, and restart button.

The baseline deliberately omits `web_server` and Home Assistant time. They are not needed by the bridge and consume resources that should be reserved until the dual-role BLE heap test passes. They can be restored later if measurements show adequate margin.
