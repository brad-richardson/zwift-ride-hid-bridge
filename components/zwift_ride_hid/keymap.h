// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "input_state.h"

namespace esphome::zwift_ride_hid {

namespace hid_usage {
constexpr uint8_t NONE = 0x00;
constexpr uint8_t ERROR_ROLLOVER = 0x01;
constexpr uint8_t A = 0x04;
constexpr uint8_t B = 0x05;
constexpr uint8_t E = 0x08;
constexpr uint8_t Q = 0x14;
constexpr uint8_t R = 0x15;
constexpr uint8_t S = 0x16;
constexpr uint8_t W = 0x1A;
constexpr uint8_t X = 0x1B;
constexpr uint8_t Y = 0x1C;
constexpr uint8_t Z = 0x1D;
constexpr uint8_t DIGIT_1 = 0x1E;
constexpr uint8_t DIGIT_2 = 0x1F;
constexpr uint8_t DIGIT_3 = 0x20;
constexpr uint8_t DIGIT_4 = 0x21;
constexpr uint8_t RETURN = 0x28;
constexpr uint8_t ESCAPE = 0x29;
constexpr uint8_t TAB = 0x2B;
constexpr uint8_t SPACE = 0x2C;
constexpr uint8_t F1 = 0x3A;
constexpr uint8_t F2 = 0x3B;
constexpr uint8_t F3 = 0x3C;
constexpr uint8_t F4 = 0x3D;
constexpr uint8_t F5 = 0x3E;
constexpr uint8_t F6 = 0x3F;
constexpr uint8_t F7 = 0x40;
constexpr uint8_t F8 = 0x41;
constexpr uint8_t ARROW_RIGHT = 0x4F;
constexpr uint8_t ARROW_LEFT = 0x50;
constexpr uint8_t ARROW_DOWN = 0x51;
constexpr uint8_t ARROW_UP = 0x52;
constexpr uint8_t LEFT_CONTROL = 0xE0;
constexpr uint8_t RIGHT_GUI = 0xE7;
}  // namespace hid_usage

struct Keymap {
  const char *name;
  uint8_t usages[kInputActionCount];
};

enum class KeymapProfile : uint8_t {
  DELTA_EMULATOR = 0,
  DIAGNOSTIC_ALL_INPUTS,
};

const Keymap &keymap_for_profile(KeymapProfile profile);
bool keymap_profile_from_name(const char *name, KeymapProfile *profile);

constexpr uint8_t kKeyboardReportKeyCount = 6;

struct KeyboardReport {
  uint8_t modifiers{0};
  uint8_t reserved{0};
  uint8_t keys[kKeyboardReportKeyCount]{0, 0, 0, 0, 0, 0};
};

enum class KeyboardReportStatus : uint8_t {
  OK = 0,
  SIX_KEY_ROLLOVER,
  NULL_ARGUMENT,
};

// Aggregates the entire active semantic state into one boot-keyboard report.
// Duplicate bindings are emitted once. Modifier usages E0..E7 are folded into
// the modifier byte and do not consume a 6KRO slot. On overflow all six key
// slots contain the standard ErrorRollOver usage and status is explicit.
KeyboardReportStatus build_keyboard_report(ActionMask active_actions,
                                            const Keymap &keymap,
                                            KeyboardReport *report);

void clear_keyboard_report(KeyboardReport *report);

}  // namespace esphome::zwift_ride_hid
