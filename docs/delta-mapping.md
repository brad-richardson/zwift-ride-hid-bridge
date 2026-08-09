# Delta emulator mapping

`delta_emulator` is the default profile. It emits a stable set of ordinary keyboard keys and intentionally matches DeltaCore's bundled hardware-keyboard fallback. In Delta, connect `Zwift Ride KB` and select the keyboard for Player 1. Use **Customize Controls** only when a system or quick action needs an override.

The bundled DeltaCore map is arrows for the D-pad; `x`, `z`, `s`, and `a` for A, B, X, and Y; `q` and `w` for L1 and R1; Return for Start; Tab for Select; and Escape or `p` for Menu. Delta loads a saved mapping instead when one exists for that system/controller pair. The tables below are this project's physical layout and acceptance-test contract.

## Keys emitted by the Ride controls

| Ride semantic input | Physical control | Emitted key | Suggested role |
|---|---|---:|---|
| `dpad_up/down/left/right` | Left navigation pad | Arrow keys | D-pad |
| `button_a` | Right A | `x` | A |
| `button_b` | Right B | `z` | B |
| `button_y` | Right Y | `a` | Y |
| `button_z` | Right action-pad Z | `s` | X |
| `left_side_upper` | Left upper shifter | `q` | L |
| `right_side_upper` | Right upper shifter | `w` | R |
| `left_side_middle` | Left middle shifter | `e` | Extra / Genesis X |
| `right_side_middle` | Right middle shifter | `r` | Extra / Genesis Z |
| `left_power` | Left orange Zwift-logo/power | Tab | Select |
| `right_power` | Right orange Zwift-logo/power | Enter | Start |
| `left_side_lower` | Left drop button, LB | Space | Fast Forward / spare |
| `right_side_lower` | Right drop button, RB | Escape | Menu/pause / spare |
| `left_lever_negative/positive` | Left analog paddle, two signs | `1` / `2` | Configurable |
| `right_lever_negative/positive` | Right analog paddle, two signs | `3` / `4` | Configurable |

The right action-pad `Z` is distinct from both orange Zwift-logo/power buttons. All three have separate protocol bits. Use short taps for Start and Select: holding either logo button long enough may power down its controller.

The four shifter/power-adjust buttons are all discrete inputs (`LS1`, `LS2`, `RS1`, and `RS2`), not analog thresholds. One left/right pair supplies the default L/R shoulder keys and the second pair remains independently bindable. The exact upper/middle-to-1/2 ordering and the analog-paddle signs must be confirmed with a hardware capture before the names become a compatibility promise. The distinct emitted keys will not change merely to correct a physical-label description.

The inner/middle shifters deliberately remain `e` and `r`, which Delta does not consume in its bundled fallback. They are good candidates for pause, quick save/load, or an ergonomic alternate Select/Start pair. Because every input has a distinct key, that choice can be made in Delta's Customize Controls screen without a firmware rebuild.

## Recommended per-system bindings

| Delta system | Bind these emulator controls |
|---|---|
| NES | D-pad = arrows; A = `x`; B = `z`; Select = Tab; Start = Return |
| Game Boy / Game Boy Color | D-pad = arrows; A = `x`; B = `z`; Select = Tab; Start = Return |
| SNES | D-pad = arrows; A = `x`; B = `z`; X = `s`; Y = `a`; L = `q`; R = `w`; Select = Tab; Start = Return |
| GBA | D-pad = arrows; A = `x`; B = `z`; L = `q`; R = `w`; Select = Tab; Start = Return |
| DS | D-pad = arrows; A = `x`; B = `z`; X = `s`; Y = `a`; L = `q`; R = `w`; Select = Tab; Start = Return; touchscreen stays on the iPad |
| Genesis, 3-button | Recommended explicit binding: A = `x`; B = `z`; C = `s`; Start = Return |
| Genesis, 6-button | Add X = `a`; Y = `q`; Z = `w`; bind Mode to Space or another unused key if exposed |

NES and Game Boy simply ignore the extra emitted keys. SNES/GBA/DS use the shoulder keys. Genesis reuses all six default face/shoulder keys; confirm its labels in Customize Controls because its six-button naming differs from Delta's standard A/B/X/Y/L1/R1 vocabulary. Quick actions such as Fast Forward should be assigned only where Delta exposes the desired action without colliding with that system's controller inputs.

## Why N64 is excluded

N64 needs a useful analog-stick model plus four C directions in addition to A/B, D-pad, L/R/Z, and Start. Forcing that into this keyboard profile would either overload controls or discard the Ride paddles' analog information. Keep N64 unavailable in `delta_emulator`; design and test a separate `delta_n64` profile later.

## Sources

- [Delta controller setup and Customize Controls](https://faq.deltaemulator.com/using-delta/controllers)
- [DeltaCore bundled keyboard mapping](https://github.com/rileytestut/DeltaCore/blob/633dfa86967816315fe19b482511dab1ce517f28/DeltaCore/Supporting%20Files/KeyboardGameController.deltamapping)
- [DeltaCore default mapping loader](https://github.com/rileytestut/DeltaCore/blob/633dfa86967816315fe19b482511dab1ce517f28/DeltaCore/Game%20Controllers/Keyboard/KeyboardGameController.swift#L77-L93)
- [Delta saved-mapping precedence](https://github.com/rileytestut/Delta/blob/c1d3d068e019e6493eed45654569db3cc5beb86a/Delta/Settings/Controllers/ControlsEditorView.swift#L38-L49)
- [Delta supported systems and game formats](https://faq.deltaemulator.com/getting-started/importing-games)
- [Official Zwift Ride controller layout](https://support.zwift.com/en_us/using-your-zwift-ride-controller-Skw8efMIC)
