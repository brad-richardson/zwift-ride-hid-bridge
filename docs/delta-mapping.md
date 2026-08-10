# Delta emulator mapping

`delta_emulator` is the default profile. It emits ordinary keyboard keys that match DeltaCore's bundled hardware-keyboard fallback for the core controls, while giving every remaining Ride input a distinct spare key. In Delta, pair `Zwift Ride KB`, assign it to Player 1, and use **Customize Controls** for per-system or quick-action overrides.

DeltaCore's bundled map uses arrows for the D-pad; `x`, `z`, `s`, and `a` for A, B, X, and Y; `q` and `w` for L1 and R1; Return for Start; Tab for Select; and `p` for Menu. Delta's own keyboard-mapping screen was read on device for the SNES and DS skins to confirm those; it labels Menu `p`, and nothing observed there binds Escape. A saved Delta mapping takes precedence for that system/controller pair.

## Complete `delta_emulator` profile

| Stable semantic ID | Ride control / protocol alias | HID key | Default or suggested Delta role |
|---|---|---:|---|
| `dpad_left` | Left D-pad Left | Left Arrow | D-pad Left |
| `dpad_up` | Left D-pad Up | Up Arrow | D-pad Up |
| `dpad_right` | Left D-pad Right | Right Arrow | D-pad Right |
| `dpad_down` | Left D-pad Down | Down Arrow | D-pad Down |
| `button_a` | Right action pad, A (right) | `x` | A |
| `button_b` | Right action pad, B (bottom) | `z` | B |
| `button_y` | Right action pad, Y (top) | `s` | X |
| `button_z` | Right action pad, Z (left) | `a` | Y |
| `left_side_upper` | LS1 / upper left shifter (`SHFT_UP_L`) | `q` | L / L1 |
| `left_side_middle` | LS2 / middle left shifter (`SHFT_DN_L`) | `e` | Spare / Genesis X |
| `left_side_lower` | LB / left drop button (`POWERUP_L`) | Space | Fast Forward / spare |
| `left_power` | Left orange logo/power (`ONOFF_L`) | Tab | Select |
| `right_side_upper` | RS1 / upper right shifter (`SHFT_UP_R`) | `w` | R / R1 |
| `right_side_middle` | RS2 / middle right shifter (`SHFT_DN_R`) | `r` | Spare / Genesis Z |
| `right_side_lower` | RB / right drop button (`POWERUP_R`) | `p` | Delta menu/pause |
| `right_power` | Right orange logo/power (`ONOFF_R`) | Return | Start |
| `left_lever_negative` | Left analog lever, negative polarity | `1` | Configurable spare |
| `left_lever_positive` | Left analog lever, positive polarity | `2` | Configurable spare |
| `right_lever_negative` | Right analog lever, negative polarity | `3` | Configurable spare |
| `right_lever_positive` | Right analog lever, positive polarity | `4` | Configurable spare |

The LS1/LS2 and RS1/RS2 names above are the profile's intended upper/middle aliases. The underlying protocol identities and emitted keys are stable, but the numbered physical ordering has not yet been checked on the target hardware. If the official `1`/`2` labels prove reversed, documentation can be corrected without changing `left_side_upper`, `left_side_middle`, `right_side_upper`, `right_side_middle`, or their keys.

Likewise, the firmware preserves the signed lever values but does not guess whether a given sign physically means brake, inward steering, or outward steering. Hardware capture must establish that. The four polarity actions stay separately bindable either way. With the reference hysteresis, a direction presses at magnitude 35 and releases below magnitude 20; crossing through center releases one polarity before the other can press.

The action pad is bound by position rather than by matching letters, because the
two diamonds disagree. Zwift places Y top, A right, B bottom, Z left; Delta's is
X top, A right, B bottom, Y left. Binding Zwift Y to Delta's Y and Zwift Z to
Delta's X — the obvious letter-wise reading — put the top button on the left one
and the left button on the top one. Each Zwift button therefore takes the key
Delta assigns to the button in the same physical place, so the pad on the bars
and the pad on screen agree: `s` top, `x` right, `z` bottom, `a` left.

Delta's menu is `p`, not Escape. Escape reaches nothing in Delta, so the earlier
binding left the right drop button doing nothing at all.

