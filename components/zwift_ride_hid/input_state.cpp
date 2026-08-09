// SPDX-License-Identifier: GPL-3.0-only
#include "input_state.h"

#include <cstddef>
#include <cstdint>

namespace esphome::zwift_ride_hid {
namespace {

struct DigitalBinding {
  uint32_t ride_mask;
  InputAction action;
};

constexpr DigitalBinding kDigitalBindings[] = {
    {0x000001UL, InputAction::DPAD_LEFT},
    {0x000002UL, InputAction::DPAD_UP},
    {0x000004UL, InputAction::DPAD_RIGHT},
    {0x000008UL, InputAction::DPAD_DOWN},
    {0x000010UL, InputAction::BUTTON_A},
    {0x000020UL, InputAction::BUTTON_B},
    {0x000040UL, InputAction::BUTTON_Y},
    {0x000080UL, InputAction::BUTTON_Z},
    {0x000100UL, InputAction::LEFT_SIDE_UPPER},
    {0x000200UL, InputAction::LEFT_SIDE_MIDDLE},
    {0x000400UL, InputAction::LEFT_SIDE_LOWER},
    {0x000800UL, InputAction::LEFT_POWER},
    {0x001000UL, InputAction::RIGHT_SIDE_UPPER},
    {0x002000UL, InputAction::RIGHT_SIDE_MIDDLE},
    {0x004000UL, InputAction::RIGHT_SIDE_LOWER},
    {0x008000UL, InputAction::RIGHT_POWER},
};

constexpr ActionMask kAnalogActionMask =
    action_mask(InputAction::LEFT_LEVER_NEGATIVE) |
    action_mask(InputAction::LEFT_LEVER_POSITIVE) |
    action_mask(InputAction::RIGHT_LEVER_NEGATIVE) |
    action_mask(InputAction::RIGHT_LEVER_POSITIVE);

}  // namespace

InputState::InputState(uint8_t press_threshold, uint8_t release_threshold) {
  this->set_thresholds(press_threshold, release_threshold);
}

bool InputState::set_thresholds(uint8_t press_threshold, uint8_t release_threshold) {
  if (press_threshold == 0 || press_threshold <= release_threshold) {
    return false;
  }
  this->press_threshold_ = press_threshold;
  this->release_threshold_ = release_threshold;
  return true;
}

void InputState::update_lever_(ActionMask *actions, InputAction negative_action,
                               InputAction positive_action, int32_t value) const {
  const ActionMask negative_mask = action_mask(negative_action);
  const ActionMask positive_mask = action_mask(positive_action);
  bool negative = (*actions & negative_mask) != 0;
  bool positive = (*actions & positive_mask) != 0;

  if (negative && value >= -static_cast<int32_t>(this->release_threshold_)) {
    negative = false;
  }
  if (positive && value <= static_cast<int32_t>(this->release_threshold_)) {
    positive = false;
  }

  if (!negative && !positive) {
    if (value <= -static_cast<int32_t>(this->press_threshold_)) {
      negative = true;
    } else if (value >= static_cast<int32_t>(this->press_threshold_)) {
      positive = true;
    }
  }

  *actions &= ~(negative_mask | positive_mask);
  if (negative) {
    *actions |= negative_mask;
  }
  if (positive) {
    *actions |= positive_mask;
  }
}

InputTransitions InputState::apply(const RideInputPacket &packet) {
  const ActionMask previous = this->active_actions_;
  ActionMask next = previous & kAnalogActionMask;

  for (const auto &binding : kDigitalBindings) {
    if ((packet.pressed_buttons & binding.ride_mask) != 0) {
      next |= action_mask(binding.action);
    }
  }

  for (uint8_t channel = 0; channel < kAnalogChannelCount; channel++) {
    if (!packet.has_analog(channel)) {
      continue;
    }
    this->analog_[channel] = packet.analog[channel];
    this->analog_seen_mask_ |= static_cast<uint8_t>(1U << channel);
  }

  if (packet.has_analog(0)) {
    this->update_lever_(&next, InputAction::LEFT_LEVER_NEGATIVE,
                        InputAction::LEFT_LEVER_POSITIVE, packet.analog[0]);
  }
  if (packet.has_analog(1)) {
    this->update_lever_(&next, InputAction::RIGHT_LEVER_NEGATIVE,
                        InputAction::RIGHT_LEVER_POSITIVE, packet.analog[1]);
  }

  this->active_actions_ = next;
  return {next & ~previous, previous & ~next};
}

InputTransitions InputState::release_all() {
  const ActionMask previous = this->active_actions_;
  this->active_actions_ = 0;
  for (auto &value : this->analog_) {
    value = 0;
  }
  this->analog_seen_mask_ = 0;
  return {0, previous};
}

}  // namespace esphome::zwift_ride_hid
