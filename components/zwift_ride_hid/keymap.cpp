// SPDX-License-Identifier: GPL-3.0-only
#include "keymap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::zwift_ride_hid {
namespace {

constexpr Keymap kDeltaEmulatorMap{
    "delta_emulator",
    {
        hid_usage::ARROW_LEFT,   // DPAD_LEFT
        hid_usage::ARROW_UP,     // DPAD_UP
        hid_usage::ARROW_RIGHT,  // DPAD_RIGHT
        hid_usage::ARROW_DOWN,   // DPAD_DOWN
        // The action pad is mapped by position, not by letter. Zwift places
        // Y top, A right, B bottom, Z left; Delta's diamond is X top, A right,
        // B bottom, Y left. Binding Zwift Y to Delta's Y would put the top
        // button on the left one, so each Zwift button takes the key Delta
        // assigns to the button in the same place: s top, x right, z bottom,
        // a left.
        hid_usage::X,            // BUTTON_A    right  -> Delta A
        hid_usage::Z,            // BUTTON_B    bottom -> Delta B
        hid_usage::S,            // BUTTON_Y    top    -> Delta X
        hid_usage::A,            // BUTTON_Z    left   -> Delta Y
        hid_usage::Q,            // LEFT_SIDE_UPPER
        hid_usage::E,            // LEFT_SIDE_MIDDLE
        hid_usage::SPACE,        // LEFT_SIDE_LOWER
        hid_usage::TAB,          // LEFT_POWER / Select
        hid_usage::W,            // RIGHT_SIDE_UPPER
        hid_usage::R,            // RIGHT_SIDE_MIDDLE
        hid_usage::P,            // RIGHT_SIDE_LOWER / Delta menu
        hid_usage::RETURN,       // RIGHT_POWER / Start
        hid_usage::DIGIT_1,      // LEFT_LEVER_NEGATIVE
        hid_usage::DIGIT_2,      // LEFT_LEVER_POSITIVE
        hid_usage::DIGIT_3,      // RIGHT_LEVER_NEGATIVE
        hid_usage::DIGIT_4,      // RIGHT_LEVER_POSITIVE
    }};

constexpr Keymap kDiagnosticAllInputsMap{
    "diagnostic_all_inputs",
    {
        hid_usage::ARROW_LEFT,   // DPAD_LEFT
        hid_usage::ARROW_UP,     // DPAD_UP
        hid_usage::ARROW_RIGHT,  // DPAD_RIGHT
        hid_usage::ARROW_DOWN,   // DPAD_DOWN
        hid_usage::A,            // BUTTON_A
        hid_usage::B,            // BUTTON_B
        hid_usage::Y,            // BUTTON_Y
        hid_usage::Z,            // BUTTON_Z
        hid_usage::F1,           // LEFT_SIDE_UPPER
        hid_usage::F2,           // LEFT_SIDE_MIDDLE
        hid_usage::F3,           // LEFT_SIDE_LOWER
        hid_usage::F4,           // LEFT_POWER
        hid_usage::F5,           // RIGHT_SIDE_UPPER
        hid_usage::F6,           // RIGHT_SIDE_MIDDLE
        hid_usage::F7,           // RIGHT_SIDE_LOWER
        hid_usage::F8,           // RIGHT_POWER
        hid_usage::DIGIT_1,      // LEFT_LEVER_NEGATIVE
        hid_usage::DIGIT_2,      // LEFT_LEVER_POSITIVE
        hid_usage::DIGIT_3,      // RIGHT_LEVER_NEGATIVE
        hid_usage::DIGIT_4,      // RIGHT_LEVER_POSITIVE
    }};

bool is_modifier(uint8_t usage) {
  return usage >= hid_usage::LEFT_CONTROL && usage <= hid_usage::RIGHT_GUI;
}

}  // namespace

const Keymap &keymap_for_profile(KeymapProfile profile) {
  switch (profile) {
    case KeymapProfile::DIAGNOSTIC_ALL_INPUTS:
      return kDiagnosticAllInputsMap;
    case KeymapProfile::DELTA_EMULATOR:
    default:
      return kDeltaEmulatorMap;
  }
}

bool keymap_profile_from_name(const char *name, KeymapProfile *profile) {
  if (name == nullptr || profile == nullptr) {
    return false;
  }
  if (std::strcmp(name, kDeltaEmulatorMap.name) == 0) {
    *profile = KeymapProfile::DELTA_EMULATOR;
    return true;
  }
  if (std::strcmp(name, kDiagnosticAllInputsMap.name) == 0) {
    *profile = KeymapProfile::DIAGNOSTIC_ALL_INPUTS;
    return true;
  }
  return false;
}

KeyboardReportStatus build_keyboard_report(ActionMask active_actions,
                                            const Keymap &keymap,
                                            KeyboardReport *report) {
  if (report == nullptr) {
    return KeyboardReportStatus::NULL_ARGUMENT;
  }

  KeyboardReport candidate{};
  uint8_t key_count = 0;
  bool overflow = false;
  for (uint8_t action = 0; action < kInputActionCount; action++) {
    if ((active_actions & (static_cast<ActionMask>(1UL) << action)) == 0) {
      continue;
    }

    const uint8_t usage = keymap.usages[action];
    if (usage == hid_usage::NONE) {
      continue;
    }
    if (is_modifier(usage)) {
      candidate.modifiers |= static_cast<uint8_t>(1U << (usage - hid_usage::LEFT_CONTROL));
      continue;
    }
    if (overflow) {
      continue;
    }

    bool duplicate = false;
    for (uint8_t index = 0; index < key_count; index++) {
      if (candidate.keys[index] == usage) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    if (key_count == kKeyboardReportKeyCount) {
      overflow = true;
      continue;
    }
    candidate.keys[key_count++] = usage;
  }

  if (overflow) {
    for (auto &key : candidate.keys) {
      key = hid_usage::ERROR_ROLLOVER;
    }
  }
  *report = candidate;
  return overflow ? KeyboardReportStatus::SIX_KEY_ROLLOVER : KeyboardReportStatus::OK;
}

void clear_keyboard_report(KeyboardReport *report) {
  if (report != nullptr) {
    *report = KeyboardReport{};
  }
}

}  // namespace esphome::zwift_ride_hid
