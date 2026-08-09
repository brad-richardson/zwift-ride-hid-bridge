// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "ride_protocol.h"

namespace esphome::zwift_ride_hid {

enum class InputAction : uint8_t {
  DPAD_LEFT = 0,
  DPAD_UP,
  DPAD_RIGHT,
  DPAD_DOWN,
  BUTTON_A,
  BUTTON_B,
  BUTTON_Y,
  BUTTON_Z,
  LEFT_SIDE_UPPER,
  LEFT_SIDE_MIDDLE,
  LEFT_SIDE_LOWER,
  LEFT_POWER,
  RIGHT_SIDE_UPPER,
  RIGHT_SIDE_MIDDLE,
  RIGHT_SIDE_LOWER,
  RIGHT_POWER,
  LEFT_LEVER_NEGATIVE,
  LEFT_LEVER_POSITIVE,
  RIGHT_LEVER_NEGATIVE,
  RIGHT_LEVER_POSITIVE,
  COUNT,
};

using ActionMask = uint32_t;

constexpr uint8_t kInputActionCount = static_cast<uint8_t>(InputAction::COUNT);

constexpr ActionMask action_mask(InputAction action) {
  return static_cast<ActionMask>(1UL << static_cast<uint8_t>(action));
}

struct InputTransitions {
  ActionMask pressed{0};
  ActionMask released{0};

  bool changed() const { return this->pressed != 0 || this->released != 0; }
};

class InputState {
 public:
  InputState() = default;
  InputState(uint8_t press_threshold, uint8_t release_threshold);

  // Thresholds must satisfy 0 <= release < press. Invalid values are rejected
  // without changing the existing thresholds.
  bool set_thresholds(uint8_t press_threshold, uint8_t release_threshold);

  // Applies a fully decoded packet. Digital controls are snapshots. Analog
  // channels omitted from a packet retain their prior state.
  InputTransitions apply(const RideInputPacket &packet);

  // Used for disconnect, OTA, reset, and HID-session teardown.
  InputTransitions release_all();

  bool active(InputAction action) const { return (this->active_actions_ & action_mask(action)) != 0; }
  ActionMask active_actions() const { return this->active_actions_; }
  int32_t analog(uint8_t channel) const {
    return channel < kAnalogChannelCount ? this->analog_[channel] : 0;
  }
  bool has_analog(uint8_t channel) const {
    return channel < kAnalogChannelCount && (this->analog_seen_mask_ & (1U << channel)) != 0;
  }
  uint8_t press_threshold() const { return this->press_threshold_; }
  uint8_t release_threshold() const { return this->release_threshold_; }

 private:
  void update_lever_(ActionMask *actions, InputAction negative_action,
                     InputAction positive_action, int32_t value) const;

  ActionMask active_actions_{0};
  int32_t analog_[kAnalogChannelCount]{0, 0, 0, 0};
  uint8_t analog_seen_mask_{0};
  uint8_t press_threshold_{35};
  uint8_t release_threshold_{20};
};

}  // namespace esphome::zwift_ride_hid