The right action-pad Z button is independent of both orange Zwift-logo/power controls. Short logo taps can therefore provide Select and Start. A long hold is still the controllers' power gesture, so neither logo control is a good choice for an emulator action that must be held.

The second shifter pair (`e`/`r`) is intentionally not consumed by Delta's bundled fallback. It is available for pause, quick save/load, alternate shoulder placement, or ergonomic alternate Select/Start bindings. LS1, LS2, RS1, and RS2 are discrete button bits; they are not the signed analog lever thresholds.

## `diagnostic_all_inputs` profile

Use this profile in Notes or a keyboard tester when identifying physical controls. Every known discrete input and active lever polarity has a unique key:

| Inputs | Emitted keys |
|---|---|
| D-pad Left / Up / Right / Down | Left / Up / Right / Down Arrow |
| A / B / Y / action-pad Z | `a` / `b` / `y` / `z` |
| LS1 / LS2 / LB / left logo | F1 / F2 / F3 / F4 |
| RS1 / RS2 / RB / right logo | F5 / F6 / F7 / F8 |
| Left lever negative / positive | `1` / `2` |
| Right lever negative / positive | `3` / `4` |

The keyboard report is standard six-key rollover (6KRO). Holds and normal gameplay chords are represented together in one report. More than six distinct non-modifier keys produces the standard HID ErrorRollOver report and a warning instead of silently dropping an arbitrary input.

## Recommended per-system bindings

| Delta system | Suggested binding |
|---|---|
| NES | D-pad = arrows; A = `x`; B = `z`; Select = Tab; Start = Return |
| Game Boy / Game Boy Color | D-pad = arrows; A = `x`; B = `z`; Select = Tab; Start = Return |
| SNES | D-pad = arrows; A = `x`; B = `z`; X = `s`; Y = `a`; L = `q`; R = `w`; Select = Tab; Start = Return |
| GBA | D-pad = arrows; A = `x`; B = `z`; L = `q`; R = `w`; Select = Tab; Start = Return |
| DS | D-pad = arrows; A = `x`; B = `z`; X = `s`; Y = `a`; L = `q`; R = `w`; Select = Tab; Start = Return; use the iPad for touch |
| Genesis, 3-button | Explicitly bind A = `x`; B = `z`; C = `s`; Start = Return |
| Genesis, 6-button | Add X = `a`; Y = `q`; Z = `w`; assign Mode to Space or another spare if Delta exposes it |

Simpler systems ignore extra emitted keys. Confirm Genesis labels in Customize Controls because its six-button names do not align perfectly with the usual A/B/X/Y/L1/R1 vocabulary. Assign quick actions only after verifying they do not collide with a system's controller inputs.

## Why N64 is excluded

N64 needs a useful analog-stick model and four C directions in addition to A/B, D-pad, L/R/Z, and Start. Forcing those onto the universal keyboard profile would overload controls or throw away the Ride levers' analog information. `delta_emulator` therefore makes no N64 compatibility promise. A later `delta_n64` profile can be designed after real lever captures and a keyboard-versus-composite-gamepad decision.

## Sources

- [Delta controller setup and Customize Controls](https://faq.deltaemulator.com/using-delta/controllers)
- [DeltaCore bundled keyboard mapping](https://github.com/rileytestut/DeltaCore/blob/633dfa86967816315fe19b482511dab1ce517f28/DeltaCore/Supporting%20Files/KeyboardGameController.deltamapping)
- [DeltaCore default mapping loader](https://github.com/rileytestut/DeltaCore/blob/633dfa86967816315fe19b482511dab1ce517f28/DeltaCore/Game%20Controllers/Keyboard/KeyboardGameController.swift#L77-L93)
- [Delta saved-mapping precedence](https://github.com/rileytestut/Delta/blob/c1d3d068e019e6493eed45654569db3cc5beb86a/Delta/Settings/Controllers/ControlsEditorView.swift#L38-L49)
- [Delta supported systems and game formats](https://faq.deltaemulator.com/getting-started/importing-games)
- [Official Zwift Ride controller layout](https://support.zwift.com/en_us/using-your-zwift-ride-controller-Skw8efMIC)
